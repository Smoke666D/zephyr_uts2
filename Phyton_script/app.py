import asyncio
import os
import sys
from nicegui import app, ui
from telemetry import telemetry_reader_loop
from ui_components import build_dashboard

# Словари для хранения открытых дескрипторов портов
ports_refs = {'tel': {'ser': None}, 'shell': {'ser': None}}
ui_labels = {}

# Настройка стилей страницы
ui.query('body').classes('bg-slate-900 text-white p-2')


def close_all_ports():
  """Безопасно закрывает все открытые COM-порты"""
  for key, ref in ports_refs.items():
    s = ref.get('ser')
    if s and s.is_open:
      try:
        s.close()
        ref['ser'] = None
        print(f'[SYSTEM]: COM-порт {key} успешно закрыт.')
      except Exception as e:
        print(f'[SYSTEM ERROR]: Ошибка закрытия порта {key}: {e}')


async def shutdown_application():
  """Процедура корректного завершения программы"""
  ui.notify('Завершение работы приложения...', type='warning')

  # 1. Сначала закрываем все аппаратные COM-порты
  close_all_ports()

  # 2. Пытаемся закрыть вкладку в браузере скриптом
  try:
    await ui.run_javascript(
        'window.open("", "_self", ""); window.close();', timeout=0.3
    )
  except Exception:
    pass

  await asyncio.sleep(0.2)

  # 3. Гасим сервер NiceGUI и процесс Python
  try:
    app.shutdown()
  finally:
    os._exit(0)


# Шапка дашборда с кнопкой "ВЫХОД"
with ui.row().classes('w-full justify-between items-center mb-2 px-1'):
  ui.label('UTS2 Embedded Dashboard').classes(
      'text-xl font-bold text-sky-400 tracking-wide'
  )

  # Кнопка Выход
  ui.button('ВЫХОД', on_click=shutdown_application).classes(
      'bg-red-700 hover:bg-red-600 text-white font-bold text-xs py-1.5 px-4'
      ' rounded shadow'
  )

# Закрытие портов при системном сигнале (Ctrl+C в терминале)
app.on_shutdown(close_all_ports)

# Автозавершение при закрытии вкладки крестиком (3 сек пауза на F5)
disconnect_timer = None


def on_client_disconnect(*args, **kwargs):
  global disconnect_timer

  async def delayed_shutdown():
    await asyncio.sleep(3.0)
    print('[SYSTEM]: Вкладка браузера закрыта. Завершение работы...')
    close_all_ports()
    try:
      app.shutdown()
    finally:
      os._exit(0)

  disconnect_timer = asyncio.create_task(delayed_shutdown())


app.on_disconnect(on_client_disconnect)

# Построение интерфейса
can_logs, lin_logs, raw_logs = build_dashboard(ports_refs, ui_labels)

# Фоновый цикл чтения телеметрии
ui.timer(
    0.1,
    lambda: telemetry_reader_loop(
        ports_refs['tel'], ui_labels, can_logs, lin_logs, raw_logs
    ),
    once=True,
)

# Запуск приложения
ui.run(port=8080, title='UTS2 Dashboard', favicon=None, reload=False)