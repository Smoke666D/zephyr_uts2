import time
import pytest
from drivers.scpi_bench import SCPIBench
from drivers.target_rtt import TargetRTT

def pytest_addoption(parser):
    parser.addoption("--scpi-port", action="store", default="COM11", help="USB CDC COM port")
    parser.addoption("--jlink-sn", action="store", default=None, type=int, help="J-Link SN")
    parser.addoption("--mcu-device", action="store", default="STM32F407VG", help="Target MCU")

@pytest.fixture(scope="session")
def bench(request):
    """Фикстура для SCPI стенда"""
    port = request.config.getoption("--scpi-port")
    instrument = SCPIBench(port=port)
    yield instrument
    instrument.close()

@pytest.fixture(scope="session")
def target(request, bench):
    """
    Фикстура тестового узла по RTT.
    Зависит от 'bench', чтобы сначала подать питание и включить JTAG,
    а уже потом пытаться подключиться через J-Link.
    """
    print("[HIL] Подготовка стенда: подаем питание и включаем JTAG...")
    
    # 1. Подаем питание на стенд
    bench.send("OUTP:POW:ALL 1")
    time.sleep(1.0) 
    # 2. Включаем JTAG в мультиплексоре
    bench.send("OUTP:MUX:JTAG 1")
    time.sleep(1.0)
    bench.send("OUTP:MUX JTAG_MUX_EN, ON")
    
    # Небольшая пауза на инициализацию железа и старт процессора
    time.sleep(1.0)
    
    # 3. Теперь, когда питание и линии активны, инициализируем J-Link / RTT
    sn = request.config.getoption("--jlink-sn")
    mcu = request.config.getoption("--mcu-device")
    
    print(f"[HIL] Подключение к целевому узлу по J-Link (MCU: {mcu})...")
    node = TargetRTT(serial_no=sn, target_device=mcu)
    
    yield node
    
    # Teardown после всех тестов: можно выключить питание
    try:
        bench.send("OUTP:POW:ALL 0")
        bench.send("OUTP:MUX:JTAG 0")
    except:
        pass
    
    node.close()