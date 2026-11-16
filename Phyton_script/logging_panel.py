import time
from nicegui import ui
from logger import start_logger, stop_logger
from oscilloscope import refresh_logs_ui


def build_logging_panel():
    """Строит блок управления записью логов в CSV с цифровым секундомером (мм:сс.мс)"""
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        ui.label('Логирование данных в файл (CSV/Excel)').classes(
            'text-xs font-semibold text-sky-400 mb-1'
        )

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