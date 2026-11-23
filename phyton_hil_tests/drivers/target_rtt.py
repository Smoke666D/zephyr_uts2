import pylink
import time

class TargetRTT:
    def __init__(self, serial_no: int = None, target_device: str = "STM32F407VG"):
        print(f"Connecting to target via J-Link (Device: {target_device})...")
        self.jlink = pylink.JLink()
        if serial_no:
            self.jlink.open(serial_no=serial_no)
        else:
            self.jlink.open()
        
        self.jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        self.jlink.connect(target_device)
        
        # Запуск RTT
        self.jlink.rtt_start()
        time.sleep(0.2)
        print("J-Link & RTT connected successfully.")

    def send_cmd(self, command: str, channel: int = 0):
        data = list(bytearray(f"{command}\n", encoding="utf-8"))
        self.jlink.rtt_write(channel, data)

    def read_output(self, channel: int = 0, max_bytes: int = 1024) -> str:
        data = self.jlink.rtt_read(channel, max_bytes)
        if data:
            return "".join([chr(b) for b in data])
        return ""

    def close(self):
        try:
            self.jlink.rtt_stop()
            self.jlink.close()
            print("J-Link connection closed.")
        except:
            pass