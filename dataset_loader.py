import os
import numpy as np

class SisFallLoader:
    def __init__(self, base_path, sampling_freq=200):
        self.base_path = base_path
        self.sampling_freq = sampling_freq
        
        # Sensor characteristics
        self.adxl345_res = 13
        self.adxl345_range = 16
        self.itg3200_res = 16
        self.itg3200_range = 2000
        self.mma8451_res = 14
        self.mma8451_range = 8

    def convert_accel(self, data, resolution, range_val):
        return (2 * range_val / (2**resolution)) * data

    def convert_gyro(self, data, resolution, range_val):
        return (2 * range_val / (2**resolution)) * data

    def load_file(self, file_path):
        """Loads a single SisFall file and converts bits to physical units."""
        try:
            # Read file, stripping trailing semicolon and splitting by comma
            with open(file_path, 'r') as f:
                content = f.read()
            
            # Clean content: remove semicolons and split into lines
            lines = content.strip().replace(';', '').split('\n')
            data = []
            for line in lines:
                if line.strip():
                    data.append([float(x) for x in line.split(',')])
            
            data = np.array(data)
            if data.shape[1] != 9:
                print(f"Warning: {file_path} has {data.shape[1]} columns instead of 9.")
                return None
            
            # Convert to physical units
            # Columns: 0-2: ADXL345, 3-5: ITG3200, 6-8: MMA8451
            data[:, 0:3] = self.convert_accel(data[:, 0:3], self.adxl345_res, self.adxl345_range)
            data[:, 3:6] = self.convert_gyro(data[:, 3:6], self.itg3200_res, self.itg3200_range)
            data[:, 6:9] = self.convert_accel(data[:, 6:9], self.mma8451_res, self.mma8451_range)
            
            return data
        except Exception as e:
            print(f"Error loading {file_path}: {e}")
            return None

    def segment_data(self, data, window_size_sec, overlap_pct=0.5):
        """Segments data into windows."""
        window_size = int(window_size_sec * self.sampling_freq)
        step_size = int(window_size * (1 - overlap_pct))
        
        windows = []
        for i in range(0, len(data) - window_size + 1, step_size):
            window = data[i : i + window_size]
            windows.append(window)
        
        return np.array(windows)

    def load_dataset(self, window_size_sec=2.0, overlap_pct=0.5, limit_files=None):
        """Walks through the directory and loads files into a dataset."""
        X = []
        y = []
        count = 0
        
        for root, dirs, files in os.walk(self.base_path):
            for file in files:
                if file.endswith('.txt') and '_' in file:
                    file_path = os.path.join(root, file)
                    is_fall = 1 if file.startswith('F') else 0
                    
                    data = self.load_file(file_path)
                    if data is not None:
                        windows = self.segment_data(data, window_size_sec, overlap_pct)
                        if len(windows) > 0:
                            X.append(windows)
                            y.extend([is_fall] * len(windows))
                    
                    count += 1
                    if limit_files and count >= limit_files:
                        break
            if limit_files and count >= limit_files:
                break
        
        if len(X) == 0:
            return None, None
            
        return np.vstack(X), np.array(y)

if __name__ == "__main__":
    # Example usage
    base_dir = r"c:\Users\jeyar\Antigravity\Fall_Detection\SisFall"
    loader = SisFallLoader(base_dir)
    
    print("Testing loader with a sample file...")
    sample_file = os.path.join(base_dir, "SA01", "F01_SA01_R01.txt")
    if os.path.exists(sample_file):
        data = loader.load_file(sample_file)
        print(f"Loaded {sample_file}")
        print(f"Shape: {data.shape}")
        print("First 5 rows (converted):")
        print(data[:5])
        
        print("\nTesting segmentation...")
        windows = loader.segment_data(data, window_size_sec=2.0)
        print(f"Number of windows: {len(windows)}")
        print(f"Window shape: {windows[0].shape}")
    else:
        print(f"Sample file not found: {sample_file}")

