from nicegui import ui
from telemetry import telemetry_reader_loop
from ui_components import build_dashboard

# Словари для хранения текущих открытых сессий портов (по ссылке)
ports_refs = {'tel': {'ser': None}, 'shell': {'ser': None}}

# Словарь для элементов UI датчиков
ui_labels = {}

# Настройка стилей страницы
ui.query('body').classes('bg-slate-900 text-white p-2')
ui.label('UTS2 Embedded Dashboard').classes(
    'text-xl font-bold mb-2 text-sky-400'
)

# Строим интерфейс из файла ui_components.py
build_dashboard(ports_refs, ui_labels)

# Запускаем фоновый процесс чтения телеметрии
ui.timer(0.1, lambda: telemetry_reader_loop(ports_refs['tel'], ui_labels), once=True)

# Запуск приложения
ui.run(port=8080, title='UTS2 Dashboard', favicon=None)