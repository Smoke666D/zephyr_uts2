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

# Кэш прочитанного файла: { 'sensor_name': {'V': [...], 'I': [...], 'P': [...]} }
cached_sensors_data = {}
selected_sensors = []

# Имена датчиков тока стенда
CURRENT_SENSOR_NAMES = [
    'BOARD LOW',
    'BOARD HIGH',
    'Питание метки №2',
    'Питание метки №1',
    'Питание стенда',
    'Линия 3.3В стенда',
    'Питание тестируемого узла',
    'Линия 5В стенда',
]

SERIES_COLORS = ['#38bdf8', '#4ade80', '#facc15', '#f43f5e']


def get_log_files():
  os.makedirs(LOGS_DIR, exist_ok=True)
  pattern = os.path.join(LOGS_DIR, '*.csv')
  files = glob.glob(pattern)
  files.sort(key=os.path.getmtime, reverse=True)
  return files


def refresh_iv_logs_ui():
  global refresh_logs_callback
  if refresh_logs_callback:
    refresh_logs_callback()


def update_stats_panel():
  """Вычисляет базовую статистику ВАХ для выбранных датчиков (только U и I)"""
  global stats_container, selected_sensors, cached_sensors_data
  if not stats_container:
    return

  stats_container.clear()
  if not selected_sensors:
    return

  with stats_container:
    with ui.row().classes('w-full gap-2 items-center'):
      for idx, s_name in enumerate(selected_sensors):
        if s_name in cached_sensors_data:
          data = cached_sensors_data[s_name]
          v_arr = data['V']
          i_arr = data['I']
          if not v_arr or not i_arr:
            continue

          color = SERIES_COLORS[idx % len(SERIES_COLORS)]
          v_max = max(v_arr)
          i_max = max(i_arr)
          i_avg = sum(i_arr) / len(i_arr)
          v_avg = sum(v_arr) / len(v_arr)

          # Эквивалентное сопротивление R = U_avg / I_avg
          r_eq_str = (
              f'{v_avg / i_avg:.1f} Ом'
              if abs(i_avg) > 1e-4
              else '>100 кОм'
          )

          with ui.card().classes(
              'bg-slate-900/90 py-1.5 px-3 flex-1 rounded border'
              ' border-slate-700/80 shadow-md flex flex-col justify-between'
          ):
            with ui.row().classes('w-full justify-between items-center mb-1'):
              ui.label(s_name).style(f'color: {color};').classes(
                  'text-xs font-bold leading-none truncate max-w-[200px]'
              )
              ui.label(f'R_экв: {r_eq_str}').classes(
                  'text-[10px] font-mono text-slate-300'
              )

            with ui.row().classes(
                'w-full justify-between items-center text-[10px] font-mono'
                ' leading-none'
            ):
              ui.label(f'U_max: {v_max:.2f} В').classes('text-emerald-400')
              ui.label(f'I_max: {i_max:.4f} А').classes('text-sky-400')


