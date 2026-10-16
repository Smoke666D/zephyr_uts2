import csv
from datetime import datetime
import glob
import os
from nicegui import ui

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

chart_container = None
channels_container = None
refresh_logs_callback = None
log_info_label = None

# Кэш прочитанного файла: { 'col_name': [values...] } и список временных меток
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


def refresh_logs_ui():
  global refresh_logs_callback
  if refresh_logs_callback:
    refresh_logs_callback()


def render_chart():
  """Пересоздает элемент графика внутри chart_container на основе выбранных каналов"""
  global chart_container, selected_channels, cached_data, cached_timestamps
  if not chart_container or not cached_timestamps:
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
          'sampling': 'lttb',
          'itemStyle': {'color': color},
          'lineStyle': {'width': 1.5},
      })

  chart_options = {
      'animation': False,
      'tooltip': {'trigger': 'axis'},
      'xAxis': {'type': 'category', 'data': cached_timestamps, 'boundaryGap': False},
      'yAxis': {'type': 'value', 'scale': True},
      'series': series,
      'legend': {'data': legend, 'textStyle': {'color': '#fff'}},
      'grid': {
          'left': '4%',
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

  chart_container.clear()
  with chart_container:
    ui.echart(chart_options).style('width: 100%; height: 320px;').classes('rounded bg-slate-900')


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
  global cached_data, cached_timestamps, selected_channels, log_info_label
  if not file_path or not os.path.exists(file_path):
    ui.notify('Файл не выбран или не существует!', type='warning')
    return

  ui.notify(f'Загрузка: {os.path.basename(file_path)}...', type='info')

  cached_data = {}
  cached_timestamps = []
  raw_full_timestamps = []

  try:
    with open(file_path, 'r', encoding='utf-8') as f:
      lines = f.readlines()

    if not lines:
      ui.notify('Файл пуст!', type='warning')
      return

    if lines[0].startswith('sep='):
      lines = lines[1:]

    reader = csv.reader(lines, delimiter=';')
    header = next(reader, None)

    if not header:
      ui.notify('Не удалось прочитать заголовок таблицы!', type='warning')
      return

    idx_time = header.index('Timestamp') if 'Timestamp' in header else 0
    data_cols = [col.strip() for col in header if col.strip() and col.strip() != 'Timestamp']

    for col in data_cols:
      cached_data[col] = []

    for row_idx, row in enumerate(reader):
      if not row or len(row) <= idx_time:
        continue
      full_t_str = row[idx_time].strip()
      raw_full_timestamps.append(full_t_str)

      # Для подписи оси X берем только время (ЧЧ:ММ:СС.мс)
      t_short = full_t_str.split()[-1] if len(full_t_str.split()) > 1 else full_t_str
      cached_timestamps.append(t_short)

      for col in data_cols:
        c_idx = header.index(col)
        if len(row) > c_idx:
          try:
            cached_data[col].append(float(row[c_idx]))
          except ValueError:
            cached_data[col].append(0.0)
        else:
          cached_data[col].append(0.0)

    # --- ВЫЧИСЛЕНИЕ ДЛИТЕЛЬНОСТИ ЛОГА ---
    duration_str = '00:00.000'
    if raw_full_timestamps:
      try:
        t_start = datetime.strptime(raw_full_timestamps[0], '%Y-%m-%d %H:%M:%S.%f')
        t_end = datetime.strptime(raw_full_timestamps[-1], '%Y-%m-%d %H:%M:%S.%f')
        delta = t_end - t_start
        total_seconds = delta.total_seconds()
        mins = int(total_seconds // 60)
        secs = int(total_seconds % 60)
        millis = int((total_seconds - int(total_seconds)) * 1000)
        duration_str = f'{mins:02d}:{secs:02d}.{millis:03d}'
      except Exception:
        # Резервный расчет по количеству точек при 1 кГц (1 точка = 1 мс)
        total_ms = len(raw_full_timestamps)
        mins = total_ms // 60000
        secs = (total_ms % 60000) // 1000
        millis = total_ms % 1000
        duration_str = f'{mins:02d}:{secs:02d}.{millis:03d}'

    # Обновляем инфо-метку на экране
    if log_info_label:
      log_info_label.set_text(f'Длительность: {duration_str} | Точек: {len(cached_timestamps)}')

    selected_channels = [c for c in ['AO_1', 'AO_2'] if c in data_cols]
    if not selected_channels and data_cols:
      selected_channels = data_cols[:2]

    rebuild_channel_checkboxes(data_cols)
    render_chart()
    ui.notify(f'Загружено {len(cached_timestamps)} точек ({duration_str})', type='positive')

  except Exception as e:
    print(f'[ERROR] Ошибка загрузки лога: {e}')
    ui.notify(f'Ошибка чтения файла: {e}', type='negative')


def build_oscilloscope_panel():
  global chart_container, channels_container, refresh_logs_callback, log_info_label

  with ui.card().classes('bg-slate-800 p-3 w-full'):
    with ui.row().classes('w-full justify-between items-center mb-2'):
      with ui.row().classes('items-center gap-2'):
        ui.label('Офлайн-осциллограф (Анализ логов)').classes(
            'text-sm font-bold text-sky-400'
        )
        # Виджет вывода длительности и объема точек
        log_info_label = ui.label('').classes(
            'text-xs font-mono text-emerald-400 bg-slate-900 px-2 py-0.5 rounded border border-slate-700'
        )

      with ui.row().classes('items-center gap-2'):
        select_box = ui.row().classes('items-center')
        current_select = {'widget': None}

        def rebuild_select_element():
          select_box.clear()
          files = get_log_files()

          with select_box:
            opts = {f: os.path.basename(f) for f in files} if files else {'': 'Нет логов'}
            first_val = files[0] if files else None

            sel = ui.select(
                options=opts,
                value=first_val,
                label='Лог-файл',
            ).props('dark outlined dense').classes('w-72 bg-slate-700 text-xs text-white rounded')

            current_select['widget'] = sel

        refresh_logs_callback = rebuild_select_element
        rebuild_select_element()

        def on_refresh_click():
          rebuild_select_element()
          files = get_log_files()
          ui.notify(f'Найдено файлов: {len(files)}', type='info')

        ui.button('Обновить список', on_click=on_refresh_click).classes(
            'bg-slate-700 text-white text-xs py-1 px-2'
        )

        def on_open_click():
          sel_widget = current_select.get('widget')
          target_path = sel_widget.value if sel_widget else None
          if target_path and os.path.exists(target_path):
            load_log_to_chart(target_path)
          else:
            ui.notify('Выберите корректный лог-файл из списка!', type='warning')

        ui.button(
            'Открыть график',
            on_click=on_open_click,
        ).classes('bg-emerald-600 text-white text-xs font-bold py-1 px-3')

    # Выделенный контейнер под график
    chart_container = ui.column().classes('w-full')
    with chart_container:
      ui.label('Выберите лог-файл и нажмите "Открыть график"').classes(
          'text-xs text-slate-400 italic py-10 self-center'
      )

    channels_container = ui.grid(columns=6).classes(
        'w-full gap-1 bg-slate-700/30 p-2 rounded mt-2'
    )