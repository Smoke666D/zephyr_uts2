import asyncio
from nicegui import ui

from can_panel import build_can_panel
from current_panel import build_current_panel
from dac_res_panel import build_dac_res_panel
from dout_panel import build_dout_panel
from led_panel import build_led_panel
from lin_panel import build_lin_panel
from logging_panel import build_logging_panel
from oscilloscope import build_oscilloscope_panel
from ports_panel import build_ports_panel
from power_panel import build_power_panel
from sensors_panel import build_sensors_panel
from system_panel import build_system_panel, read_all_uids_async
from uart_detect_panel import build_uart_detect_panel
from iv_curve_panel import build_iv_curve_panel

can_logs_dict = {}
lin_logs_dict = {}
raw_logs_dict = {}


def build_dashboard(ports_refs, ui_labels):
    shell_ref = ports_refs['shell']
    system_callbacks = {}

    with ui.column().classes('w-full gap-2') as container:

        # 1. Порты (ports_panel.py)
        async def on_scpi_ready():
            if 'read_all_uids' in system_callbacks:
                await system_callbacks['read_all_uids']()

        build_ports_panel(ports_refs, on_device_connected=on_scpi_ready)

        # Питание OUTPut:POWer (power_panel.py)
        build_power_panel(ports_refs)

        #Автоопределение портов UART и JTAG (uart_detect_panel.py)
        build_uart_detect_panel(ports_refs)
        
        # Светодиоды DISPlay:LED:STATe (led_panel.py)
        build_led_panel(ports_refs)

        # ЦАП и потенциометры (dac_res_panel.py)
        build_dac_res_panel(ports_refs)

        # Дашборд датчиков (sensors_panel.py)
        build_sensors_panel(ui_labels)

        # Датчики тока, напряжения и мощности (current_panel.py)
        build_current_panel(ports_refs, ui_labels)
        
        # Логирование CSV с секундомером (logging_panel.py)
        build_logging_panel()

        #  Офлайн-осциллограф (oscilloscope.py)
        ui.label('Осциллограф').classes('text-sm font-bold text-sky-300 mt-3 mb-1')
        build_oscilloscope_panel()
        
        # Дискретные драйверы OUTPut:DOUT 1-18 (dout_panel.py)
        build_dout_panel(ports_refs)

        # 9. Шины LIN 1-4 (lin_panel.py)
        build_lin_panel(ports_refs, ui_labels, lin_logs_dict)

        # 10. Шины CAN 1-3 (can_panel.py)
        build_can_panel(ports_refs, can_logs_dict)

  
# ==========================================
        # 14. ГРАФИК ВОЛЬТ-АМПЕРНЫХ ХАРАКТЕРИСТИК (ВАХ)
        # ==========================================
        ui.label('Вольт-амперные характеристики (ВАХ)').classes(
            'text-sm font-bold text-sky-300 mt-3 mb-1'
        )
        build_iv_curve_panel(ports_refs)
        
        #Система: UID, SN, HW, USER (system_panel.py)
        system_callbacks['read_all_uids'] = lambda: read_all_uids_async(shell_ref)
        build_system_panel(ports_refs)

    container.make_sortable()
    return can_logs_dict, lin_logs_dict, raw_logs_dict