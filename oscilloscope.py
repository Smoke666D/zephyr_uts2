from collections import deque
from globals_data import telemetry_data_cache
from nicegui import ui

# Настройки истории по умолчанию
MAX_POINTS = 300
time_buffer = deque(maxlen=MAX_POINTS)
data_ao1 = deque(maxlen=MAX_POINTS)
data_ao2 = deque(maxlen=MAX_POINTS)
counter = 0

active_chart = None
is_running = True  # По умолчанию осциллограф запущен


def update_oscilloscope_data(ao1_val, ao2_val):
  global counter
  # Если на паузе (Стоп) — игнорируем новые данные
  if not is_running:
    return

  counter += 1

  if len(time_buffer) >= time_buffer.maxlen:
    time_buffer.popleft()
    data_ao1.popleft()
    data_ao2.popleft()

  time_buffer.append(str(counter))
  data_ao1.append(float(ao1_val))
  data_ao2.append(float(ao2_val))

  if active_chart and time_buffer:
    try:
      active_chart.options['xAxis']['data'] = list(time_buffer)
      active_chart.options['series'][0]['data'] = list(data_ao1)
      active_chart.options['series'][1]['data'] = list(data_ao2)
      active_chart.update()
    except Exception as e:
      print(f'Ошибка обновления графика: {e}')


def build_oscilloscope_panel():
  global active_chart

  with ui.card().classes('bg-slate-800 p-3 w-full'):

    # Шапка панели с кнопками управления и масштабом истории
    with ui.row().classes('w-full justify-between items-center mb-2'):
      ui.label('Осциллограф (AO 1 & AO 2)').classes(
          'text-sm font-bold text-sky-400'
      )

      with ui.row().classes('items-center gap-2'):

        # Кнопка Старт / Стоп
        def toggle_run():
          global is_running
          is_running = not is_running
          if is_running:
            btn_run.text = 'Стоп'
            btn_run.classes(remove='bg-emerald-600', add='bg-red-600')
            ui.notify('Осциллограф запущен', type='positive')
          else:
            btn_run.text = 'Старт'
            btn_run.classes(remove='bg-red-600', add='bg-emerald-600')
            ui.notify('Осциллограф на паузе', type='warning')

        btn_run = ui.button('Стоп', on_click=toggle_run).classes(
            'bg-red-600 text-white text-xs font-bold py-1 px-3'
        )

        # Кнопка Очистить
        def clear_history():
          global counter, time_buffer, data_ao1, data_ao2
          time_buffer.clear()
          data_ao1.clear()
          data_ao2.clear()
          counter = 0
          if active_chart:
            active_chart.options['xAxis']['data'] = []
            active_chart.options['series'][0]['data'] = []
            active_chart.options['series'][1]['data'] = []
            active_chart.update()
          ui.notify('История графиков очищена', type='info')

        ui.button('Очистить', on_click=clear_history).classes(
            'bg-slate-700 text-white text-xs py-1 px-3'
        )

        ui.label('|').classes('text-slate-500')

        # Выбор глубины истории
        ui.label('История:').classes('text-xs text-slate-300')

        def change_history_depth(e):
          global time_buffer, data_ao1, data_ao2
          new_max = int(e.value)
          time_buffer = deque(time_buffer, maxlen=new_max)
          data_ao1 = deque(data_ao1, maxlen=new_max)
          data_ao2 = deque(data_ao2, maxlen=new_max)
          ui.notify(f'Глубина истории изменена на {new_max} точек', type='info')

        ui.select(
            options={100: '100', 300: '300', 500: '500', 1000: '1000'},
            value=300,
            on_change=change_history_depth,
        ).props('dark outlined dense').classes(
            'w-24 bg-slate-700 text-xs text-white rounded'
        )

    # Инициализация графика с зумом
    active_chart = ui.echart({
        'tooltip': {'trigger': 'axis'},
        'xAxis': {'type': 'category', 'data': [], 'boundaryGap': False},
        'yAxis': {'type': 'value'},
        'series': [
            {
                'name': 'AO 1',
                'type': 'line',
                'data': [],
                'showSymbol': False,
                'itemStyle': {'color': '#38bdf8'},
            },
            {
                'name': 'AO 2',
                'type': 'line',
                'data': [],
                'showSymbol': False,
                'itemStyle': {'color': '#4ade80'},
            },
        ],
        'legend': {'data': ['AO 1', 'AO 2'], 'textStyle': {'color': '#fff'}},
        'grid': {
            'left': '3%',
            'right': '4%',
            'bottom': '15%',
            'top': '20%',
            'containLabel': True,
        },
        'dataZoom': [
            {'type': 'slider', 'xAxisIndex': 0, 'start': 0, 'end': 100},
            {'type': 'inside', 'xAxisIndex': 0},
        ],
    }).classes('w-full h-60 bg-slate-900 rounded')