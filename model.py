import tensorflow as tf
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Conv1D, MaxPooling1D, GRU, Dense, Dropout, BatchNormalization

def create_cnn_gru_model(input_shape):
    """
    Creates a hybrid CNN-GRU model for fall detection.
    input_shape: (timesteps, channels), e.g., (400, 6)
    """
    model = Sequential([
        # CNN Part - Extracts spatial features (local patterns in sensor data)
        Conv1D(filters=64, kernel_size=3, activation='relu', input_shape=input_shape),
        BatchNormalization(),
        MaxPooling1D(pool_size=2),
        
        Conv1D(filters=32, kernel_size=3, activation='relu'),
        BatchNormalization(),
        MaxPooling1D(pool_size=2),
        
        # GRU Part - Processes temporal dependencies (sequence of events)
        GRU(units=64, return_sequences=False),
        Dropout(0.3),
        
        # Fully Connected Part
        Dense(32, activation='relu'),
        Dropout(0.2),
        Dense(1, activation='sigmoid')
    ])
    
    model.compile(optimizer='adam', 
                  loss='binary_crossentropy', 
                  metrics=['accuracy', tf.keras.metrics.Recall(), tf.keras.metrics.Precision()])
    
    return model

if __name__ == "__main__":
    # Example for SisFall: 2 seconds @ 200Hz = 400 timesteps, 6 channels
    model = create_cnn_gru_model((400,6))
    model.summary()
