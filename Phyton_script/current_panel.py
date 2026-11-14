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
    """Строит панель мониторинга 8 датчиков с выводом параметров в одну строку: ТОК -> НАПРЯЖЕНИЕ -> МОЩНОСТЬ"""
    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        with ui.row().classes('w-full justify-between items-center mb-1.5'):
            ui.label('Панель датчиков тока, напряжения и мощности').classes(
                'text-sm font-semibold text-yellow-300'
            )

        with ui.grid(columns=4).classes('w-full gap-2'):
            for idx, sensor_name in enumerate(CURRENT_SENSOR_NAMES):
                i = idx + 1
                with ui.card().classes(
                    'bg-slate-700/80 py-2 px-3 rounded flex flex-col justify-between border border-slate-600/60'
                ):
                    # Заголовок / имя датчика
                    ui.label(sensor_name).classes(
                        'text-xs text-yellow-300 font-bold leading-none truncate w-full mb-2'
                    )

                    # Единая горизонтальная строка: ТОК | НАПРЯЖЕНИЕ | МОЩНОСТЬ
                    with ui.row().classes('w-full items-center justify-between gap-1 flex-nowrap'):
                        # 1. ТОК (I)
                        with ui.row().classes('items-center gap-0.5 flex-nowrap'):
                            ui.label('I:').classes('text-[11px] text-slate-200 font-semibold leading-none')
                            ui_labels[f'curr_i_{i}'] = ui.label('0.0000').classes(
                                'text-sm font-mono text-sky-400 font-bold leading-none'
                            )
                            ui.label('А').classes('text-[10px] text-slate-300 leading-none')

                        # 2. НАПРЯЖЕНИЕ (U)
                        with ui.row().classes('items-center gap-0.5 flex-nowrap'):
                            ui.label('U:').classes('text-[11px] text-slate-200 font-semibold leading-none')
                            ui_labels[f'curr_v_{i}'] = ui.label('0.00').classes(
                                'text-sm font-mono text-emerald-400 font-bold leading-none'
                            )
                            ui.label('В').classes('text-[10px] text-slate-300 leading-none')

                        # 3. МОЩНОСТЬ (P)
                        with ui.row().classes('items-center gap-0.5 flex-nowrap'):
                            ui.label('P:').classes('text-[11px] text-slate-200 font-semibold leading-none')
                            p_lbl = ui.label('0.0000').classes(
                                'text-sm font-mono text-amber-300 font-bold leading-none'
                            )
                            ui_labels[f'curr_p_{i}'] = p_lbl
                            ui_labels[f'curr_e_{i}'] = p_lbl  # Алиас для обратной совместимости
                            ui.label('Вт').classes('text-[10px] text-slate-300 leading-none')