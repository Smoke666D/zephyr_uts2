import time
from nicegui import ui
from logger import ALL_TELEMETRY_CHANNELS, start_logger, stop_logger
from oscilloscope import refresh_logs_ui

NONE_OPTION = '--- Не выбран ---'

# Каналы, выбранные по умолчанию в первых слотах
DEFAULT_SLOTS = [
    'Питание тестируемого узла V',
    'Питание тестируемого узла I',
    'AO_1',
    'AO_2',
    NONE_OPTION,
    NONE_OPTION,
    NONE_OPTION,
    NONE_OPTION,
    NONE_OPTION,
    NONE_OPTION,
]


def build_logging_panel():
    """Строит блок управления записью логов с выбором до 10 каналов и секундомером"""
    channel_options = [NONE_OPTION] + list(ALL_TELEMETRY_CHANNELS.keys())
    slot_selects = []

    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        # Верхняя строка: заголовок, секундомер и кнопка Старт/Стоп
        with ui.row().classes('w-full justify-between items-center mb-2'):
            ui.label('Логирование данных в файл (CSV/Excel — до 10 каналов)').classes(
                'text-sm font-bold text-sky-400'
            )

            with ui.row().classes('gap-2 items-center'):
                def clear_all_slots():
                    if timer_active:
                        return
                    for sel in slot_selects:
                        sel.value = NONE_OPTION

                def set_default_slots():
                    if timer_active:
                        return
                    for idx, sel in enumerate(slot_selects):
                        sel.value = DEFAULT_SLOTS[idx] if idx < len(DEFAULT_SLOTS) else NONE_OPTION

                ui.button('По умолчанию', on_click=set_default_slots).classes(
                    'bg-slate-700 hover:bg-slate-600 text-slate-200 text-[10px] py-1 px-2 rounded'
                )
                ui.button('Очистить выбор', on_click=clear_all_slots).classes(
                    'bg-slate-700 hover:bg-slate-600 text-slate-200 text-[10px] py-1 px-2 rounded'
                )

        # --- СЕТКА ИЗ 10 ВЫПАДАЮЩИХ СПИСКОВ ДЛЯ ВЫБОРА КАНАЛОВ ---
        with ui.grid(columns=5).classes('w-full gap-2 bg-slate-700/40 p-2 rounded border border-slate-600 mb-2'):
            for i in range(10):
                default_val = DEFAULT_SLOTS[i] if i < len(DEFAULT_SLOTS) and DEFAULT_SLOTS[i] in channel_options else NONE_OPTION
                sel = ui.select(
                    options=channel_options,
                    value=default_val,
                    label=f'Канал {i + 1}',
                ).props('dark outlined dense options-dense').classes(
                    'w-full bg-slate-800 text-xs text-white rounded'
                )
                slot_selects.append(sel)

        # --- НИЖНЯЯ СТРОКА: ТАЙМЕР И КНОПКА ЗАПИСИ ---
        with ui.row().classes('gap-3 items-center'):
            timer_start_ts = 0.0
            timer_active = False

            timer_lbl = ui.label('00:00.000').classes(
                'text-sm font-mono text-emerald-400 font-bold bg-slate-900 py-1 px-2.5 rounded border border-slate-700'
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
                    # Собираем уникальные выбранные каналы из 10 слотов
                    chosen = []
                    for s in slot_selects:
                        if s.value and s.value != NONE_OPTION and s.value not in chosen:
                            chosen.append(s.value)

                    if not chosen:
                        ui.notify('Выберите хотя бы один канал для записи в лог!', type='warning')
                        return

                    # Блокируем селекты на время записи
                    for s in slot_selects:
                        s.disable()

                    start_logger(selected_channels=chosen)
                    timer_start_ts = time.time()
                    timer_active = True
                    log_clock_timer.activate()
                    timer_lbl.set_text('00:00.000')

                    btn_log.text = 'Стоп записи'
                    btn_log.classes(remove='bg-emerald-600', add='bg-red-600')
                    ui.notify(f'Запись начата (каналов: {len(chosen)})', type='positive')
                else:
                    stop_logger()
                    timer_active = False
                    log_clock_timer.deactivate()
                    elapsed = time.time() - timer_start_ts
                    mins = int(elapsed // 60)
                    secs = int(elapsed % 60)
                    millis = int((elapsed - int(elapsed)) * 1000)
                    timer_lbl.set_text(f'{mins:02d}:{secs:02d}.{millis:03d}')

                    # Разблокируем селекты после остановки
                    for s in slot_selects:
                        s.enable()

                    btn_log.text = 'Старт записи лога'
                    btn_log.classes(remove='bg-red-600', add='bg-emerald-600')
                    ui.notify(
                        f'Запись завершена ({mins:02d}:{secs:02d}.{millis:03d})',
                        type='info',
                    )

                    ui.timer(0.2, refresh_logs_ui, once=True)

            btn_log = ui.button('Старт записи лога', on_click=toggle_log).classes(
                'bg-emerald-600 text-white text-xs font-bold py-1.5 px-4 rounded shadow'
            )

            ui.label('В лог пишутся только выбранные выше каналы (ускоряет загрузку осциллографа)').classes(
                'text-[11px] text-slate-400'
            )