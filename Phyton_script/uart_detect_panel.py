import asyncio
import time
from nicegui import ui
import serial
from ports_panel import get_available_ports


def build_uart_detect_panel(ports_refs):
    """Строит блок автоопределения портов UART через перебор MUX и управление JTAG"""
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        ui.label('Автоопределение и калибровка портов UART').classes(
            'text-sm font-bold text-yellow-300 text-center w-full mb-1.5'
        )

        port_labels_box = ui.row().classes('w-full gap-3 items-center justify-center my-2')

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
                f'[SHELL ACK TIMEOUT]: Не дождались "{target_ack}". Буфер: {repr(accum)}'
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
                ui.notify('Ошибка/Таймаут OUTP:MUX EN_USB_UART, OFF', type='negative')
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

            ui.notify('Ожидание отключения старых портов в ОС (2 сек)...', type='info')
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

            ui.notify('Ожидание подключения 4 новых COM-портов в Windows...', type='info')
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
                    f'Найдено только {len(new_ports)} из 4 портов ({", ".join(new_ports)}).',
                    type='negative',
                )
                btn_start_detect.enable()
                return

            label_widgets = {}
            with port_labels_box:
                for p in new_ports:
                    with ui.card().classes(
                        'bg-slate-700 py-1.5 px-3 rounded text-center border border-slate-600 min-w-[130px]'
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
                        f'Предупреждение: OUTP:MUX:UART {channel_num} не подтвержден',
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
                    print(f'[DETECT TEST]: Шлем в {candidate_p} -> MUX канал {channel_num}')

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
                            f'[DETECT CHECK]: Получены данные от {candidate_p}: {repr(data_str)}'
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
                        lbl_w.classes(remove='text-amber-400', add='text-emerald-400 font-bold')
                    ui.notify(f'Успех! {matched_port} -> Порт {channel_num}', type='positive')
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
                'bg-emerald-600 hover:bg-emerald-500 text-white font-bold text-xs py-2 px-6 rounded shadow'
            )

        # --- УПРАВЛЕНИЕ JTAG (SCPI OUTPut:MUX JTAG_MUX_EN + OUTPut:MUX:JTAG 1..5) ---
        with ui.row().classes(
            'w-full justify-center gap-6 items-center mt-3 pt-2 border-t border-slate-700'
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
            ).props('dark outlined dense').classes('w-36 bg-slate-700 text-xs text-white rounded')