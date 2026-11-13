from nicegui import ui

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


def build_current_panel(ports_refs, ui_labels):
    """Строит панель мониторинга 8 датчиков тока, напряжения и мощности"""
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        with ui.row().classes('w-full justify-between items-center mb-1'):
            ui.label('Панель датчиков тока, напряжения и мощности').classes(
                'text-xs font-semibold text-yellow-300'
            )

        def make_energy_reset_handler(d_idx, d_name):
            def handler():
                cmd = f'energy_reset {d_idx}\r\n'
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                    curr_shell.write(cmd.encode('utf-8'))
                    ui.notify(f'[{d_name}] Команда сброса отправлена', type='info')
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

                    # Строка U (Напряжение)
                    with ui.row().classes('w-full justify-between items-center mt-0.5'):
                        ui.label('U:').classes('text-[10px] text-slate-300 leading-none')
                        with ui.row().classes('items-center gap-1'):
                            ui_labels[f'curr_v_{i}'] = ui.label('0.00').classes(
                                'text-xs font-mono text-emerald-400 font-bold leading-none'
                            )
                            ui.label('В').classes('text-[10px] text-slate-400 leading-none')

                    # Строка I (Ток)
                    with ui.row().classes('w-full justify-between items-center mt-1'):
                        ui.label('I:').classes('text-[10px] text-slate-300 leading-none')
                        with ui.row().classes('items-center gap-1'):
                            ui_labels[f'curr_i_{i}'] = ui.label('0.0000').classes(
                                'text-xs font-mono text-sky-400 font-bold leading-none'
                            )
                            ui.label('А').classes('text-[10px] text-slate-400 leading-none')

                    # Строка P (Мощность вместо энергии)
                    with ui.row().classes('w-full justify-between items-center mt-1'):
                        ui.label('P:').classes('text-[10px] text-slate-300 leading-none')
                        with ui.row().classes('items-center gap-1'):
                            p_lbl = ui.label('0.0000').classes(
                                'text-xs font-mono text-amber-400 font-bold leading-none'
                            )
                            ui_labels[f'curr_p_{i}'] = p_lbl
                            ui_labels[f'curr_e_{i}'] = p_lbl  # Алиас для обратной совместимости
                            ui.label('Вт').classes('text-[10px] text-slate-400 leading-none')