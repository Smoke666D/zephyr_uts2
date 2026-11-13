import asyncio
import os
import time
import serial
from nicegui import ui

from logger import start_logger, stop_logger
from oscilloscope import build_oscilloscope_panel, refresh_logs_ui
from ports_panel import build_ports_panel, get_available_ports
from system_panel import build_system_panel, read_all_uids_async
from power_panel import build_power_panel

# Светодиоды SCPI подсистемы DISPlay:LED
SCPI_LEDS = [
    {'target': 'RED', 'name': 'Красный'},
    {'target': 'GRE', 'name': 'Зеленый'},
    {'target': 'BLU', 'name': 'Синий'},
]

CURRENT_SENSOR_NAMES = [
    'BOARD LOW',
    'BOARD HIGH',
    'Питание метки №2',
    'Питание метки №1',
    'Питание стенда',
    'Линия 3.3В стенда',
    'Питание тестируемого узла',
    'Линия 5В стенда',
]

can_logs_dict = {}
lin_logs_dict = {}
raw_logs_dict = {}


def build_dashboard(ports_refs, ui_labels):
    tel_ref = ports_refs['tel']
    shell_ref = ports_refs['shell']
    system_callbacks = {}

    with ui.column().classes('w-full gap-2') as container:

        # ==========================================
        # 1. БЛОК УПРАВЛЕНИЯ ПОРТАМИ (ВЫНЕСЕН В ports_panel.py)
        # ==========================================
        async def on_scpi_ready():
            if 'read_all_uids' in system_callbacks:
                await system_callbacks['read_all_uids']()

        build_ports_panel(ports_refs, on_device_connected=on_scpi_ready)

        # ==========================================
        # 2. КАРТОЧКА: СИСТЕМА (ВЫНЕСЕНА В system_panel.py)
        # ==========================================
        system_callbacks['read_all_uids'] = lambda: read_all_uids_async(shell_ref)
        build_system_panel(ports_refs)

        # ==========================================
        # 3. УПРАВЛЕНИЕ ПИТАНИЕМ (ВЫНЕСЕНО В power_panel.py)
        # ==========================================
        build_power_panel(ports_refs)

        # ==========================================
        # 4. СВЕТОДИОДЫ (SCPI DISPlay:LED:STATe)
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            ui.label('Подсистема индикации (DISPlay:LED)').classes(
                'text-xs font-semibold text-yellow-300 mb-1'
            )

            def make_scpi_led_handler(target_name, led_label):
                def handler(e):
                    state_val = 'ON' if e.value else 'OFF'
                    cmd = f'DISP:LED:STAT {target_name}, {state_val}\r\n'
                    curr_shell = shell_ref.get('ser')
                    if curr_shell and curr_shell.is_open:
                        curr_shell.write(cmd.encode('utf-8'))
                        ui.notify(f'[{led_label}] SCPI: {cmd.strip()}', type='info')
                    else:
                        ui.notify('SCPI порт закрыт!', type='warning')

                return handler

            with ui.row().classes('w-full gap-6 items-center px-1'):
                for item in SCPI_LEDS:
                    ui.checkbox(
                        item['name'],
                        value=False,
                        on_change=make_scpi_led_handler(item['target'], item['name']),
                    ).classes('text-white text-xs')

        # ==========================================
        # 5. ДИСКРЕТНЫЕ ДРАЙВЕРЫ (SCPI OUTPut:DOUT 1–18)
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            ui.label('Управление дискретными драйверами (OUTPut:DOUT 1–18)').classes(
                'text-sm font-bold text-yellow-300 text-center w-full mb-2 tracking-wide'
            )

            def on_dout_change(ch_num, select_element):
                state = select_element.value
                cmd = f'OUTP:DOUT {ch_num}, {state}\r\n'
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                    curr_shell.write(cmd.encode('utf-8'))
                    ui.notify(f'[DOUT {ch_num}] SCPI: {cmd.strip()}', type='info')
                else:
                    ui.notify('SCPI порт закрыт!', type='warning')

            with ui.grid(columns=6).classes('w-full gap-1'):
                for i in range(1, 19):
                    with ui.card().classes(
                        'bg-slate-700 p-1 flex-row items-center justify-between rounded'
                    ):
                        ui.label(f'DOUT {i}').classes('text-[11px] text-white font-bold')

                        if i <= 16:
                            options_list = ['IN', 'LOW', 'HIGH', 'SHORT']
                        else:
                            options_list = ['IN', 'LOW', 'HIGH']

                        dout_select = ui.select(
                            options=options_list, value='IN'
                        ).props('dark outlined dense').classes(
                            'w-28 bg-slate-800 text-[10px] text-white'
                        )
                        dout_select.on(
                            'update:model-value',
                            lambda e, c=i, s=dout_select: on_dout_change(c, s),
                        )

        # ==========================================
        # 6. ЦАП (SOURce:DAC) И ПОТЕНЦИОМЕТРЫ (SOURce:RESistance)
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            ui.label('Управление ЦАП (SOURce:DAC) и потенциометрами (SOURce:RESistance)').classes(
                'text-xs font-semibold text-yellow-300 mb-1.5'
            )
            with ui.row().classes('w-full gap-4 items-center'):
                # ЦАП: 12-битный код от 0 до 4095
                with ui.row().classes('items-center gap-1.5 flex-1'):
                    ui.label('ЦАП:').classes('text-xs text-white font-medium')
                    dac_input = ui.input(placeholder='0 - 4095', value='4095').props(
                        'dark outlined dense type=number min=0 max=4095 step=1 input-class="text-right"'
                    ).classes('w-28 bg-slate-700 text-xs text-white rounded')
                    ui.label('код (0..4095)').classes('text-xs text-slate-300 font-medium')

                    def send_dac():
                        try:
                            val = int(dac_input.value)
                            if 0 <= val <= 4095:
                                cmd = f'SOUR:DAC {val}\r\n'
                                curr_shell = shell_ref.get('ser')
                                if curr_shell and curr_shell.is_open:
                                    curr_shell.write(cmd.encode('utf-8'))
                                    ui.notify(f'[ЦАП] SCPI: {cmd.strip()}', type='positive')
                                else:
                                    ui.notify('SCPI порт закрыт!', type='warning')
                            else:
                                ui.notify(
                                    'Код ЦАП должен быть в диапазоне от 0 до 4095!', type='warning'
                                )
                        except ValueError:
                            ui.notify('Введите целое число для кода ЦАП (0-4095)', type='negative')

                    dac_input.on('keydown.enter', send_dac)
                    ui.button('Set DAC', on_click=send_dac).classes(
                        'bg-sky-600 text-white text-xs py-1 px-2'
                    )

                # Потенциометр Канал 1: 8-битный код от 0 до 255
                with ui.row().classes('items-center gap-1.5'):
                    ui.label('Резистор 1:').classes('text-xs text-white font-medium')
                    res1_input = ui.input(placeholder='0 - 255', value='0').props(
                        'dark outlined dense type=number min=0 max=255 step=1 input-class="text-right"'
                    ).classes('w-20 bg-slate-700 text-xs text-white rounded')
                    ui.label('код (0..255)').classes('text-xs text-slate-300 font-medium')

                    def send_res1():
                        try:
                            val = int(res1_input.value)
                            if 0 <= val <= 255:
                                cmd = f'SOUR:RES 1, {val}\r\n'
                                curr_shell = shell_ref.get('ser')
                                if curr_shell and curr_shell.is_open:
                                    curr_shell.write(cmd.encode('utf-8'))
                                    ui.notify(
                                        f'[Резистор 1] SCPI: {cmd.strip()}', type='positive'
                                    )
                                else:
                                    ui.notify('SCPI порт закрыт!', type='warning')
                            else:
                                ui.notify(
                                    'Значение Резистора 1 должно быть от 0 до 255!',
                                    type='warning',
                                )
                        except ValueError:
                            ui.notify(
                                'Введите целое число для Резистора 1 (0-255)', type='negative'
                            )

                    res1_input.on('keydown.enter', send_res1)
                    ui.button('Set R1', on_click=send_res1).classes(
                        'bg-emerald-600 text-white text-xs py-1 px-2'
                    )

                # Потенциометр Канал 2: 8-битный код от 0 до 255
                with ui.row().classes('items-center gap-1.5'):
                    ui.label('Резистор 2:').classes('text-xs text-white font-medium')
                    res2_input = ui.input(placeholder='0 - 255', value='0').props(
                        'dark outlined dense type=number min=0 max=255 step=1 input-class="text-right"'
                    ).classes('w-20 bg-slate-700 text-xs text-white rounded')
                    ui.label('код (0..255)').classes('text-xs text-slate-300 font-medium')

                    def send_res2():
                        try:
                            val = int(res2_input.value)
                            if 0 <= val <= 255:
                                cmd = f'SOUR:RES 2, {val}\r\n'
                                curr_shell = shell_ref.get('ser')
                                if curr_shell and curr_shell.is_open:
                                    curr_shell.write(cmd.encode('utf-8'))
                                    ui.notify(
                                        f'[Резистор 2] SCPI: {cmd.strip()}', type='positive'
                                    )
                                else:
                                    ui.notify('SCPI порт закрыт!', type='warning')
                            else:
                                ui.notify(
                                    'Значение Резистора 2 должно быть от 0 до 255!',
                                    type='warning',
                                )
                        except ValueError:
                            ui.notify(
                                'Введите целое число для Резистора 2 (0-255)', type='negative'
                            )

                    res2_input.on('keydown.enter', send_res2)
                    ui.button('Set R2', on_click=send_res2).classes(
                        'bg-emerald-600 text-white text-xs py-1 px-2'
                    )

        # ==========================================
        # 7. ДАШБОРД ДАТЧИКОВ
        # ==========================================
        def create_metric_card(title, key, unit=''):
            with ui.card().classes(
                'bg-slate-700 py-3 px-3 flex-1 min-w-[130px] rounded flex flex-row justify-between items-center cursor-default'
            ):
                ui.label(title).classes(
                    'text-xs text-slate-300 font-medium leading-none m-0'
                )
                with ui.row().classes('items-center gap-1 m-0'):
                    lbl = ui.label('0.00').classes(
                        'text-base font-mono text-emerald-400 font-bold leading-none m-0'
                    )
                    if unit:
                        ui.label(unit).classes(
                            'text-xs text-slate-400 font-medium leading-none m-0'
                        )
                ui_labels[key] = lbl

        with ui.column().classes('w-full gap-2 cursor-grab'):
            ui.label('Дашборд датчиков').classes('text-sm font-bold text-sky-300')
            with ui.row().classes('w-full gap-1'):
                create_metric_card('RF Power', 'rf_power', unit='дБ')
                create_metric_card('NTC', 'ntc')
                create_metric_card('Ext VSense', 'ext_vsense')
                create_metric_card('Brd Detect', 'brd_detect')
                create_metric_card('Температура МУ', 'temp_gnd', unit='°C')
                create_metric_card('Температура ИУ', 'temp_vdd', unit='°C')
                create_metric_card('Освещенность (DA22)', 'light_gnd', unit='лк')
                create_metric_card('Освещенность (DA23)', 'light_vdd', unit='лк')

            ui.label('Environment Voltages').classes(
                'text-xs font-semibold text-slate-400 mt-1'
            )
            with ui.row().classes('w-full gap-1'):
                for idx, name in enumerate([
                    'P3V3',
                    'P5V0',
                    'VIN',
                    'VDOUT1',
                    'P40V',
                    'USB',
                    'VDOUT2',
                    'VDOUT3',
                ]):
                    create_metric_card(name, f'env_{idx}', unit='В')

            ui.label('AO Channels (1-18)').classes(
                'text-xs font-semibold text-slate-400 mt-1'
            )
            with ui.grid(columns=6).classes('w-full gap-1'):
                for i in range(18):
                    create_metric_card(f'AO {i+1}', f'ao_{i}', unit='В')

            ui.label('AIN VSense (1-6)').classes(
                'text-xs font-semibold text-slate-400 mt-1'
            )
            with ui.grid(columns=6).classes('w-full gap-1'):
                for i in range(6):
                    create_metric_card(f'VSense {i+1}', f'vsense_{i}', unit='В')

        # ==========================================
        # 8. ДАТЧИКИ ТОКА, НАПРЯЖЕНИЯ И ЭНЕРГИИ (1-8)
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            with ui.row().classes('w-full justify-between items-center mb-1'):
                ui.label('Панель датчиков тока, напряжения и энергии').classes(
                    'text-xs font-semibold text-yellow-300'
                )

            def make_energy_reset_handler(d_idx, d_name):
                def handler():
                    cmd = f'energy_reset {d_idx}\r\n'
                    curr_shell = shell_ref.get('ser')
                    if curr_shell and curr_shell.is_open:
                        curr_shell.write(cmd.encode('utf-8'))
                        ui.notify(f'[{d_name}] Сброс энергии отправлен', type='info')
                    else:
                        ui.notify('SCPI порт закрыт!', type='warning')

                return handler

            with ui.grid(columns=4).classes('w-full gap-1.5'):
                for idx, sensor_name in enumerate(CURRENT_SENSOR_NAMES):
                    i = idx + 1
                    with ui.card().classes(
                        'bg-slate-700 py-1.5 px-2.5 rounded flex flex-col justify-between'
                    ):
                        with ui.row().classes('w-full justify-between items-center mb-1'):
                            ui.label(sensor_name).classes(
                                'text-[11px] text-yellow-300 font-bold leading-none truncate max-w-[170px]'
                            )
                            ui.button(
                                'Сброс', on_click=make_energy_reset_handler(i, sensor_name)
                            ).classes('bg-red-700 text-white text-[9px] py-0.5 px-1.5 rounded')

                        with ui.row().classes('w-full justify-between items-center mt-0.5'):
                            ui.label('U:').classes('text-[10px] text-slate-300 leading-none')
                            with ui.row().classes('items-center gap-1'):
                                ui_labels[f'curr_v_{i}'] = ui.label('0.00').classes(
                                    'text-xs font-mono text-emerald-400 font-bold leading-none'
                                )
                                ui.label('В').classes('text-[10px] text-slate-400 leading-none')

                        with ui.row().classes('w-full justify-between items-center mt-1'):
                            ui.label('I:').classes('text-[10px] text-slate-300 leading-none')
                            with ui.row().classes('items-center gap-1'):
                                ui_labels[f'curr_i_{i}'] = ui.label('0.0000').classes(
                                    'text-xs font-mono text-sky-400 font-bold leading-none'
                                )
                                ui.label('А').classes('text-[10px] text-slate-400 leading-none')

                        with ui.row().classes('w-full justify-between items-center mt-1'):
                            ui.label('E:').classes('text-[10px] text-slate-300 leading-none')
                            with ui.row().classes('items-center gap-1'):
                                ui_labels[f'curr_e_{i}'] = ui.label('0.000000').classes(
                                    'text-xs font-mono text-amber-400 font-bold leading-none'
                                )
                                ui.label('Дж').classes(
                                    'text-[10px] text-slate-400 leading-none'
                                )

        # ==========================================
        # 9. ИНТЕРФЕЙСЫ LIN (1-4) (SCPI OUTPut:LINPulldown)
        # ==========================================
        ui.label('Интерфейсы LIN (1-4)').classes(
            'text-sm font-bold text-sky-300 mt-3'
        )

        def make_lin_block(l_idx):
            with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
                with ui.row().classes('w-full justify-between items-center mb-1'):
                    ui.label(f'LIN Интерфейс {l_idx}').classes(
                        'text-xs font-semibold text-yellow-300'
                    )

                    def handle_pull_down(e, idx=l_idx):
                        state = 'ON' if e.value else 'OFF'
                        channel_name = f'LIN{idx}'
                        cmd = f'OUTP:LINP {channel_name}, {state}\r\n'
                        curr_shell = shell_ref.get('ser')
                        if curr_shell and curr_shell.is_open:
                            curr_shell.write(cmd.encode('utf-8'))
                            ui.notify(
                                f'[{channel_name} PULL_DOWN] SCPI: {cmd.strip()}',
                                type='info',
                            )
                        else:
                            ui.notify('SCPI порт закрыт!', type='warning')

                    ui.checkbox(
                        'PULL_DOWN', value=False, on_change=handle_pull_down
                    ).classes('text-white text-xs')

                    with ui.row().classes('items-center gap-1'):
                        ui.label('U:').classes('text-[11px] text-slate-300')
                        lin_v_lbl = ui.label('0.00').classes(
                            'text-xs font-mono text-emerald-400 font-bold'
                        )
                        ui.label('В').classes('text-[11px] text-slate-400')
                        ui_labels[f'lin_v_{l_idx}'] = lin_v_lbl

                ui.label('Входящие пакеты:').classes(
                    'text-[10px] text-slate-400 mt-1 mb-0.5'
                )
                lin_log = ui.log(max_lines=20).classes(
                    'w-full h-16 bg-slate-900 text-cyan-400 font-mono text-[11px] p-1'
                    ' rounded'
                )
                lin_log.push(f'LIN{l_idx} готов...')
                lin_logs_dict[l_idx] = lin_log

                with ui.row().classes('w-full gap-2 items-center mt-1.5'):
                    lin_data_input = ui.input(
                        placeholder='Данные через пробел (например: 10 20 AA)'
                    ).props('dark outlined dense input-class="text-xs"').classes(
                        'flex-1 bg-slate-700 text-xs text-white rounded'
                    )

                    def send_lin_msg(idx=l_idx, data_inp=lin_data_input, log=lin_log):
                        ldata = data_inp.value.strip()
                        if not ldata:
                            ui.notify(
                                f'LIN{idx}: Введите данные для отправки!', type='warning'
                            )
                            return

                        cmd = f'lin_send {idx} {ldata}\r\n'
                        curr_shell = shell_ref.get('ser')

                        if curr_shell and curr_shell.is_open:
                            curr_shell.write(cmd.encode('utf-8'))
                            log.push(f'TX -> {ldata}')
                            data_inp.set_value('')
                        else:
                            ui.notify('SCPI порт закрыт!', type='warning')

                    lin_data_input.on('keydown.enter', send_lin_msg)
                    ui.button('Отправить', on_click=send_lin_msg).classes(
                        'bg-sky-600 text-white text-xs py-1 px-3 font-bold'
                    )

        for lin_idx in range(1, 5):
            make_lin_block(lin_idx)

        # ==========================================
        # 10. CAN ИНТЕРФЕЙСЫ (1-3)
        # ==========================================
        ui.label('Интерфейсы CAN (1-3)').classes(
            'text-sm font-bold text-sky-300 mt-3'
        )
        for can_idx in range(1, 4):
            with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
                ui.label(f'CAN Интерфейс {can_idx}').classes(
                    'text-xs font-semibold text-yellow-300 mb-1'
                )

                with ui.row().classes('w-full gap-2 items-center'):
                    can_speed_data = ui.select(
                        options=['1000', '500', '250', '125'],
                        value='1000',
                        label='Скорость Data (кб/с)',
                    ).props('dark outlined dense').classes(
                        'w-36 bg-slate-700 text-xs text-white rounded'
                    )
                    can_speed_data.disable()

                    def update_can_mode(e, ds=can_speed_data):
                        if e.value == 'CANFD':
                            ds.enable()
                        else:
                            ds.disable()
                            ds.set_value('1000')

                    can_mode = ui.select(
                        options=['Classic', 'CANFD'],
                        value='Classic',
                        label='Режим',
                        on_change=update_can_mode,
                    ).props('dark outlined dense').classes(
                        'w-28 bg-slate-700 text-xs text-white rounded'
                    )

                    can_speed_nom = ui.select(
                        options=['1000', '500', '250', '125'],
                        value='500',
                        label='Скорость (кб/с)',
                    ).props('dark outlined dense').classes(
                        'w-32 bg-slate-700 text-xs text-white rounded'
                    )

                ui.label('Входящие сообщения:').classes(
                    'text-[10px] text-slate-400 mt-1 mb-0.5'
                )
                can_log = ui.log(max_lines=30).classes(
                    'w-full h-20 bg-slate-900 text-green-400 font-mono text-[11px] p-1'
                    ' rounded'
                )
                can_log.push(f'CAN{can_idx} готов к работе...')
                can_logs_dict[can_idx] = can_log

                with ui.row().classes('w-full gap-2 items-center mt-1.5'):
                    can_id_input = ui.input(placeholder='ID (например, 123)').props(
                        'dark outlined dense input-class="text-xs"'
                    ).classes('w-36 bg-slate-700 text-xs text-white rounded')

                    can_data_input = ui.input(
                        placeholder='Данные через пробел (например: 11 22 AA)'
                    ).props('dark outlined dense input-class="text-xs"').classes(
                        'flex-1 bg-slate-700 text-xs text-white rounded'
                    )

                    def send_can_msg(
                        c_idx=can_idx,
                        id_inp=can_id_input,
                        data_inp=can_data_input,
                        log=can_log,
                    ):
                        cid = id_inp.value.strip()
                        cdata = data_inp.value.strip()
                        if not cid:
                            ui.notify(f'CAN{c_idx}: Укажите ID сообщения!', type='warning')
                            return

                        cmd = f'can send {c_idx} {cid} {cdata}\r\n'
                        curr_shell = shell_ref.get('ser')

                        if curr_shell and curr_shell.is_open:
                            curr_shell.write(cmd.encode('utf-8'))
                            log.push(f'TX -> ID: {cid} | DATA: {cdata}')
                            id_inp.set_value('')
                            data_inp.set_value('')
                        else:
                            ui.notify(
                                'SCPI порт закрыт! Невозможно отправить пакет',
                                type='warning',
                            )

                    can_id_input.on('keydown.enter', send_can_msg)
                    can_data_input.on('keydown.enter', send_can_msg)
                    ui.button('Отправить', on_click=send_can_msg).classes(
                        'bg-sky-600 text-white text-xs py-1 px-3 font-bold'
                    )

        # ==========================================
        # 11. АВТООПРЕДЕЛЕНИЕ И КАЛИБРОВКА ПОРТОВ UART (SCPI SENS:UART)
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            ui.label('Автоопределение и калибровка портов UART').classes(
                'text-sm font-bold text-yellow-300 text-center w-full mb-1.5'
            )

            port_labels_box = ui.row().classes(
                'w-full gap-3 items-center justify-center my-2'
            )

            async def shell_cmd_wait(cmd_bytes, target_ack, timeout=4.0):
                curr_shell = shell_ref.get('ser')
                if not curr_shell or not curr_shell.is_open:
                    return False, ''

                curr_shell.reset_input_buffer()
                curr_shell.write(cmd_bytes)
                print(f'[SHELL TX]: {cmd_bytes.strip().decode(errors="ignore")}')

                start_t = time.time()
                accum = ''
                clean_target = target_ack.strip().lower()

                while time.time() - start_t < timeout:
                    if curr_shell.in_waiting > 0:
                        raw = curr_shell.read(curr_shell.in_waiting)
                        accum += raw.decode('utf-8', errors='ignore')
                        if clean_target in accum.lower():
                            print(f'[SHELL ACK OK]: Найдено "{target_ack}"')
                            return True, accum
                    await asyncio.sleep(0.04)

                print(
                    f'[SHELL ACK TIMEOUT]: Не дождались "{target_ack}". Буфер:'
                    f' {repr(accum)}'
                )
                return False, accum

            async def scpi_query(query_bytes, timeout=0.6):
                curr_shell = shell_ref.get('ser')
                if not curr_shell or not curr_shell.is_open:
                    return ''
                curr_shell.reset_input_buffer()
                curr_shell.write(query_bytes)
                query_str = query_bytes.strip().decode('utf-8', errors='ignore')

                start_t = time.time()
                accum = ''
                while time.time() - start_t < timeout:
                    if curr_shell.in_waiting > 0:
                        accum += curr_shell.read(curr_shell.in_waiting).decode(
                            'utf-8', errors='ignore'
                        )
                        lines = [
                            line.strip() for line in accum.splitlines() if line.strip()
                        ]
                        filtered = [l for l in lines if l.lower() != query_str.lower()]
                        if filtered:
                            return filtered[-1]
                    await asyncio.sleep(0.02)

                lines = [line.strip() for line in accum.splitlines() if line.strip()]
                filtered = [l for l in lines if l.lower() != query_str.lower()]
                return filtered[-1] if filtered else ''

            async def run_full_detect_procedure():
                curr_shell = shell_ref.get('ser')
                if not curr_shell or not curr_shell.is_open:
                    ui.notify('SCPI порт закрыт! Сначала откройте порт.', type='warning')
                    return

                btn_start_detect.disable()
                port_labels_box.clear()

                # Шаг 1: Выключение EN_USB_UART через SCPI
                ui.notify('Шаг 1: Выключение EN_USB_UART...', type='info')
                ok, _ = await shell_cmd_wait(
                    b'OUTP:MUX EN_USB_UART, OFF;*OPC?\r\n', '1'
                )
                if not ok:
                    ui.notify(
                        'Ошибка/Таймаут OUTP:MUX EN_USB_UART, OFF', type='negative'
                    )
                    btn_start_detect.enable()
                    return

                # Шаг 2: Выключение UART_FB_EN через SCPI
                ui.notify('Шаг 2: Выключение UART_FB_EN...', type='info')
                ok, _ = await shell_cmd_wait(
                    b'OUTP:MUX UART_FB_EN, OFF;*OPC?\r\n', '1'
                )
                if not ok:
                    ui.notify('Ошибка/Таймаут OUTP:MUX UART_FB_EN, OFF', type='negative')
                    btn_start_detect.enable()
                    return

                # Шаг 3: Переключение мультиплексора UART на канал 1 по SCPI
                ui.notify('Шаг 3: Переключение UART MUX на канал 1...', type='info')
                ok, _ = await shell_cmd_wait(b'OUTP:MUX:UART 1;*OPC?\r\n', '1')
                if not ok:
                    ui.notify('Ошибка/Таймаут OUTP:MUX:UART 1', type='negative')
                    btn_start_detect.enable()
                    return

                ui.notify(
                    'Ожидание отключения старых портов в ОС (2 сек)...', type='info'
                )
                await asyncio.sleep(2.0)

                base_ports = set(get_available_ports())
                print(f'[DETECT] Базовые порты системы (без хаба): {base_ports}')

                # Шаг 4: Включение EN_USB_UART через SCPI
                ui.notify('Шаг 4: Включение EN_USB_UART...', type='info')
                ok, _ = await shell_cmd_wait(b'OUTP:MUX EN_USB_UART, ON;*OPC?\r\n', '1')
                if not ok:
                    ui.notify('Ошибка/Таймаут OUTP:MUX EN_USB_UART, ON', type='negative')
                    btn_start_detect.enable()
                    return

                # Шаг 5: Включение UART_FB_EN через SCPI
                ui.notify('Шаг 5: Включение UART_FB_EN...', type='info')
                ok, _ = await shell_cmd_wait(b'OUTP:MUX UART_FB_EN, ON;*OPC?\r\n', '1')
                if not ok:
                    ui.notify('Ошибка/Таймаут OUTP:MUX UART_FB_EN, ON', type='negative')
                    btn_start_detect.enable()
                    return

                # Настройка приемника стенда: 115200 бод и очистка буфера
                curr_shell.write(b'SENS:UART:BAUD 115200\r\n')
                curr_shell.write(b'SENS:UART:CLE\r\n')
                await asyncio.sleep(0.1)

                ui.notify(
                    'Ожидание подключения 4 новых COM-портов в Windows...', type='info'
                )
                scan_start = time.time()
                new_ports = []

                while time.time() - scan_start < 8.0:
                    curr_ports = set(get_available_ports())
                    diff = sorted(list(curr_ports - base_ports))
                    if len(diff) >= 4:
                        new_ports = diff
                        break
                    await asyncio.sleep(0.5)

                if len(new_ports) < 4:
                    curr_ports = set(get_available_ports())
                    new_ports = sorted(list(curr_ports - base_ports))

                print(f'[DETECT] Новые порты хаба: {new_ports}')
                if len(new_ports) < 4:
                    ui.notify(
                        f'Найдено только {len(new_ports)} из 4 портов'
                        f' ({", ".join(new_ports)}).',
                        type='negative',
                    )
                    btn_start_detect.enable()
                    return

                label_widgets = {}
                with port_labels_box:
                    for p in new_ports:
                        with ui.card().classes(
                            'bg-slate-700 py-1.5 px-3 rounded text-center border'
                            ' border-slate-600 min-w-[130px]'
                        ):
                            ui.label(p).classes('text-xs font-mono font-bold text-sky-300')
                            lbl = ui.label('Определение...').classes(
                                'text-[11px] font-mono text-amber-400'
                            )
                            label_widgets[p] = lbl

                open_handles = {}
                for p in new_ports:
                    try:
                        s = serial.Serial(p, baudrate=115200, timeout=0.05)
                        open_handles[p] = s
                    except Exception as e:
                        print(f'[DETECT ERROR]: Не удалось открыть {p}: {e}')

                remaining_ports = list(new_ports)
                identified_mapping = {}

                # --- ЭТАП 4: Последовательное определение Портов 1..4 через SENSe:UART ---
                for channel_num in range(1, 5):
                    ui.notify(f'Поиск физического Порта {channel_num}...', type='info')

                    cmd = f'OUTP:MUX:UART {channel_num};*OPC?\r\n'.encode()
                    ok, _ = await shell_cmd_wait(cmd, '1', timeout=3.0)
                    if not ok:
                        ui.notify(
                            'Предупреждение: OUTP:MUX:UART'
                            f' {channel_num} не подтвержден',
                            type='warning',
                        )
                    await asyncio.sleep(0.1)

                    matched_port = None
                    test_payload = b'TEST\r\n'

                    for candidate_p in list(remaining_ports):
                        handle = open_handles.get(candidate_p)
                        if not handle or not handle.is_open:
                            continue

                        curr_shell.write(b'SENS:UART:CLE\r\n')
                        await asyncio.sleep(0.04)

                        handle.reset_input_buffer()
                        handle.reset_output_buffer()
                        handle.write(test_payload)
                        print(
                            f'[DETECT TEST]: Шлем в {candidate_p} -> MUX канал'
                            f' {channel_num}'
                        )

                        got_bytes = False
                        poll_start = time.time()
                        while time.time() - poll_start < 0.5:
                            count_str = await scpi_query(b'SENS:UART:COUN?\r\n', timeout=0.15)
                            try:
                                cnt = int(count_str)
                                if cnt > 0:
                                    got_bytes = True
                                    break
                            except ValueError:
                                pass
                            await asyncio.sleep(0.04)

                        if got_bytes:
                            data_str = await scpi_query(b'SENS:UART:DATA?\r\n', timeout=0.25)
                            print(
                                f'[DETECT CHECK]: Получены данные от {candidate_p}:'
                                f' {repr(data_str)}'
                            )
                            if 'TEST' in data_str:
                                matched_port = candidate_p
                                break

                    if matched_port:
                        identified_mapping[channel_num] = matched_port
                        remaining_ports.remove(matched_port)
                        lbl_w = label_widgets.get(matched_port)
                        if lbl_w:
                            lbl_w.set_text(f'Порт {channel_num}')
                            lbl_w.classes(
                                remove='text-amber-400', add='text-emerald-400 font-bold'
                            )
                        ui.notify(
                            f'Успех! {matched_port} -> Порт {channel_num}', type='positive'
                        )
                    else:
                        ui.notify(
                            f'Не удалось определить порт для канала {channel_num}',
                            type='warning',
                        )

                for p, h in open_handles.items():
                    try:
                        h.close()
                    except Exception:
                        pass

                btn_start_detect.enable()
                ui.notify('Процедура автоопределения завершена!', type='info')

            with ui.row().classes('w-full justify-center gap-4 items-center mt-1'):
                btn_start_detect = ui.button(
                    'СТАРТ', on_click=run_full_detect_procedure
                ).classes(
                    'bg-emerald-600 hover:bg-emerald-500 text-white font-bold text-xs'
                    ' py-2 px-6 rounded shadow'
                )

            # --- УПРАВЛЕНИЕ JTAG (SCPI OUTPut:MUX JTAG_MUX_EN + OUTPut:MUX:JTAG 1..5) ---
            with ui.row().classes(
                'w-full justify-center gap-6 items-center mt-3 pt-2 border-t'
                ' border-slate-700'
            ):

                def handle_jtag_enable(e):
                    state = 'ON' if e.value else 'OFF'
                    cmd = f'OUTP:MUX JTAG_MUX_EN, {state}\r\n'
                    curr_shell = shell_ref.get('ser')
                    if curr_shell and curr_shell.is_open:
                        curr_shell.write(cmd.encode('utf-8'))
                        ui.notify(f'[JTAG MUX EN] SCPI: {cmd.strip()}', type='info')
                    else:
                        ui.notify('SCPI порт закрыт!', type='warning')

                ui.checkbox(
                    'JTAG ENABLE', value=False, on_change=handle_jtag_enable
                ).classes('text-white text-xs font-semibold')

                def handle_jtag_mux(e):
                    mux_val = e.value
                    cmd = f'OUTP:MUX:JTAG {mux_val}\r\n'
                    curr_shell = shell_ref.get('ser')
                    if curr_shell and curr_shell.is_open:
                        curr_shell.write(cmd.encode('utf-8'))
                        ui.notify(f'[JTAG MUX] SCPI: {cmd.strip()}', type='info')
                    else:
                        ui.notify('SCPI порт закрыт!', type='warning')

                jtag_options = {
                    1: 'JTAG 1',
                    2: 'JTAG 2',
                    3: 'JTAG 3',
                    4: 'JTAG 4',
                    5: 'JTAG 5',
                }
                ui.select(
                    options=jtag_options,
                    value=1,
                    label='Канал JTAG',
                    on_change=handle_jtag_mux,
                ).props('dark outlined dense').classes(
                    'w-36 bg-slate-700 text-xs text-white rounded'
                )

        # ==========================================
        # 12. БЛОК ЛОГИРОВАНИЯ В CSV/EXCEL С СЕКУНДОМЕРОМ
        # ==========================================
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            ui.label('Логирование данных в файл (CSV/Excel)').classes(
                'text-xs font-semibold text-sky-400 mb-1'
            )

            with ui.row().classes('gap-3 items-center'):
                timer_start_ts = 0.0
                timer_active = False

                timer_lbl = ui.label('00:00.000').classes(
                    'text-sm font-mono text-emerald-400 font-bold bg-slate-900 py-1'
                    ' px-2.5 rounded border border-slate-700'
                )

                def update_timer_ui():
                    if timer_active:
                        elapsed = time.time() - timer_start_ts
                        mins = int(elapsed // 60)
                        secs = int(elapsed % 60)
                        millis = int((elapsed - int(elapsed)) * 1000)
                        timer_lbl.set_text(f'{mins:02d}:{secs:02d}.{millis:03d}')

                log_clock_timer = ui.timer(0.05, update_timer_ui, active=False)

                def toggle_log():
                    nonlocal timer_start_ts, timer_active
                    if btn_log.text.startswith('Старт'):
                        start_logger()
                        timer_start_ts = time.time()
                        timer_active = True
                        log_clock_timer.activate()
                        timer_lbl.set_text('00:00.000')

                        btn_log.text = 'Стоп записи'
                        btn_log.classes(remove='bg-emerald-600', add='bg-red-600')
                        ui.notify('Запись лога начата', type='positive')
                    else:
                        stop_logger()
                        timer_active = False
                        log_clock_timer.deactivate()
                        elapsed = time.time() - timer_start_ts
                        mins = int(elapsed // 60)
                        secs = int(elapsed % 60)
                        millis = int((elapsed - int(elapsed)) * 1000)
                        timer_lbl.set_text(f'{mins:02d}:{secs:02d}.{millis:03d}')

                        btn_log.text = 'Старт записи лога'
                        btn_log.classes(remove='bg-red-600', add='bg-emerald-600')
                        ui.notify(
                            f'Запись завершена ({mins:02d}:{secs:02d}.{millis:03d})',
                            type='info',
                        )

                        ui.timer(0.2, refresh_logs_ui, once=True)

                btn_log = ui.button('Старт записи лога', on_click=toggle_log).classes(
                    'bg-emerald-600 text-white text-xs font-bold py-1 px-3'
                )

                ui.label('Формат: ММ:СС.мс (сохранение в logs/)').classes(
                    'text-[10px] text-slate-400'
                )

        # ==========================================
        # 13. ОСЦИЛЛОГРАФ (В самом конце)
        # ==========================================
        ui.label('Осциллограф').classes('text-sm font-bold text-sky-300 mt-3 mb-1')
        build_oscilloscope_panel()

    container.make_sortable()
    return can_logs_dict, lin_logs_dict, raw_logs_dict