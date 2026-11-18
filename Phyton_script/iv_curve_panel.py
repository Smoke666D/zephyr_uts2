import csv
import glob
import os
import threading
import time
import traceback
from nicegui import ui
from globals_data import telemetry_data_cache

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
IV_LOGS_DIR = os.path.join(BASE_DIR, 'logs_iv')

chart_container = None
log_info_label = None

# Общее потокобезопасное состояние для связи рабочего потока и UI-таймера
scan_state = {
    'running': False,
    'progress': 0.0,
    'status_text': 'Готов к запуску измерения',
    'finished_ready': False,
    'last_log_path': None,
    'v_points': [],
    'i_points': [],
    'error_msg': None,
}

stop_event = threading.Event()


def get_iv_log_files():
    os.makedirs(IV_LOGS_DIR, exist_ok=True)
    pattern = os.path.join(IV_LOGS_DIR, '*.csv')
    files = glob.glob(pattern)
    files.sort(key=os.path.getmtime, reverse=True)
    return files


def render_iv_chart(v_data, i_data, title_name='Питание тестируемого узла'):
    global chart_container
    if not chart_container:
        return

    points = list(zip(v_data, i_data))
    points.sort(key=lambda p: p[0])

    chart_options = {
        'animation': False,
        'tooltip': {
            'trigger': 'axis',
            'formatter': (
                'function (params) {'
                '  if (!params || !params.length) return "";'
                '  let p = params[0].value;'
                '  return "Напряжение: " + p[0].toFixed(2) + " В<br/>Ток: " + p[1].toFixed(4) + " А";'
                '}'
            ),
        },
        'xAxis': {
            'type': 'value',
            'name': 'U, В',
            'nameLocation': 'middle',
            'nameGap': 25,
            'scale': True,
            'axisLabel': {'color': '#94a3b8'},
            'splitLine': {'lineStyle': {'color': '#334155'}},
        },
        'yAxis': {
            'type': 'value',
            'name': 'I, А',
            'nameLocation': 'middle',
            'nameGap': 35,
            'scale': True,
            'axisLabel': {'color': '#94a3b8'},
            'splitLine': {'lineStyle': {'color': '#334155'}},
        },
        'series': [{
            'name': title_name,
            'type': 'line',
            'data': points,
            'showSymbol': True,
            'symbolSize': 4,
            'itemStyle': {'color': '#38bdf8'},
            'lineStyle': {'width': 2},
        }],
        'grid': {
            'left': '8%',
            'right': '4%',
            'bottom': '18%',
            'top': '10%',
            'containLabel': True,
        },
        'dataZoom': [
            {'type': 'slider', 'xAxisIndex': 0, 'bottom': '4%'},
            {'type': 'slider', 'yAxisIndex': 0, 'left': '1%'},
            {'type': 'inside', 'xAxisIndex': 0},
            {'type': 'inside', 'yAxisIndex': 0},
        ],
    }

    chart_container.clear()
    with chart_container:
        ui.echart(chart_options).style('width: 100%; height: 360px;').classes('rounded bg-slate-900')


def load_iv_log_file(file_path):
    global log_info_label
    if not file_path or not os.path.exists(file_path):
        ui.notify('Файл лога ВАХ не найден!', type='warning')
        return

    v_list = []
    i_list = []

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        if not lines:
            return
        if lines[0].startswith('sep='):
            lines = lines[1:]

        reader = csv.reader(lines, delimiter=';')
        header = next(reader, None)
        if not header:
            return

        idx_v = header.index('Voltage_V') if 'Voltage_V' in header else 2
        idx_i = header.index('Current_A') if 'Current_A' in header else 3

        for row in reader:
            if len(row) > max(idx_v, idx_i):
                try:
                    v_list.append(float(row[idx_v]))
                    i_list.append(float(row[idx_i]))
                except ValueError:
                    continue

        if log_info_label:
            log_info_label.set_text(f'{os.path.basename(file_path)}: {len(v_list)} точек')

        render_iv_chart(v_list, i_list, f'ВАХ ({os.path.basename(file_path)})')
        ui.notify(f'Загружен лог ВАХ: {len(v_list)} точек', type='positive')
    except Exception as e:
        ui.notify(f'Ошибка чтения лога ВАХ: {e}', type='negative')


