import asyncio
import time
from nicegui import ui


def build_dac_res_panel(ports_refs):
    """Строит панель управления ЦАП (12-бит) со слайдером/спадом и потенциометрами (8-бит)"""
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        ui.label('Управление ЦАП (SOURce:DAC) и потенциометрами (SOURce:RESistance)').classes(
            'text-xs font-semibold text-yellow-300 mb-2'
        )

        # Переменные состояния для ЦАП
        last_dac_send_time = 0.0
        last_sent_dac_val = -1
        dac_updating_lock = False
        is_ramping = False
        ramp_task = None

        def send_dac_command(code_val: int):
            """Отправляет команду SOUR:DAC <val> в порт"""
            nonlocal last_sent_dac_val
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
                cmd = f'SOUR:DAC {code_val}\r\n'
                curr_shell.write(cmd.encode('utf-8'))
                last_sent_dac_val = code_val
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

        def on_slider_change(e):
            """Обработчик движения слайдера с троттлингом"""
            nonlocal last_dac_send_time, dac_updating_lock
            if dac_updating_lock or is_ramping:
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
            """Отправка финального значения при отпускании ползунка мыши"""
            if is_ramping:
                return
            code_val = int(dac_slider.value)
            if code_val != last_sent_dac_val:
                send_dac_command(code_val)
                ui.notify(f'[ЦАП] Установлен код: {code_val}', type='positive')

        def on_input_change():
            """Ручной ввод кода ЦАП через текстовое поле"""
            nonlocal dac_updating_lock
            if dac_updating_lock or is_ramping:
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

        # --- АСИНХРОННЫЙ ЛИНЕЙНЫЙ СПАД ОТ 4095 ДО 0 ---
        async def run_dac_ramp(duration_ms: float):
            nonlocal is_ramping, dac_updating_lock
            total_sec = max(0.02, duration_ms / 1000.0)
            start_time = time.time()

            try:
                while is_ramping:
                    elapsed = time.time() - start_time
                    progress = elapsed / total_sec

                    if progress >= 1.0:
                        code = 0
                    else:
                        code = int(round(4095 * (1.0 - progress)))

                    # Блокируем триггеры ручного ввода и обновляем UI
                    dac_updating_lock = True
                    dac_input.value = str(code)
                    dac_slider.value = code
                    dac_updating_lock = False

                    send_dac_command(code)

                    if progress >= 1.0:
                        break

                    # Шаг ~25 мс (40 кадров/сек) для ровного спада без забивания UART
                    await asyncio.sleep(0.025)

                if is_ramping:
                    ui.notify('Спад ЦАП завершен (код 0)', type='positive')
            except asyncio.CancelledError:
                pass
            finally:
                is_ramping = False
                btn_ramp.text = 'Старт'
                btn_ramp.classes(remove='bg-red-600 hover:bg-red-500', add='bg-emerald-600 hover:bg-emerald-500')

        def toggle_ramp():
            nonlocal is_ramping, ramp_task
            curr_shell = shell_ref.get('ser')
            if not curr_shell or not curr_shell.is_open:
                ui.notify('SCPI порт закрыт!', type='warning')
                return

            if not is_ramping:
                try:
                    dur = float(ramp_time_input.value)
                    if dur <= 0:
                        raise ValueError()
                except ValueError:
                    ui.notify('Введите корректное время в мс (> 0)!', type='negative')
                    return

                is_ramping = True
                btn_ramp.text = 'Стоп'
                btn_ramp.classes(remove='bg-emerald-600 hover:bg-emerald-500', add='bg-red-600 hover:bg-red-500')
                ui.notify(f'Запущен спад ЦАП (4095 -> 0 за {int(dur)} мс)...', type='info')
                ramp_task = asyncio.create_task(run_dac_ramp(dur))
            else:
                is_ramping = False
                if ramp_task and not ramp_task.done():
                    ramp_task.cancel()
                btn_ramp.text = 'Старт'
                btn_ramp.classes(remove='bg-red-600 hover:bg-red-500', add='bg-emerald-600 hover:bg-emerald-500')
                ui.notify('Спад ЦАП остановлен пользователем', type='warning')

        # --- ВЕРСТКА ПАНЕЛИ ---
        with ui.row().classes('w-full gap-4 items-center'):

            # ЛЕВАЯ ЧАСТЬ: ЦАП (Слайдер + Автоспад)
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex flex-col gap-1.5'):
                # 1. Шапка ЦАП с ручным кодом
                with ui.row().classes('w-full justify-between items-center'):
                    ui.label('ЦАП (12-бит)').classes('text-xs font-bold text-sky-300')
                    with ui.row().classes('items-center gap-1'):
                        dac_input = ui.input(value='4095').props(
                            'dark outlined dense type=number min=0 max=4095 step=1 input-class="font-mono text-xs text-right w-16"'
                        ).classes('bg-slate-900 rounded')
                        dac_input.on('keydown.enter', on_input_change)
                        ui.label('код').classes('text-[10px] text-slate-400')

                # 2. Ползунок ручной регулировки
                dac_slider = ui.slider(min=0, max=4095, step=1, value=4095, on_change=on_slider_change).props(
                    'dark color=sky-400'
                ).classes('w-full mt-0.5')
                dac_slider.on('pan-end', on_slider_release)
                dac_slider.on('change', on_slider_release)

                # 3. Режим спада (4095 -> 0)
                with ui.row().classes('w-full items-center justify-between pt-1 border-t border-slate-600/60'):
                    with ui.row().classes('items-center gap-1'):
                        ui.label('Спад 4095→0:').classes('text-[11px] text-slate-300 font-medium')
                        ramp_time_input = ui.input(value='1000', placeholder='мс').props(
                            'dark outlined dense type=number min=20 step=100 input-class="font-mono text-xs text-right w-20"'
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