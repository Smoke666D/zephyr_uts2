import pylink

jlink = pylink.JLink()
print("Opening J-Link...")
jlink.open()

print("Setting interface to SWD...")
jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)

# Попробуем подключиться к чипу
target_name = "STM32F413VG"
print(f"Connecting to target: {target_name}...")
try:
    jlink.connect(target_name)
    print("SUCCESS: Target connected!")
    
    print("Starting RTT...")
    jlink.rtt_start()
    data = jlink.rtt_read(0, 100)
    print("RTT Data read:", data)
    
except Exception as e:
    print(f"ERROR during connect/RTT: {e}")
finally:
    jlink.close()