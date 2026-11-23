import serial
import time

class SCPIBench:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0):
        print(f"Connecting to SCPI bench on {port}...")
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
        time.sleep(0.5)  # Пауза для инициализации CDC
        self.clear_buffer()

    def clear_buffer(self):
        self.ser.reset_input_buffer()

    def send(self, command: str):
        full_cmd = f"{command}\n".encode("utf-8")
        self.ser.write(full_cmd)
        time.sleep(0.05)

    def query(self, command: str) -> str:
        self.clear_buffer()
        full_cmd = f"{command}\n".encode("utf-8")
        self.ser.write(full_cmd)
        time.sleep(0.05)
        response = self.ser.readline().decode("utf-8", errors="replace")
        return response.strip()

    def close(self):
        if self.ser.is_open:
            self.ser.close()
            print("SCPI bench connection closed.")