from nicegui import ui
from telemetry import telemetry_reader_loop
from ui_components import build_dashboard

ports_refs = {'tel': {'ser': None}, 'shell': {'ser': None}}
ui_labels = {}

ui.query('body').classes('bg-slate-900 text-white p-2')
ui.label('UTS2 Embedded Dashboard').classes(
    'text-xl font-bold mb-2 text-sky-400'
)

# Строим интерфейс и получаем словари логов
can_logs, lin_logs, raw_logs = build_dashboard(ports_refs, ui_labels)

# Запускаем фоновый процесс чтения телеметрии
ui.timer(
    0.1,
    lambda: telemetry_reader_loop(
        ports_refs['tel'], ui_labels, can_logs, lin_logs, raw_logs
    ),
    once=True,
)

ui.run(port=8080, title='UTS2 Dashboard', favicon=None)