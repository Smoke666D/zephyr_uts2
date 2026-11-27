from nicegui import ui


def build_can_panel(ports_refs, can_logs_dict):
    """Строит блок из 3 интерфейсов CAN (вкл/выкл, Classic/CANFD, выбор скоростей, логирование, отправка)"""
    shell_ref = ports_refs['shell']

    ui.label('Интерфейсы CAN (1-3)').classes('text-sm font-bold text-sky-300 mt-3')

    for can_idx in range(1, 4):
        with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
            # Шапка карточки: название и чекбокс состояния контроллера
            with ui.row().classes('w-full justify-between items-center mb-1'):
                ui.label(f'CAN Интерфейс {can_idx}').classes(
                    'text-xs font-semibold text-yellow-300'
                )

                def make_can_stat_handler(c_idx):
                    def handler(e):
                        state = 'ON' if e.value else 'OFF'
                        cmd = f'CAN:STAT {c_idx}, {state}\r\n'
                        curr_shell = shell_ref.get('ser')
                        if curr_shell and getattr(curr_shell, 'is_open', False):
                            curr_shell.write(cmd.encode('utf-8'))
                            ui.notify(f'[CAN{c_idx}] SCPI: {cmd.strip()}', type='info')
                        else:
                            ui.notify('SCPI порт закрыт!', type='warning')

                    return handler

                ui.checkbox(
                    'Контроллер включен',
                    value=False,
                    on_change=make_can_stat_handler(can_idx),
                ).props('dense').classes('text-white text-xs font-medium')

            with ui.row().classes('w-full gap-2 items-center'):
                can_speed_data = ui.select(
                    options=['1000', '500', '250', '125'],
                    value='1000',
                    label='Скорость Data (кб/с)',
                ).props('dark outlined dense').classes('w-36 bg-slate-700 text-xs text-white rounded')
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

            ui.label('Входящие сообщения:').classes('text-[10px] text-slate-400 mt-1 mb-0.5')
            can_log = ui.log(max_lines=30).classes(
                'w-full h-20 bg-slate-900 text-green-400 font-mono text-[11px] p-1 rounded'
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

                    if curr_shell and getattr(curr_shell, 'is_open', False):
                        curr_shell.write(cmd.encode('utf-8'))
                        log.push(f'TX -> ID: {cid} | DATA: {cdata}')
                        id_inp.set_value('')
                        data_inp.set_value('')
                    else:
                        ui.notify('SCPI порт закрыт! Невозможно отправить пакет', type='warning')

                can_id_input.on('keydown.enter', send_can_msg)
                can_data_input.on('keydown.enter', send_can_msg)
                ui.button('Отправить', on_click=send_can_msg).classes(
                    'bg-sky-600 text-white text-xs py-1 px-3 font-bold'
                )