def render_chart():
  """Перерисовывает график ВАХ (X: Напряжение В, Y: Ток А)"""
  global chart_container, selected_sensors, cached_sensors_data
  if not chart_container or not cached_sensors_data:
    return

  update_stats_panel()

  series = []
  legend = []

  for idx, s_name in enumerate(selected_sensors):
    if s_name in cached_sensors_data:
      color = SERIES_COLORS[idx % len(SERIES_COLORS)]
      legend.append(s_name)

      v_list = cached_sensors_data[s_name]['V']
      i_list = cached_sensors_data[s_name]['I']

      # Формируем двумерные точки: [Напряжение, Ток]
      points = list(zip(v_list, i_list))

      series.append({
          'name': s_name,
          'type': 'line',
          'data': points,
          'showSymbol': False,
          'sampling': 'lttb',
          'itemStyle': {'color': color},
          'lineStyle': {'width': 1.8},
      })

  chart_options = {
      'animation': False,
      'tooltip': {
          'trigger': 'axis',
          'formatter': (  # Кастомный тултип с показом U и I
              'function (params) {'
              '  if (!params || !params.length) return "";'
              '  let res = params[0].seriesName + "<br/>";'
              '  params.forEach(p => {'
              '    res += p.marker + " U: " + p.value[0].toFixed(2) + " В | I:'
              ' " + p.value[1].toFixed(4) + " А<br/>";'
              '  });'
              '  return res;'
              '}'
          ),
      },
      'xAxis': {
          'type': 'value',
          'name': 'Напряжение, В',
          'nameLocation': 'middle',
          'nameGap': 25,
          'scale': True,
          'axisLabel': {'color': '#94a3b8'},
          'splitLine': {'lineStyle': {'color': '#334155'}},
      },
      'yAxis': {
          'type': 'value',
          'name': 'Ток, А',
          'nameLocation': 'middle',
          'nameGap': 35,
          'scale': True,
          'axisLabel': {'color': '#94a3b8'},
          'splitLine': {'lineStyle': {'color': '#334155'}},
      },
      'series': series,
      'legend': {'data': legend, 'textStyle': {'color': '#fff'}},
      'grid': {
          'left': '6%',
          'right': '4%',
          'bottom': '18%',
          'top': '12%',
          'containLabel': True,
      },
      'dataZoom': [
          {'type': 'slider', 'xAxisIndex': 0, 'bottom': '5%'},
          {'type': 'slider', 'yAxisIndex': 0, 'left': '1%'},
          {'type': 'inside', 'xAxisIndex': 0},
          {'type': 'inside', 'yAxisIndex': 0},
      ],
  }

  chart_container.clear()
  with chart_container:
    ui.echart(chart_options).style('width: 100%; height: 350px;').classes(
        'rounded bg-slate-900'
    )


def rebuild_checkboxes():
  """Строит сетку из 8 чекбоксов с названиями датчиков тока"""
  global channels_container, selected_sensors
  if not channels_container:
    return

  channels_container.clear()
  with channels_container:
    ui.label(
        'Выберите датчики для построения ВАХ (до 4 одновременно):'
    ).classes('text-xs font-semibold text-yellow-300 col-span-full mb-1')

    for s_name in CURRENT_SENSOR_NAMES:
      is_default = s_name in selected_sensors

      def make_handler(name):

        def on_change(e):
          global selected_sensors
          if e.value:
            if len(selected_sensors) >= 4:
              e.sender.set_value(False)
              ui.notify(
                  'Максимум 4 характеристики одновременно!', type='warning'
              )
              return
            if name not in selected_sensors:
              selected_sensors.append(name)
          else:
            if name in selected_sensors:
              selected_sensors.remove(name)
          render_chart()

        return on_change

      ui.checkbox(
          s_name, value=is_default, on_change=make_handler(s_name)
      ).classes('text-white text-xs')


