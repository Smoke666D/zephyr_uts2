import asyncio
import ctypes
import sys
import threading
import time
from nicegui import ui


def set_win_timer_res(enable=True):
    """Включает системный режим разрешения 1 мс в планировщике Windows"""
    if sys.platform == 'win32':
        try:
            if enable:
                ctypes.windll.winmm.timeBeginPeriod(1)
            else:
                ctypes.windll.winmm.timeEndPeriod(1)
        except Exception:
            pass


def build_dac_res_panel(ports_refs):
    """Строит панель управления ЦАП (12-бит) с потоковым спадом и потенциометрами (8-бит)"""
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        ui.label('Управление ЦАП (SOURce:DAC) и потенциометрами (SOURce:RESistance)').classes(
            'text-xs font-semibold text-yellow-300 mb-2'
        )

        # Состояние и синхронизация отдельного потока ЦАП
        last_dac_send_time = 0.0
        last_sent_dac_val = -1
        dac_updating_lock = False

        ramp_thread = None
        ramp_stop_event = threading.Event()
        shared_ramp_state = {'running': False, 'current_code': 4095}

        def send_dac_command(code_val: int):
            """Отправляет одиночную команду SOUR:DAC <val> в Shell"""
            nonlocal last_sent_dac_val
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
                cmd = f'SOUR:DAC {code_val}\r\n'
                curr_shell.write(cmd.encode('utf-8'))
                last_sent_dac_val = code_val
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

        def on_slider_change(e):
            """Ручное перемещение слайдера мышкой"""
            nonlocal last_dac_send_time, dac_updating_lock
            if dac_updating_lock or shared_ramp_state['running']:
                return

            code_val = int(e.value)
            dac_updating_lock = True
            dac_input.value = str(code_val)
            dac_updating_lock = False

            now = time.time()
            if now - last_dac_send_time > 0.07 or code_val in (0, 4095):
                last_dac_send_time = now
                send_dac_command(code_val)

        def on_slider_release():
            if shared_ramp_state['running']:
                return
            code_val = int(dac_slider.value)
            if code_val != last_sent_dac_val:
                send_dac_command(code_val)
                ui.notify(f'[ЦАП] Установлен код: {code_val}', type='positive')

        def on_input_change():
            nonlocal dac_updating_lock
            if dac_updating_lock or shared_ramp_state['running']:
                return
            try:
                code_val = int(dac_input.value)
                if 0 <= code_val <= 4095:
                    dac_updating_lock = True
                    dac_slider.value = code_val
                    dac_updating_lock = False
                    send_dac_command(code_val)
                    ui.notify(f'[ЦАП] Установлен код: {code_val}', type='positive')
                else:
                    ui.notify('Код ЦАП должен быть от 0 до 4095!', type='warning')
            except ValueError:
                ui.notify('Введите целое число от 0 до 4095', type='negative')

        # --- ВЫСОКОТОЧНЫЙ РАБОЧИЙ ПОТОК СПАДА ЦАП ---
        def _dac_ramp_worker(target_shell, duration_ms):
            set_win_timer_res(True)
            try:
                curr_ser = target_shell.get('ser')
                if not curr_ser or not curr_ser.is_open:
                    return

                T_ms = max(1.0, float(duration_ms))

                # Расчёт шага по времени и шага по коду
                if T_ms >= 4095.0:
                    # Шаг по времени >= 1 мс, шаг по коду ровно 1
                    dt_sec = (T_ms / 4095.0) / 1000.0
                    total_steps = 4095
                    calc_code = lambda step: 4095 - step
                else:
                    # Шаг по времени строго 1.0 мс (не чаще 1 раза в мс)
                    dt_sec = 0.001
                    total_steps = int(round(T_ms))
                    calc_code = lambda step: int(round(4095.0 - step * (4095.0 / total_steps)))

                start_time = time.perf_counter()

                for step in range(total_steps + 1):
                    if ramp_stop_event.is_set():
                        break

                    code = calc_code(step)
                    if code < 0:
                        code = 0
                    if code > 4095:
                        code = 4095

                    # Быстрая отправка в UART без лишних накладных расходов
                    cmd = f'SOUR:DAC {code}\r\n'.encode('ascii')
                    try:
                        curr_ser.write(cmd)
                    except Exception as err:
                        print(f'[DAC WORKER ERROR]: {err}')
                        break

                    shared_ramp_state['current_code'] = code

                    # Прецизионное выдерживание следующей миллисекунды
                    target_time = start_time + (step + 1) * dt_sec
                    while not ramp_stop_event.is_set():
                        now = time.perf_counter()
                        rem = target_time - now
                        if rem <= 0:
                            break
                        # Если до следующего тика больше 2 мс - спим, иначе точный spin-wait
                        if rem > 0.002:
                            time.sleep(rem - 0.001)

                # Гарантированно выставляем 0 в конце
                if not ramp_stop_event.is_set():
                    try:
                        curr_ser.write(b'SOUR:DAC 0\r\n')
                        shared_ramp_state['current_code'] = 0
                    except Exception:
                        pass

            finally:
                set_win_timer_res(False)
                shared_ramp_state['running'] = False

        def toggle_ramp():
            nonlocal ramp_thread
            curr_shell = shell_ref.get('ser')
            if not curr_shell or not curr_shell.is_open:
                ui.notify('SCPI порт закрыт!', type='warning')
                return

            if not shared_ramp_state['running']:
                try:
                    dur = float(ramp_time_input.value)
                    if dur <= 0:
                        raise ValueError()
                except ValueError:
                    ui.notify('Введите корректное время в мс (> 0)!', type='negative')
                    return

                shared_ramp_state['running'] = True
                ramp_stop_event.clear()

                btn_ramp.text = 'Стоп'
                btn_ramp.classes(remove='bg-emerald-600 hover:bg-emerald-500', add='bg-red-600 hover:bg-red-500')
                ui.notify(f'Запущен спад ЦАП (4095 -> 0 за {int(dur)} мс в отдельном потоке)...', type='info')

                # Запуск отдельного потока ОС
                ramp_thread = threading.Thread(
                    target=_dac_ramp_worker, args=(shell_ref, dur), daemon=True
                )
                ramp_thread.start()
            else:
                ramp_stop_event.set()
                shared_ramp_state['running'] = False
                btn_ramp.text = 'Старт'
                btn_ramp.classes(remove='bg-red-600 hover:bg-red-500', add='bg-emerald-600 hover:bg-emerald-500')
                ui.notify('Спад ЦАП остановлен', type='warning')

        # Фоновый опрос для синхронизации графического интерфейса со значениями из потока
        def sync_ui_with_thread():
            if shared_ramp_state['running']:
                code = shared_ramp_state['current_code']
                if dac_slider.value != code:
                    nonlocal dac_updating_lock
                    dac_updating_lock = True
                    dac_slider.value = code
                    dac_input.value = str(code)
                    dac_updating_lock = False
            else:
                if btn_ramp.text != 'Старт':
                    btn_ramp.text = 'Старт'
                    btn_ramp.classes(remove='bg-red-600 hover:bg-red-500', add='bg-emerald-600 hover:bg-emerald-500')
                    # Обновляем UI до финального нуля
                    dac_slider.value = shared_ramp_state['current_code']
                    dac_input.value = str(shared_ramp_state['current_code'])

        ui.timer(0.04, sync_ui_with_thread)

        # --- ВЕРСТКА ПАНЕЛИ ---
        with ui.row().classes('w-full gap-4 items-center'):

            # ЛЕВАЯ ЧАСТЬ: ЦАП (Слайдер + Потоковый спад)
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex flex-col gap-1.5'):
                with ui.row().classes('w-full justify-between items-center'):
                    ui.label('ЦАП (12-бит)').classes('text-xs font-bold text-sky-300')
                    with ui.row().classes('items-center gap-1'):
                        dac_input = ui.input(value='4095').props(
                            'dark outlined dense type=number min=0 max=4095 step=1 input-class="font-mono text-xs text-right w-16"'
                        ).classes('bg-slate-900 rounded')
                        dac_input.on('keydown.enter', on_input_change)
                        ui.label('код').classes('text-[10px] text-slate-400')

                dac_slider = ui.slider(min=0, max=4095, step=1, value=4095, on_change=on_slider_change).props(
                    'dark color=sky-400'
                ).classes('w-full mt-0.5')
                dac_slider.on('pan-end', on_slider_release)
                dac_slider.on('change', on_slider_release)

                # Режим спада
                with ui.row().classes('w-full items-center justify-between pt-1 border-t border-slate-600/60'):
                    with ui.row().classes('items-center gap-1'):
                        ui.label('Спад 4095→0:').classes('text-[11px] text-slate-300 font-medium')
                        ramp_time_input = ui.input(value='1000', placeholder='мс').props(
                            'dark outlined dense type=number min=1 step=100 input-class="font-mono text-xs text-right w-20"'
                        ).classes('bg-slate-900 rounded')
                        ui.label('мс').classes('text-[10px] text-slate-400')

                    btn_ramp = ui.button('Старт', on_click=toggle_ramp).classes(
                        'bg-emerald-600 hover:bg-emerald-500 text-white font-bold text-xs py-1 px-3 rounded shadow'
                    )

            # ПРАВАЯ ЧАСТЬ: Потенциометры R1 и R2
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex flex-col gap-2'):
                ui.label('Потенциометры (8-бит)').classes('text-xs font-bold text-sky-300')

                with ui.row().classes('w-full gap-2 items-center justify-between'):
                    # Резистор 1
                    with ui.row().classes('items-center gap-1 flex-1'):
                        ui.label('R1:').classes('text-xs text-white font-medium')
                        res1_input = ui.input(placeholder='0-255', value='0').props(
                            'dark outlined dense type=number min=0 max=255 step=1 input-class="text-right text-xs"'
                        ).classes('w-16 bg-slate-900 rounded')

                        def send_res1():
                            try:
                                val = int(res1_input.value)
                                if 0 <= val <= 255:
                                    cmd = f'SOUR:RES 1, {val}\r\n'
                                    curr_shell = shell_ref.get('ser')
                                    if curr_shell and curr_shell.is_open:
                                        curr_shell.write(cmd.encode('utf-8'))
                                        ui.notify(f'[R1] SCPI: {cmd.strip()}', type='positive')
                                    else:
                                        ui.notify('SCPI порт закрыт!', type='warning')
                                else:
                                    ui.notify('Значение R1 от 0 до 255!', type='warning')
                            except ValueError:
                                ui.notify('Введите число 0-255', type='negative')

                        res1_input.on('keydown.enter', send_res1)
                        ui.button('Set', on_click=send_res1).classes(
                            'bg-emerald-600 hover:bg-emerald-500 text-white text-[10px] font-bold py-1 px-2 rounded shadow'
                        )

                    # Резистор 2
                    with ui.row().classes('items-center gap-1 flex-1'):
                        ui.label('R2:').classes('text-xs text-white font-medium')
                        res2_input = ui.input(placeholder='0-255', value='0').props(
                            'dark outlined dense type=number min=0 max=255 step=1 input-class="text-right text-xs"'
                        ).classes('w-16 bg-slate-900 rounded')

                        def send_res2():
                            try:
                                val = int(res2_input.value)
                                if 0 <= val <= 255:
                                    cmd = f'SOUR:RES 2, {val}\r\n'
                                    curr_shell = shell_ref.get('ser')
                                    if curr_shell and curr_shell.is_open:
                                        curr_shell.write(cmd.encode('utf-8'))
                                        ui.notify(f'[R2] SCPI: {cmd.strip()}', type='positive')
                                    else:
                                        ui.notify('SCPI порт закрыт!', type='warning')
                                else:
                                    ui.notify('Значение R2 от 0 до 255!', type='warning')
                            except ValueError:
                                ui.notify('Введите число 0-255', type='negative')

                        res2_input.on('keydown.enter', send_res2)
                        ui.button('Set', on_click=send_res2).classes(
                            'bg-emerald-600 hover:bg-emerald-500 text-white text-[10px] font-bold py-1 px-2 rounded shadow'
                        )