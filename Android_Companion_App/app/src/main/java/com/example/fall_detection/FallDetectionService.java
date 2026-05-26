package com.example.fall_detection;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;
import java.util.Queue;
import java.util.UUID;

public class FallDetectionService extends Service {
    private static final String TAG = "FallDetectionService";
    private static final String CHANNEL_ID = "FallDetectionChannel";
    private static final int NOTIF_ID = 1;

    // UUIDs
    private static final UUID SERVICE_UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
    private static final UUID ALERT_CHAR_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8");
    private static final UUID VITALS_CHAR_UUID = UUID.fromString("8d451240-2aa4-4780-87a4-e53b6fa6c9b3");
    private static final UUID TIME_CHAR_UUID = UUID.fromString("f36414a6-7880-4965-8b83-2945d81b8969");
    private static final UUID CLIENT_CHARACTERISTIC_CONFIG = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    // Communication
    public static final String ACTION_VITALS_UPDATED = "com.example.fall_detection.VITALS_UPDATED";
    public static final String ACTION_STATUS_UPDATED = "com.example.fall_detection.STATUS_UPDATED";
    public static final String ACTION_REQUEST_SYNC = "com.example.fall_detection.REQUEST_SYNC";
    public static final String EXTRA_DATA = "extra_data";
    public static final String EXTRA_STATUS = "extra_status";

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothGatt bluetoothGatt;
    private BluetoothLeScanner bleScanner;
    private boolean isScanning = false;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private SharedPreferences prefs;
    private String lastVitals = "0,0,0,0";
    private String currentStatus = "Disconnected";

    private final Queue<GattCommand> commandQueue = new LinkedList<>();
    private boolean isCommandPending = false;
    private enum CommandType { WRITE_CHAR, WRITE_DESC }
    private static class GattCommand {
        CommandType type; Object target;
        GattCommand(CommandType t, Object o) { this.type = t; this.target = o; }
    }

    private final IBinder binder = new LocalBinder();
    public class LocalBinder extends Binder { FallDetectionService getService() { return FallDetectionService.this; } }