def build_iv_curve_panel(ports_refs=None):
    global chart_container, log_info_label
    shell_ref = ports_refs['shell'] if ports_refs and 'shell' in ports_refs else None

    with ui.card().classes('bg-slate-800 p-3 w-full'):
        ui.label('Построитель вольт-амперных характеристик (ВАХ)').classes(
            'text-sm font-bold text-yellow-300 text-center w-full mb-1 tracking-wide'
        )
        ui.label('Датчик: Питание тестируемого узла (канал 7)').classes(
            'text-[10px] text-slate-400 text-center w-full mb-2'
        )

        # --- СЕТКА ПАРАМЕТРОВ СКАНИРОВАНИЯ (По умолчанию: 4095 -> 2000 -> 0) ---
        with ui.grid(columns=6).classes('w-full gap-2 items-center bg-slate-700/40 p-2 rounded border border-slate-600 mb-2'):
            with ui.column().classes('gap-0'):
                ui.label('Начальный DAC:').classes('text-[10px] text-slate-300')
                dac_start_in = ui.input(value='4095').props('dark outlined dense type=number min=0 max=4095 input-class="text-xs font-mono"')

            with ui.column().classes('gap-0'):
                ui.label('Точка перелома:').classes('text-[10px] text-yellow-300 font-semibold')
                dac_break_in = ui.input(value='2000').props('dark outlined dense type=number min=0 max=4095 input-class="text-xs font-mono"')

            with ui.column().classes('gap-0'):
                ui.label('Конечный DAC:').classes('text-[10px] text-slate-300')
                dac_end_in = ui.input(value='0').props('dark outlined dense type=number min=0 max=4095 input-class="text-xs font-mono"')

            with ui.column().classes('gap-0'):
                ui.label('Шаг зоны 1:').classes('text-[10px] text-sky-300')
                step1_in = ui.input(value='20').props('dark outlined dense type=number min=1 max=1000 input-class="text-xs font-mono"')

            with ui.column().classes('gap-0'):
                ui.label('Шаг зоны 2:').classes('text-[10px] text-emerald-300')
                step2_in = ui.input(value='50').props('dark outlined dense type=number min=1 max=1000 input-class="text-xs font-mono"')

            with ui.column().classes('gap-0'):
                ui.label('Время шага, мс:').classes('text-[10px] text-slate-300')
                step_time_in = ui.input(value='50').props('dark outlined dense type=number min=5 max=5000 input-class="text-xs font-mono"')

        # --- БЛОК ПРОГРЕСС-БАРА И СТАТУСА ---
        with ui.card().classes('w-full bg-slate-900/80 p-2.5 rounded border border-slate-700 flex flex-col gap-1.5 mb-2'):
            progress_bar = ui.linear_progress(value=0.0, show_value=False).props(
                'color=sky-400 track-color=slate-800 instant-feedback'
            ).classes('w-full rounded h-3')
            progress_label = ui.label('Готов к запуску измерения').classes(
                'text-xs font-mono text-emerald-400 text-center w-full leading-tight font-bold'
            )

        # --- РАБОЧИЙ ПОТОК СКАНИРОВАНИЯ (В ОТДЕЛЬНОМ THREAD) ---
        def _scan_thread_worker(curr_shell, start_dac, break_dac, end_dac, s1, s2, dt_ms, log_path):
            v_pts = []
            i_pts = []
            span_dac = max(1, abs(end_dac - start_dac))
            is_decreasing = start_dac > end_dac
            delay_sec = dt_ms / 1000.0

            try:
                with open(log_path, 'w', newline='', encoding='utf-8') as f:
                    f.write('sep=;\n')
                    f.write('Timestamp;DAC_Code;Voltage_V;Current_A\n')

                    curr_dac = start_dac
                    while not stop_event.is_set():
                        # 1. Отправляем команду DAC
                        cmd = f'SOUR:DAC {curr_dac}\r\n'.encode('ascii')
                        curr_shell.write(cmd)

                        # 2. Ждем время установления шага
                        time.sleep(delay_sec)

                        # 3. Берем свежие показания датчика №7 из кэша телеметрии
                        v_val = float(telemetry_data_cache.get('curr_v_7', 0.0))
                        i_val = float(telemetry_data_cache.get('curr_i_7', 0.0))

                        v_pts.append(v_val)
                        i_pts.append(i_val)

                        # 4. Обновляем общее состояние для UI-таймера
                        pct = min(1.0, max(0.0, abs(curr_dac - start_dac) / span_dac))
                        if is_decreasing:
                            zone_str = 'Зона 1' if curr_dac > break_dac else 'Зона 2'
                        else:
                            zone_str = 'Зона 1' if curr_dac < break_dac else 'Зона 2'

                        scan_state['progress'] = pct
                        scan_state['status_text'] = (
                            f'{zone_str} | DAC: {curr_dac} → {end_dac} ({int(pct * 100)}%) | '
                            f'U: {v_val:.2f} В | I: {i_val:.4f} А | Точек: {len(v_pts)}'
                        )

                        # 5. Записываем строку в лог
                        t_now = time.strftime('%H:%M:%S.') + f'{int((time.time() % 1) * 1000):03d}'
                        f.write(f'{t_now};{curr_dac};{v_val:.4f};{i_val:.6f}\n')

                        if curr_dac == end_dac:
                            break

                        # 6. Шагаем к следующей точке (поддержка убывания и возрастания)
                        if is_decreasing:
                            cur_step = s1 if curr_dac > break_dac else s2
                            curr_dac -= cur_step
                            if curr_dac < end_dac:
                                curr_dac = end_dac
                        else:
                            cur_step = s1 if curr_dac < break_dac else s2
                            curr_dac += cur_step
                            if curr_dac > end_dac:
                                curr_dac = end_dac

                # По завершении сбрасываем DAC в 4095
                try:
                    curr_shell.write(b'SOUR:DAC 4095\r\n')
                except Exception:
                    pass

                if not stop_event.is_set():
                    scan_state['progress'] = 1.0
                    scan_state['status_text'] = f'Измерение завершено! Сохранено {len(v_pts)} точек (DAC сброшен в 4095).'
                    scan_state['v_points'] = v_pts
                    scan_state['i_points'] = i_pts
                    scan_state['last_log_path'] = log_path
                    scan_state['finished_ready'] = True
                else:
                    scan_state['status_text'] = f'Остановлено пользователем (снято {len(v_pts)} точек, DAC = 4095)'

            except Exception as ex:
                traceback.print_exc()
                scan_state['error_msg'] = str(ex)
                scan_state['status_text'] = f'Ошибка: {ex}'
            finally:
                scan_state['running'] = False

        # --- ТАЙМЕР СИНХРОНИЗАЦИИ UI ---
        def ui_sync_callback():
            progress_bar.value = scan_state['progress']
            progress_label.set_text(scan_state['status_text'])

            if scan_state['error_msg']:
                err = scan_state['error_msg']
                scan_state['error_msg'] = None
                ui.notify(f'Ошибка сканирования: {err}', type='negative')

            if scan_state['finished_ready']:
                scan_state['finished_ready'] = False
                path = scan_state['last_log_path']
                v_p = scan_state['v_points']
                i_p = scan_state['i_points']
                ui.notify(f'ВАХ построена ({len(v_p)} точек)!', type='positive')
                render_iv_chart(v_p, i_p, f'ВАХ ({os.path.basename(path)})')
                rebuild_log_select(selected_file=path)

            if scan_state['running']:
                if btn_start.text != 'СТОП':
                    btn_start.text = 'СТОП'
                    btn_start.classes(remove='bg-emerald-600 hover:bg-emerald-500', add='bg-red-600 hover:bg-red-500')
            else:
                if btn_start.text != 'СТАРТ ВАХ':
                    btn_start.text = 'СТАРТ ВАХ'
                    btn_start.classes(remove='bg-red-600 hover:bg-red-500', add='bg-emerald-600 hover:bg-emerald-500')

        ui.timer(0.1, ui_sync_callback)

        def parse_int_field(inp, default_val):
            v = inp.value
            if v is None or str(v).strip() == '':
                return default_val
            return int(float(v))

        def toggle_scan():
            if not scan_state['running']:
                if not shell_ref:
                    ui.notify('Ошибка: shell_ref не подключен!', type='negative')
                    return
                curr_shell = shell_ref.get('ser')
                if not curr_shell or not getattr(curr_shell, 'is_open', False):
                    ui.notify('SCPI порт закрыт! Сначала откройте порт SCPI.', type='warning')
                    return

                try:
                    start_dac = parse_int_field(dac_start_in, 4095)
                    break_dac = parse_int_field(dac_break_in, 2000)
                    end_dac = parse_int_field(dac_end_in, 0)
                    s1 = parse_int_field(step1_in, 20)
                    s2 = parse_int_field(step2_in, 50)
                    dt_ms = float(step_time_in.value if step_time_in.value not in (None, '') else 50.0)

                    is_dec = start_dac >= break_dac >= end_dac
                    is_inc = start_dac <= break_dac <= end_dac

                    if not (0 <= min(start_dac, break_dac, end_dac) and max(start_dac, break_dac, end_dac) <= 4095 and (is_dec or is_inc)):
                        ui.notify('Точка перелома должна лежать между Начальным и Конечным DAC (0..4095)!', type='negative')
                        return
                    if s1 <= 0 or s2 <= 0 or dt_ms <= 0:
                        ui.notify('Шаги и время должны быть > 0!', type='negative')
                        return
                except Exception as e:
                    ui.notify(f'Ошибка ввода параметров: {e}', type='negative')
                    return

                os.makedirs(IV_LOGS_DIR, exist_ok=True)
                ts_str = time.strftime('%Y-%m-%d_%H-%M-%S')
                log_path = os.path.join(IV_LOGS_DIR, f'iv_curve_{ts_str}.csv')

                stop_event.clear()
                scan_state['running'] = True
                scan_state['progress'] = 0.0
                scan_state['finished_ready'] = False
                scan_state['error_msg'] = None
                scan_state['status_text'] = f'Старт... DAC: {start_dac}'

                ui.notify(f'Запущено снятие ВАХ -> {os.path.basename(log_path)}', type='info')

                threading.Thread(
                    target=_scan_thread_worker,
                    args=(curr_shell, start_dac, break_dac, end_dac, s1, s2, dt_ms, log_path),
                    daemon=True,
                ).start()
            else:
                stop_event.set()
                ui.notify('Остановка сканирования...', type='warning')

        # --- СТРОКА УПРАВЛЕНИЯ И ЗАГРУЗКИ ФАЙЛОВ ---
        with ui.row().classes('w-full justify-between items-center my-1'):
            with ui.row().classes('items-center gap-2'):
                btn_start = ui.button('СТАРТ ВАХ', on_click=toggle_scan).classes(
                    'bg-emerald-600 hover:bg-emerald-500 text-white font-bold text-xs py-1.5 px-4 rounded shadow'
                )
                log_info_label = ui.label('').classes('text-xs font-mono text-sky-400')

            with ui.row().classes('items-center gap-2'):
                select_box = ui.row().classes('items-center')

                def rebuild_log_select(selected_file=None):
                    select_box.clear()
                    files = get_iv_log_files()
                    with select_box:
                        opts = {f: os.path.basename(f) for f in files} if files else {'': 'Нет логов ВАХ'}
                        val = selected_file if (selected_file and selected_file in files) else (files[0] if files else None)
                        ui.select(
                            options=opts,
                            value=val,
                            label='История ВАХ',
                            on_change=lambda e: load_iv_log_file(e.value),
                        ).props('dark outlined dense').classes('w-64 bg-slate-700 text-xs text-white rounded')

                rebuild_log_select()

                ui.button('Обновить', on_click=rebuild_log_select).classes('bg-slate-700 text-white text-xs py-1 px-2')

        # --- ХОЛСТ ГРАФИКА ВАХ ---
        chart_container = ui.column().classes('w-full')
        with chart_container:
            ui.label('Задайте параметры и нажмите "СТАРТ ВАХ" или выберите лог из истории').classes(
                'text-xs text-slate-400 italic py-12 self-center'
            )