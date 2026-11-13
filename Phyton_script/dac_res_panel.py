import time
from nicegui import ui


def build_dac_res_panel(ports_refs):
    """
    Строит панель управления ЦАП (со слайдером и полем ввода)
    и потенциометрами R1/R2 (8-бит).
    """
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        ui.label('Управление ЦАП (SOURce:DAC) и потенциометрами (SOURce:RESistance)').classes(
            'text-xs font-semibold text-yellow-300 mb-2'
        )

        last_dac_send_time = 0.0
        last_sent_dac_val = -1
        dac_updating_lock = False

        def send_dac_command(code_val: int):
            """Отправляет команду SOUR:DAC <val> в порт"""
            nonlocal last_sent_dac_val
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
                cmd = f'SOUR:DAC {code_val}\r\n'
                curr_shell.write(cmd.encode('utf-8'))
                last_sent_dac_val = code_val
                print(f'[SCPI TX]: {cmd.strip()}')
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

        def on_slider_change(e):
            """Обработчик движения слайдера с троттлингом (не чаще 15 раз в сек)"""
            nonlocal last_dac_send_time, dac_updating_lock
            if dac_updating_lock:
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
            """Гарантирует отправку точного финального значения при отпускании ползунка"""
            code_val = int(dac_slider.value)
            if code_val != last_sent_dac_val:
                send_dac_command(code_val)
                ui.notify(f'[ЦАП] Установлен код: {code_val}', type='positive')

        def on_input_change():
            """Обработчик ручного ввода числа в текстовое поле"""
            nonlocal dac_updating_lock
            if dac_updating_lock:
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

        with ui.row().classes('w-full gap-4 items-center'):

            # --- ЛЕВАЯ ЧАСТЬ: Управление ЦАП со слайдером ---
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex flex-col gap-1'):
                with ui.row().classes('w-full justify-between items-center'):
                    ui.label('ЦАП (12-бит)').classes('text-xs font-bold text-sky-300')
                    with ui.row().classes('items-center gap-1'):
                        dac_input = ui.input(value='4095').props(
                            'dark outlined dense type=number min=0 max=4095 step=1 input-class="font-mono text-xs text-right w-16"'
                        ).classes('bg-slate-900 rounded')
                        dac_input.on('keydown.enter', on_input_change)
                        ui.label('код').classes('text-[10px] text-slate-400')

                # Слайдер ЦАП от 0 до 4095
                dac_slider = ui.slider(min=0, max=4095, step=1, value=4095, on_change=on_slider_change).props(
                    'dark color=sky-400'
                ).classes('w-full mt-1')

                dac_slider.on('pan-end', on_slider_release)
                dac_slider.on('change', on_slider_release)

            # --- ПРАВАЯ ЧАСТЬ: Потенциометры R1 и R2 ---
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