    @Override public IBinder onBind(Intent intent) { return binder; }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        startMyForeground("Initializing...");
        prefs = getSharedPreferences("FallGuardPrefs", MODE_PRIVATE);
        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bm.getAdapter();
    }

    private void startMyForeground(String text) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                // Use only connectedDevice to avoid strict 'phoneCall' dialer restrictions
                startForeground(NOTIF_ID, getNotification(text), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            } else {
                startForeground(NOTIF_ID, getNotification(text));
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to start foreground service: " + e.getMessage());
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_REQUEST_SYNC.equals(intent.getAction())) {
            broadcastVitals(lastVitals);
            updateStatus(currentStatus);
        }
        if (!isScanning && bluetoothGatt == null) {
            String mac = prefs.getString("last_device_mac", "");
            if (!mac.isEmpty() && bluetoothAdapter.isEnabled()) startScan();
        }
        return START_STICKY;
    }

    public void startScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled() || bluetoothGatt != null) return;
        bleScanner = bluetoothAdapter.getBluetoothLeScanner();
        if (bleScanner == null || isScanning) return;

        isScanning = true;
        updateStatus("Scanning for Watch...");
        
        android.bluetooth.le.ScanFilter filter = new android.bluetooth.le.ScanFilter.Builder()
                .setServiceUuid(new android.os.ParcelUuid(SERVICE_UUID)).build();
        List<android.bluetooth.le.ScanFilter> filters = new ArrayList<>();
        filters.add(filter);

        bleScanner.startScan(filters, new android.bluetooth.le.ScanSettings.Builder()
                .setScanMode(android.bluetooth.le.ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback);

        handler.postDelayed(() -> {
            if (isScanning) {
                isScanning = false;
                bleScanner.stopScan(scanCallback);
                if (bluetoothGatt == null) {
                    updateStatus("Scan timeout. Retrying...");
                    handler.postDelayed(this::startScan, 15000);
                }
            }
        }, 10000);
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            isScanning = false;
            bleScanner.stopScan(scanCallback);
            prefs.edit().putString("last_device_mac", result.getDevice().getAddress()).apply();
            connectToDevice(result.getDevice());
        }
    };

    private void connectToDevice(BluetoothDevice device) {
        updateStatus("Connecting...");
        bluetoothGatt = device.connectGatt(this, false, gattCallback);
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int s, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                updateStatus("Connected. Syncing...");
                gatt.requestMtu(512);
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                bluetoothGatt = null;
                commandQueue.clear();
                isCommandPending = false;
                updateStatus("Disconnected. Retrying...");
                handler.postDelayed(FallDetectionService.this::startScan, 5000);
            }
        }

        @Override public void onMtuChanged(BluetoothGatt g, int m, int s) { g.discoverServices(); }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                BluetoothGattService s = gatt.getService(SERVICE_UUID);
                if (s != null) {
                    BluetoothGattCharacteristic t = s.getCharacteristic(TIME_CHAR_UUID);
                    if (t != null) {
                        t.setValue(new SimpleDateFormat("HH:mm:ss dd/MM", Locale.getDefault()).format(new Date()));
                        commandQueue.add(new GattCommand(CommandType.WRITE_CHAR, t));
                    }
                    queueSub(s, VITALS_CHAR_UUID);
                    queueSub(s, ALERT_CHAR_UUID);
                    processNextCommand();
                }
            }
        }

        private void queueSub(BluetoothGattService s, UUID u) {
            BluetoothGattCharacteristic c = s.getCharacteristic(u);
            if (c != null) {
                bluetoothGatt.setCharacteristicNotification(c, true);
                BluetoothGattDescriptor d = c.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG);
                d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                commandQueue.add(new GattCommand(CommandType.WRITE_DESC, d));
            }
        }

        @Override public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int s) { isCommandPending = false; processNextCommand(); }
        @Override public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int s) { isCommandPending = false; processNextCommand(); }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            byte[] value = characteristic.getValue();
            if (value == null) return;
            String data = new String(value);
            if (characteristic.getUuid().equals(VITALS_CHAR_UUID)) {
                lastVitals = data;
                broadcastVitals(data);
            } else if (characteristic.getUuid().equals(ALERT_CHAR_UUID)) {
                if (data.equals("1") || data.equals("2")) {
                    wakeUpPhone();
                    
                    // Direct Auto-Launch of Emergency Screen
                    Intent emergencyIntent = new Intent(FallDetectionService.this, EmergencyActivity.class);
                    emergencyIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                    startActivity(emergencyIntent);

                    showEmergencyNotification();
                }
            }
        }
    };

    private void wakeUpPhone() {
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            PowerManager.WakeLock wl = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK | PowerManager.ACQUIRE_CAUSES_WAKEUP, "FallGuard:EmergencyWake");
            wl.acquire(15000); // Wait 15s
        }
    }

    private synchronized void processNextCommand() {
        if (isCommandPending || commandQueue.isEmpty() || bluetoothGatt == null) {
            if (commandQueue.isEmpty() && bluetoothGatt != null) updateStatus("Watch Active & Protected");
            return;
        }
        GattCommand cmd = commandQueue.poll();
        if (cmd == null) return;
        isCommandPending = true;
        if (cmd.type == CommandType.WRITE_CHAR) bluetoothGatt.writeCharacteristic((BluetoothGattCharacteristic) cmd.target);
        else bluetoothGatt.writeDescriptor((BluetoothGattDescriptor) cmd.target);
    }

    private void broadcastVitals(String data) {
        Intent intent = new Intent(ACTION_VITALS_UPDATED);
        intent.putExtra(EXTRA_DATA, data);
        intent.setPackage(getPackageName()); // Explicit
        sendBroadcast(intent);
    }

    private void updateStatus(String status) {
        currentStatus = status;
        updateNotification("Status: " + status);
        Intent intent = new Intent(ACTION_STATUS_UPDATED);
        intent.putExtra(EXTRA_STATUS, status);
        intent.setPackage(getPackageName()); // Explicit
        sendBroadcast(intent);
    }

    private void showEmergencyNotification() {
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        
        Intent fullScreenIntent = new Intent(this, EmergencyActivity.class);
        fullScreenIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_USER_ACTION);
        
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        
        PendingIntent fullScreenPendingIntent = PendingIntent.getActivity(this, 0, fullScreenIntent, flags);

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle("EMERGENCY DETECTED")
                .setContentText("Emergency contact will be called in 10 seconds.")
                .setPriority(NotificationCompat.PRIORITY_MAX)
                .setCategory(NotificationCompat.CATEGORY_CALL)
                .setFullScreenIntent(fullScreenPendingIntent, true)
                .setOngoing(true)
                .setAutoCancel(false)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC);

        nm.notify(2, builder.build());
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel c = new NotificationChannel(CHANNEL_ID, "FallGuard", NotificationManager.IMPORTANCE_HIGH);
            c.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
            NotificationManager m = getSystemService(NotificationManager.class);
            if (m != null) m.createNotificationChannel(c);
        }
    }

    private Notification getNotification(String text) {
        Intent i = new Intent(this, MainActivity.class);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent p = PendingIntent.getActivity(this, 0, i, flags);

        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("FallGuard Monitoring")
                .setContentText(text)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentIntent(p)
                .setOngoing(true)
                .build();
    }

    private void updateNotification(String text) {
        NotificationManager m = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (m != null) m.notify(NOTIF_ID, getNotification(text));
    }

    @Override
    public void onDestroy() {
        if (bluetoothGatt != null) bluetoothGatt.disconnect();
        super.onDestroy();
    }
}
