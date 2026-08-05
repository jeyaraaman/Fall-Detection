import os
import numpy as np
import tensorflow as tf
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from dataset_loader import SisFallLoader
from model import create_cnn_gru_model
import matplotlib.pyplot as plt

def train_model():
    # 1. Configuration
    base_dir = r"c:\Users\jeyar\Antigravity\Fall_Detection\SisFall"
    window_size_sec = 2.0
    sampling_freq = 200
    timesteps = int(window_size_sec * sampling_freq)
    channels = 6
    
    # 2. Load Data
    print("Loading SisFall dataset...")
    loader = SisFallLoader(base_dir)
    # Limiting files for demonstration, remove limit_files=100 for full training
    X, y = loader.load_dataset(window_size_sec=window_size_sec, limit_files=200)
    
    if X is None:
        print("No data found. Check SisFall directory path.")
        return

    print(f"Dataset loaded. X shape: {X.shape}, y shape: {y.shape}")
    print(f"Class distribution: Fall={np.sum(y)}, No Fall={len(y) - np.sum(y)}")

    # 3. Preprocessing
    # Reshape X for scaling: (Samples * Timesteps, Channels)
    samples, ts, ch = X.shape
    X_reshaped = X.reshape(-1, ch)
    
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X_reshaped)
    
    # Reshape back to (Samples, Timesteps, Channels)
    X = X_scaled.reshape(samples, ts, ch)
    
    # 4. Split Data
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42, stratify=y)
    X_train, X_val, y_train, y_val = train_test_split(X_train, y_train, test_size=0.1, random_state=42, stratify=y_train)

    # 5. Create Model
    model = create_cnn_gru_model((timesteps, channels))
    
    # 6. Training
    early_stopping = tf.keras.callbacks.EarlyStopping(
        monitor='val_loss', 
        patience=5, 
        restore_best_weights=True
    )
    
    print("Starting training...")
    history = model.fit(
        X_train, y_train,
        epochs=30,
        batch_size=32,
        validation_data=(X_val, y_val),
        callbacks=[early_stopping]
    )

    # 7. Evaluation
    print("\nEvaluating on test set:")
    loss, acc, recall, precision = model.evaluate(X_test, y_test)
    print(f"Test Accuracy: {acc:.4f}, Recall: {recall:.4f}, Precision: {precision:.4f}")

    # 8. Save Model
    model_save_path = "fall_detection_model_cnn_gru.h5"
    model.save(model_save_path)
    print(f"Model saved to {model_save_path}")

    # 9. Convert to TFLite (Optional but recommended for ESP32)
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS,
        tf.lite.OpsSet.SELECT_TF_OPS
    ]
    tflite_model = converter.convert()
    with open("fall_detection_model.tflite", "wb") as f:
        f.write(tflite_model)
    print("Model converted to TFLite.")

if __name__ == "__main__":
    train_model()
