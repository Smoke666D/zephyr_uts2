import csv
import glob
import os
from nicegui import ui

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

chart_container = None
channels_container = None
stats_container = None
refresh_logs_callback = None
log_info_label = None

# Кэш прочитанного файла: { 'col_name': [values...] } и список временных меток
cached_data = {}
cached_timestamps = []
selected_channels = []

SERIES_COLORS = ['#38bdf8', '#4ade80', '#facc15', '#f43f5e', '#c084fc', '#fb923c']


def update_oscilloscope_data(*args, **kwargs):
  """Заглушка для совместимости с внешними модулями"""
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


def update_stats_panel():
  """Вычисляет среднее, мин и макс с процентами отклонения и выводит над графиком"""
  global stats_container, selected_channels, cached_data
  if not stats_container:
    return

  stats_container.clear()
  if not selected_channels:
    return

  with stats_container:
    with ui.row().classes('w-full gap-2 items-center'):
      for idx, col_name in enumerate(selected_channels):
        if col_name in cached_data and cached_data[col_name]:
          vals = cached_data[col_name]
          color = SERIES_COLORS[idx % len(SERIES_COLORS)]

          v_avg = sum(vals) / len(vals)
          v_max = max(vals)
          v_min = min(vals)

          # Расчет отклонений от среднего в процентах
          if abs(v_avg) > 1e-9:
            dev_max_pct = ((v_max - v_avg) / abs(v_avg)) * 100.0
            dev_min_pct = ((v_min - v_avg) / abs(v_avg)) * 100.0
            str_dev_max = f'{dev_max_pct:+.1f}%'
            str_dev_min = f'{dev_min_pct:+.1f}%'
          else:
            str_dev_max = '0.0%'
            str_dev_min = '0.0%'

          with ui.card().classes(
              'bg-slate-900/90 py-1.5 px-3 flex-1 rounded border border-slate-700/80 shadow-md flex flex-col justify-between'
          ):
            with ui.row().classes('w-full justify-between items-center mb-1'):
              ui.label(col_name).style(f'color: {color};').classes(
                  'text-xs font-bold leading-none truncate'
              )
              ui.label(f'Avg: {v_avg:.3f}').classes(
                  'text-[11px] font-mono text-slate-200 font-semibold'
              )

            with ui.row().classes('w-full justify-between items-center text-[10px] font-mono leading-none'):
              with ui.row().classes('items-center gap-1'):
                ui.label('Max:').classes('text-slate-400')
                ui.label(f'{v_max:.3f}').classes('text-emerald-400 font-bold')
                ui.label(f'({str_dev_max})').classes('text-emerald-500/80 text-[9px]')

              with ui.row().classes('items-center gap-1'):
                ui.label('Min:').classes('text-slate-400')
                ui.label(f'{v_min:.3f}').classes('text-rose-400 font-bold')
                ui.label(f'({str_dev_min})').classes('text-rose-500/80 text-[9px]')


def render_chart():
  """Пересоздает элемент графика внутри chart_container на основе выбранных каналов"""
  global chart_container, selected_channels, cached_data, cached_timestamps
  if not chart_container or not cached_timestamps:
    return

  update_stats_panel()

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
  mcu_ms_timestamps = []

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

    # Определение индексов временных колонок
    idx_time = 0
    if 'Time' in header:
      idx_time = header.index('Time')
    elif 'Timestamp' in header:
      idx_time = header.index('Timestamp')

    idx_mcu_ms = header.index('MCU_Time_ms') if 'MCU_Time_ms' in header else -1

    # Исключаем служебные временные колонки из параметров для построения графиков
    data_cols = [
        col.strip()
        for col in header
        if col.strip() and col.strip() not in ('Time', 'Timestamp', 'MCU_Time_ms')
    ]

    for col in data_cols:
      cached_data[col] = []

    for row_idx, row in enumerate(reader):
      if not row or len(row) <= idx_time:
        continue

      t_str = row[idx_time].strip()
      cached_timestamps.append(t_str)

      if idx_mcu_ms != -1 and len(row) > idx_mcu_ms:
        try:
          mcu_ms_timestamps.append(int(row[idx_mcu_ms]))
        except ValueError:
          pass

      for col in data_cols:
        c_idx = header.index(col)
        if len(row) > c_idx:
          try:
            cached_data[col].append(float(row[c_idx]))
          except ValueError:
            cached_data[col].append(0.0)
        else:
          cached_data[col].append(0.0)

    # Расчет точной длительности по реальному таймстампу MCU
    duration_str = '00:00.000'
    if mcu_ms_timestamps:
      total_ms = mcu_ms_timestamps[-1] - mcu_ms_timestamps[0]
      mins = (total_ms // 1000) // 60
      secs = (total_ms // 1000) % 60
      millis = total_ms % 1000
      duration_str = f'{mins:02d}:{secs:02d}.{millis:03d}'
    elif cached_timestamps:
      total_ms = len(cached_timestamps)
      mins = total_ms // 60000
      secs = (total_ms % 60000) // 1000
      millis = total_ms % 1000
      duration_str = f'{mins:02d}:{secs:02d}.{millis:03d}'

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
  global chart_container, channels_container, stats_container, refresh_logs_callback, log_info_label

  with ui.card().classes('bg-slate-800 p-3 w-full'):
    # Шапка управления
    with ui.row().classes('w-full justify-between items-center mb-2'):
      with ui.row().classes('items-center gap-2'):
        ui.label('Офлайн-осциллограф (Анализ логов)').classes(
            'text-sm font-bold text-sky-400'
        )
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

    # Панель статистических карточек над графиком
    stats_container = ui.row().classes('w-full mb-1.5')

    # Контейнер для холста графика
    chart_container = ui.column().classes('w-full')
    with chart_container:
      ui.label('Выберите лог-файл и нажмите "Открыть график"').classes(
          'text-xs text-slate-400 italic py-10 self-center'
      )

    # Контейнер для чекбоксов каналов
    channels_container = ui.grid(columns=6).classes(
        'w-full gap-1 bg-slate-700/30 p-2 rounded mt-2'
    )