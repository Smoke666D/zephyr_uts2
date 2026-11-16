from nicegui import ui


def build_lin_panel(ports_refs, ui_labels, lin_logs_dict):
    """Строит блок из 4 интерфейсов LIN (SCPI OUTPut:LINPulldown, замеры U, отправка, логи)"""
    shell_ref = ports_refs['shell']

    ui.label('Интерфейсы LIN (1-4)').classes('text-sm font-bold text-sky-300 mt-3')

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
                        ui.notify(f'[{channel_name} PULL_DOWN] SCPI: {cmd.strip()}', type='info')
                    else:
                        ui.notify('SCPI порт закрыт!', type='warning')

                ui.checkbox('PULL_DOWN', value=False, on_change=handle_pull_down).classes(
                    'text-white text-xs'
                )

                with ui.row().classes('items-center gap-1'):
                    ui.label('U:').classes('text-[11px] text-slate-300')
                    lin_v_lbl = ui.label('0.00').classes(
                        'text-xs font-mono text-emerald-400 font-bold'
                    )
                    ui.label('В').classes('text-[11px] text-slate-400')
                    ui_labels[f'lin_v_{l_idx}'] = lin_v_lbl

            ui.label('Входящие пакеты:').classes('text-[10px] text-slate-400 mt-1 mb-0.5')
            lin_log = ui.log(max_lines=20).classes(
                'w-full h-16 bg-slate-900 text-cyan-400 font-mono text-[11px] p-1 rounded'
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
                        ui.notify(f'LIN{idx}: Введите данные для отправки!', type='warning')
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