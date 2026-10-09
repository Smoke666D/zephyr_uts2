from nicegui import ui
from telemetry import telemetry_reader_loop
from ui_components import build_dashboard

ports_refs = {'tel': {'ser': None}, 'shell': {'ser': None}}
ui_labels = {}

ui.query('body').classes('bg-slate-900 text-white p-2')
ui.label('UTS2 Embedded Dashboard').classes(
    'text-xl font-bold mb-2 text-sky-400'
)

# Получаем из билдера все три словаря (can, lin, а также общую структуру)
can_logs_dict, lin_logs_dict = build_dashboard(ports_refs, ui_labels)

# Запускаем фоновый цикл телеметрии, передавая туда словари логов CAN и LIN
ui.timer(
    0.1,
    lambda: telemetry_reader_loop(
        ports_refs['tel'], ui_labels, can_logs_dict, lin_logs_dict
    ),
    once=True,
)

ui.run(port=8080, title='UTS2 Dashboard', favicon=None)