def load_log_for_iv(file_path):
  """Вычитывает лог и извлекает ТОЛЬКО пары U и I для каждого из 8 датчиков"""
  global cached_sensors_data, selected_sensors, log_info_label
  if not file_path or not os.path.exists(file_path):
    ui.notify('Файл лога не найден!', type='warning')
    return

  ui.notify(f'Загрузка ВАХ: {os.path.basename(file_path)}...', type='info')
  cached_sensors_data = {}

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
      return

    # Ищем ТОЛЬКО колонки напряжения (V) и тока (I)
    sensor_col_indices = {}
    for i in range(1, 9):
      s_name = CURRENT_SENSOR_NAMES[i - 1]

      col_v = (
          f'Cur_V_{i}'
          if f'Cur_V_{i}' in header
          else (f'{s_name} V' if f'{s_name} V' in header else None)
      )
      col_i = (
          f'Cur_I_{i}'
          if f'Cur_I_{i}' in header
          else (f'{s_name} I' if f'{s_name} I' in header else None)
      )

      idx_v = header.index(col_v) if col_v and col_v in header else -1
      idx_i = header.index(col_i) if col_i and col_i in header else -1

      if idx_v != -1 and idx_i != -1:
        sensor_col_indices[s_name] = {'V': idx_v, 'I': idx_i}
        cached_sensors_data[s_name] = {'V': [], 'I': []}

    total_rows = 0
    for row in reader:
      if not row:
        continue
      total_rows += 1
      for s_name, indices in sensor_col_indices.items():
        try:
          v = float(row[indices['V']])
          i = float(row[indices['I']])
          cached_sensors_data[s_name]['V'].append(v)
          cached_sensors_data[s_name]['I'].append(i)
        except (ValueError, IndexError):
          continue

    if log_info_label:
      log_info_label.set_text(
          f'Файл: {os.path.basename(file_path)} | Точек: {total_rows}'
      )

    if not selected_sensors:
      selected_sensors = [CURRENT_SENSOR_NAMES[0]]

    rebuild_checkboxes()
    render_chart()
    ui.notify(f'ВАХ построена: {total_rows} точек!', type='positive')

  except Exception as e:
    print(f'[IV ERROR]: {e}')
    ui.notify(f'Ошибка построения ВАХ: {e}', type='negative')


def build_iv_curve_panel():
  """Строит панель офлайн-анализа вольт-амперных характеристик"""
  global chart_container, channels_container, stats_container, refresh_logs_callback, log_info_label

  with ui.card().classes('bg-slate-800 p-3 w-full'):
    with ui.row().classes('w-full justify-between items-center mb-2'):
      with ui.row().classes('items-center gap-2'):
        ui.label('Вольт-амперные характеристики (ВАХ)').classes(
            'text-sm font-bold text-sky-400'
        )
        log_info_label = ui.label('').classes(
            'text-xs font-mono text-emerald-400 bg-slate-900 px-2 py-0.5'
            ' rounded border border-slate-700'
        )

      with ui.row().classes('items-center gap-2'):
        select_box = ui.row().classes('items-center')
        current_select = {'widget': None}

        def rebuild_select():
          select_box.clear()
          files = get_log_files()
          with select_box:
            opts = (
                {f: os.path.basename(f) for f in files}
                if files
                else {'': 'Нет логов'}
            )
            first_val = files[0] if files else None
            sel = (
                ui.select(options=opts, value=first_val, label='Лог-файл')
                .props('dark outlined dense')
                .classes('w-72 bg-slate-700 text-xs text-white rounded')
            )
            current_select['widget'] = sel

        refresh_logs_callback = rebuild_select
        rebuild_select()

        def on_refresh():
          rebuild_select()
          files = get_log_files()
          ui.notify(f'Найдено файлов: {len(files)}', type='info')

        ui.button('Обновить список', on_click=on_refresh).classes(
            'bg-slate-700 text-white text-xs py-1 px-2'
        )

        def on_open():
          sel_w = current_select.get('widget')
          t_path = sel_w.value if sel_w else None
          if t_path and os.path.exists(t_path):
            load_log_for_iv(t_path)
          else:
            ui.notify('Выберите существующий лог-файл!', type='warning')

        ui.button('Построить ВАХ', on_click=on_open).classes(
            'bg-emerald-600 text-white text-xs font-bold py-1 px-3'
        )

    # Панель статистики над графиком
    stats_container = ui.row().classes('w-full mb-1.5')

    # Контейнер для графика ECharts
    chart_container = ui.column().classes('w-full')
    with chart_container:
      ui.label('Выберите лог-файл и нажмите "Построить ВАХ"').classes(
          'text-xs text-slate-400 italic py-10 self-center'
      )

    # Сетка из 8 чекбоксов датчиков тока (4 колонки x 2 ряда)
    channels_container = ui.grid(columns=4).classes(
        'w-full gap-1.5 bg-slate-700/30 p-2.5 rounded mt-2'
    )