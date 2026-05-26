package com.example.fall_detection;

import android.Manifest;
import android.app.KeyguardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.CountDownTimer;
import android.telephony.SmsManager;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

public class EmergencyActivity extends AppCompatActivity {

    private TextView tvTimer;
    private Button btnCallNow, btnCancel;
    private CountDownTimer timer;
    private String phoneNumber;
    private Location freshLocation = null;
    private LocationManager locationManager;
    private LocationListener locationListener;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Android 13/14 requirements for showing over lock screen
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true);
            setTurnScreenOn(true);
            KeyguardManager km = (KeyguardManager) getSystemService(Context.KEYGUARD_SERVICE);
            if (km != null) km.requestDismissKeyguard(this, null);
        } else {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                    | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD);
        }

        setContentView(R.layout.activity_emergency);

        tvTimer = findViewById(R.id.tv_countdown);
        btnCallNow = findViewById(R.id.btn_call_now);
        btnCancel = findViewById(R.id.btn_cancel_alert);

        SharedPreferences prefs = getSharedPreferences("FallGuardPrefs", MODE_PRIVATE);
        phoneNumber = prefs.getString("emergency_phone", "");

        if (phoneNumber.isEmpty()) {
            Toast.makeText(this, "No Emergency Contact Set!", Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
            locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
            locationListener = new LocationListener() {
                @Override
                public void onLocationChanged(@NonNull Location location) {
                    freshLocation = location;
                }
            };
            if (locationManager != null) {
                try {
                    locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, locationListener);
                } catch (Exception e) {}
                try {
                    locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000, 0, locationListener);
                } catch (Exception e) {}
            }
        }

        startTimer();

        btnCallNow.setOnClickListener(v -> triggerCall());
        btnCancel.setOnClickListener(v -> {
            if (timer != null) timer.cancel();
            finish();
        });
    }

    private void startTimer() {
        timer = new CountDownTimer(10000, 1000) {
            @Override
            public void onTick(long millisUntilFinished) {
                tvTimer.setText("Calling in " + (millisUntilFinished / 1000) + "s");
            }

            @Override
            public void onFinish() {
                triggerCall();
            }
        }.start();
    }

    private void triggerCall() {
        if (timer != null) timer.cancel();
        
        if (locationManager != null && locationListener != null) {
            locationManager.removeUpdates(locationListener);
        }

        // Attempt to send SMS with Location
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.SEND_SMS) == PackageManager.PERMISSION_GRANTED) {
            try {
                SmsManager smsManager;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    smsManager = this.getSystemService(SmsManager.class);
                } else {
                    smsManager = SmsManager.getDefault();
                }
                
                String message = "EMERGENCY: A fall has been detected!";
                
                if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
                    Location bestLocation = freshLocation;
                    
                    if (bestLocation == null && locationManager != null) {
                        // Iterate through all available providers to find the most recent location
                        for (String provider : locationManager.getProviders(true)) {
                            Location l = locationManager.getLastKnownLocation(provider);
                            if (l != null && (bestLocation == null || l.getTime() > bestLocation.getTime())) {
                                bestLocation = l;
                            }
                        }
                    }
                    
                    if (bestLocation != null) {
                        message += "\nLocation: https://maps.google.com/?q=" + bestLocation.getLatitude() + "," + bestLocation.getLongitude();
                    } else {
                        message += "\nLocation: [GPS Signal Unavailable]";
                    }
                }
                
                if (smsManager != null) {
                    smsManager.sendTextMessage(phoneNumber, null, message, null, null);
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        Intent intent = new Intent(Intent.ACTION_CALL);
        intent.setData(Uri.parse("tel:" + phoneNumber));
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CALL_PHONE) == PackageManager.PERMISSION_GRANTED) {
            startActivity(intent);
        }
        finish();
    }
}
