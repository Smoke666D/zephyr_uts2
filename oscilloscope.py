import csv
import glob
import os
from nicegui import ui

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

active_chart = None
channels_container = None

cached_data = {}
cached_timestamps = []
selected_channels = []

SERIES_COLORS = ['#38bdf8', '#4ade80', '#facc15', '#f43f5e', '#c084fc', '#fb923c']


def update_oscilloscope_data(*args, **kwargs):
  pass


def get_log_files():
  os.makedirs(LOGS_DIR, exist_ok=True)
  pattern = os.path.join(LOGS_DIR, '*.csv')
  files = glob.glob(pattern)
  files.sort(key=os.path.getmtime, reverse=True)
  return files


def render_chart():
  global active_chart, selected_channels, cached_data, cached_timestamps
  if not active_chart or not cached_timestamps:
    return

  series = []
  legend = []

  for idx, col_name in enumerate(selected_channels):
    if col_name in cached_data:
      color = SERIES_COLORS[idx % len(SERIES_COLORS)]
      legend.append(col_name)
      series.append({
          'name': col_name,
          'type': 'line',
          'data': cached_data[col_name],
          'showSymbol': False,
          'itemStyle': {'color': color},
      })

  # Передаем конфигурацию напрямую в движок ECharts (True = notMerge, полная перерисовка)
  chart_options = {
      'tooltip': {'trigger': 'axis'},
      'xAxis': {'type': 'category', 'data': cached_timestamps, 'boundaryGap': False},
      'yAxis': {'type': 'value', 'scale': True},
      'series': series,
      'legend': {'data': legend, 'textStyle': {'color': '#fff'}},
      'grid': {
          'left': '3%',
          'right': '4%',
          'bottom': '15%',
          'top': '15%',
          'containLabel': True,
      },
      'dataZoom': [
          {'type': 'slider', 'xAxisIndex': 0, 'start': 0, 'end': 100},
          {'type': 'inside', 'xAxisIndex': 0},
      ],
  }
  
  active_chart.run_chart_method('setOption', chart_options, True)


def rebuild_channel_checkboxes(columns):
  global channels_container, selected_channels
  if not channels_container:
    return

  channels_container.clear()
  with channels_container:
    ui.label('Выберите каналы для графика (максимум 4):').classes(
        'text-xs font-semibold text-yellow-300 col-span-full mb-1'
    )

    for col in columns:
      is_default = col in selected_channels

      def make_handler(col_name):
        def on_change(e):
          global selected_channels
          if e.value:
            if len(selected_channels) >= 4:
              e.sender.set_value(False)
              ui.notify('Максимум 4 канала одновременно!', type='warning')
              return
            if col_name not in selected_channels:
              selected_channels.append(col_name)
          else:
            if col_name in selected_channels:
              selected_channels.remove(col_name)
          render_chart()
        return on_change

      ui.checkbox(
          col,
          value=is_default,
          on_change=make_handler(col),
      ).classes('text-white text-xs')


def load_log_to_chart(file_path):
  global cached_data, cached_timestamps, selected_channels
  if not file_path or not os.path.exists(file_path):
    ui.notify('Файл не выбран или не существует!', type='warning')
    return

  ui.notify(f'Загрузка: {os.path.basename(file_path)}...', type='info')

  cached_data = {}
  cached_timestamps = []

  try:
    with open(file_path, 'r', encoding='utf-8') as f:
      lines = f.readlines()

    if not lines:
      ui.notify('Файл пуст!', type='warning')
      return

    # Отсекаем служебную строку sep=; если она первая
    if lines[0].startswith('sep='):
      lines = lines[1:]

    reader = csv.reader(lines, delimiter=';')
    header = next(reader, None)

    if not header:
      ui.notify('Не удалось прочитать шапку таблицы!', type='warning')
      return

    idx_time = header.index('Timestamp') if 'Timestamp' in header else 0
    data_cols = [col.strip() for col in header if col.strip() and col.strip() != 'Timestamp']

    for col in data_cols:
      cached_data[col] = []

    for row_idx, row in enumerate(reader):
      if not row or len(row) <= idx_time:
        continue
      t_str = row[idx_time].split()[-1]
      cached_timestamps.append(t_str)

      for col in data_cols:
        c_idx = header.index(col)
        if len(row) > c_idx:
          try:
            cached_data[col].append(float(row[c_idx]))
          except ValueError:
            cached_data[col].append(0.0)
        else:
          cached_data[col].append(0.0)

    # По умолчанию выбираем AO_1 и AO_2
    selected_channels = [c for c in ['AO_1', 'AO_2'] if c in data_cols]
    if not selected_channels and data_cols:
      selected_channels = data_cols[:2]

    # Строим чекбоксы и принудительно рисуем график
    rebuild_channel_checkboxes(data_cols)
    render_chart()
    ui.notify(f'Загружено {len(cached_timestamps)} точек!', type='positive')

  except Exception as e:
    print(f'[ERROR] Ошибка загрузки лога: {e}')
    ui.notify(f'Ошибка чтения файла: {e}', type='negative')


def build_oscilloscope_panel():
  global active_chart, channels_container

  with ui.card().classes('bg-slate-800 p-3 w-full'):
    with ui.row().classes('w-full justify-between items-center mb-2'):
      ui.label('Офлайн-осциллограф (Анализ логов)').classes(
          'text-sm font-bold text-sky-400'
      )

      with ui.row().classes('items-center gap-2'):
        select_box = ui.row().classes('items-center')
        selected_file_holder = {'path': None}

        def rebuild_select_element():
          select_box.clear()
          files = get_log_files()
          selected_file_holder['path'] = files[0] if files else None

          with select_box:
            opts = {f: os.path.basename(f) for f in files} if files else {'': 'Нет логов'}
            cur_val = files[0] if files else ''

            def on_change(e):
              selected_file_holder['path'] = e.value

            ui.select(
                options=opts,
                value=cur_val,
                label='Лог-файл',
                on_change=on_change,
            ).props('dark outlined dense').classes('w-72 bg-slate-700 text-xs text-white rounded')

        rebuild_select_element()

        def on_refresh_click():
          rebuild_select_element()
          files = get_log_files()
          ui.notify(f'Найдено файлов: {len(files)}', type='info')

        ui.button('Обновить список', on_click=on_refresh_click).classes(
            'bg-slate-700 text-white text-xs py-1 px-2'
        )

        ui.button(
            'Открыть график',
            on_click=lambda: load_log_to_chart(selected_file_holder['path']),
        ).classes('bg-emerald-600 text-white text-xs font-bold py-1 px-3')

    # Холст графика
    active_chart = ui.echart({
        'animation': False,
        'tooltip': {'trigger': 'axis'},
        'xAxis': {'type': 'category', 'data': [], 'boundaryGap': False},
        'yAxis': {'type': 'value', 'scale': True},
        'series': [],
        'legend': {'data': [], 'textStyle': {'color': '#fff'}},
        'grid': {
            'left': '3%',
            'right': '4%',
            'bottom': '15%',
            'top': '15%',
            'containLabel': True,
        },
        'dataZoom': [
            {'type': 'slider', 'xAxisIndex': 0, 'start': 0, 'end': 100},
            {'type': 'inside', 'xAxisIndex': 0},
        ],
    }).classes('w-full h-80 bg-slate-900 rounded')

    channels_container = ui.grid(columns=6).classes(
        'w-full gap-1 bg-slate-700/30 p-2 rounded mt-2'
    )