import csv
import glob
import os
from nicegui import ui

active_chart = None
log_select = None
channels_container = None

# Кэш прочитанного файла: { 'col_name': [values...] } и список временных меток
cached_data = {}
cached_timestamps = []
selected_channels = []

SERIES_COLORS = ['#38bdf8', '#4ade80', '#facc15', '#f43f5e', '#c084fc', '#fb923c']


def get_log_files():
  files = glob.glob('logs/*.csv')
  files.sort(key=os.path.getmtime, reverse=True)
  return files if files else ['Нет файлов логов']


def render_chart():
  """Перерисовывает график на основе выбранных каналов из кэша"""
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

  active_chart.options['xAxis']['data'] = cached_timestamps
  active_chart.options['series'] = series
  active_chart.options['legend']['data'] = legend
  active_chart.update()


def rebuild_channel_checkboxes(columns):
  """Строит сетку чекбоксов для выбора каналов"""
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
  if not file_path or file_path == 'Нет файлов логов' or not os.path.exists(file_path):
    ui.notify('Файл лога не найден!', type='warning')
    return

  ui.notify(f'Загрузка: {os.path.basename(file_path)}...', type='info')

  cached_data = {}
  cached_timestamps = []

  try:
    with open(file_path, 'r', encoding='utf-8') as f:
      first_line = f.readline()
      if not first_line.startswith('sep='):
        f.seek(0)

      reader = csv.reader(f, delimiter=';')
      header = next(reader, None)
      if not header:
        ui.notify('Файл пуст!', type='warning')
        return

      idx_time = header.index('Timestamp') if 'Timestamp' in header else 0
      data_cols = [col for col in header if col != 'Timestamp']

      # Инициализируем списки под каждую колонку
      for col in data_cols:
        cached_data[col] = []

      for row_idx, row in enumerate(reader):
        if not row:
          continue
        # Время
        t_str = row[idx_time].split()[-1] if len(row) > idx_time else str(row_idx)
        cached_timestamps.append(t_str)

        # Значения колонок
        for col in data_cols:
          c_idx = header.index(col)
          if len(row) > c_idx:
            try:
              cached_data[col].append(float(row[c_idx]))
            except ValueError:
              cached_data[col].append(0.0)
          else:
            cached_data[col].append(0.0)

    # По умолчанию выбираем AO_1 и AO_2, если они есть
    selected_channels = [c for c in ['AO_1', 'AO_2'] if c in data_cols]
    if not selected_channels and data_cols:
      selected_channels = data_cols[:2]

    # Перерисовываем чекбоксы и график
    rebuild_channel_checkboxes(data_cols)
    render_chart()
    ui.notify(f'Успешно загружено {len(cached_timestamps)} точек!', type='positive')

  except Exception as e:
    ui.notify(f'Ошибка чтения файла: {e}', type='negative')


def build_oscilloscope_panel():
  global active_chart, log_select, channels_container

  with ui.card().classes('bg-slate-800 p-3 w-full'):
    with ui.row().classes('w-full justify-between items-center mb-2'):
      ui.label('Офлайн-осциллограф (Анализ логов)').classes(
          'text-sm font-bold text-sky-400'
      )

      with ui.row().classes('items-center gap-2'):
        file_list = get_log_files()

        def refresh_file_list():
          files = get_log_files()
          log_select.options = files
          if files:
            log_select.value = files[0]
          ui.notify('Список логов обновлен', type='info')

        log_select = ui.select(
            options=file_list,
            value=file_list[0] if file_list else None,
            label='Выберите лог-файл',
        ).props('dark outlined dense').classes('w-64 bg-slate-700 text-xs text-white rounded')

        ui.button('Обновить список', on_click=refresh_file_list).classes(
            'bg-slate-700 text-white text-xs py-1 px-2'
        )

        ui.button(
            'Открыть график',
            on_click=lambda: load_log_to_chart(log_select.value),
        ).classes('bg-emerald-600 text-white text-xs font-bold py-1 px-3')

    # График с возможностью зума
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

    # Контейнер для чекбоксов каналов (появляется после открытия файла)
    channels_container = ui.grid(columns=6).classes(
        'w-full gap-1 bg-slate-700/30 p-2 rounded mt-2'
    )