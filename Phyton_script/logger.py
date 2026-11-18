from collections import deque
from datetime import datetime
import os
import threading
import time

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

# =========================================================================
# СПРАВОЧНИК ВСЕХ 68 КАНАЛОВ ТЕЛЕМЕТРИИ: { 'Имя канала': индекс_в_пакете }
# Индекс 0 = magic, Индекс 1 = timestamp_ms, Индексы 2..69 = данные float
# =========================================================================
ALL_TELEMETRY_CHANNELS = {}

# 1. Базовые датчики (индексы 2..9)
_BASE_NAMES = [
    'RF_Power',
    'NTC',
    'Ext_VSense',
    'Brd_Detect',
    'Temp_MU',
    'Temp_IU',
    'Light_DA22',
    'Light_DA23',
]
for _idx, _name in enumerate(_BASE_NAMES):
    ALL_TELEMETRY_CHANNELS[_name] = 2 + _idx

# 2. Каналы AO 1..18 (индексы 10..27)
for _i in range(18):
    ALL_TELEMETRY_CHANNELS[f'AO_{_i + 1}'] = 10 + _i

# 3. Каналы VSense 1..6 (индексы 28..33)
for _i in range(6):
    ALL_TELEMETRY_CHANNELS[f'VSense_{_i + 1}'] = 28 + _i

# 4. Напряжения окружения Environment (индексы 34..41)
_ENV_NAMES = ['P3V3', 'P5V0', 'VIN', 'VDOUT1', 'P40V', 'USB', 'VDOUT2', 'VDOUT3']
for _idx, _name in enumerate(_ENV_NAMES):
    ALL_TELEMETRY_CHANNELS[f'Env_{_name}'] = 34 + _idx

# 5. Датчики тока, напряжения и мощности (индексы 42..65)
_SENSOR_NAMES = [
    'BOARD LOW',
    'BOARD HIGH',
    'Питание метки №2',
    'Питание метки №1',
    'Питание стенда',
    'Линия 3.3В стенда',
    'Питание тестируемого узла',
    'Линия 5В стенда',
]
for _idx, _s_name in enumerate(_SENSOR_NAMES):
    _base = 42 + _idx * 3
    ALL_TELEMETRY_CHANNELS[f'{_s_name} V'] = _base
    ALL_TELEMETRY_CHANNELS[f'{_s_name} I'] = _base + 1
    ALL_TELEMETRY_CHANNELS[f'{_s_name} P'] = _base + 2

# 6. Напряжения LIN 1..4 (индексы 66..69)
for _i in range(4):
    ALL_TELEMETRY_CHANNELS[f'LIN_V_{_i + 1}'] = 66 + _i


# Переменные состояния логгера
log_queue = deque()
is_logging = False
log_file_path = None
file_handle = None
writer_thread_obj = None

# Индексы выбранных каналов для текущей сессии записи
active_channel_indices = []


def log_packet(unpacked_data):
    """Записывает в очередь только выбранные пользователем каналы (до 10 шт.)"""
    global is_logging, active_channel_indices
    if is_logging and active_channel_indices:
        mcu_ms = int(unpacked_data[1])

        total_sec = mcu_ms // 1000
        mins = (total_sec // 60) % 60
        hours = total_sec // 3600
        secs = total_sec % 60
        millis = mcu_ms % 1000
        time_str = f'{hours:02d}:{mins:02d}:{secs:02d}.{millis:03d}'

        # Выбираем из пакета только нужные индексы
        selected_vals = [str(unpacked_data[idx]) for idx in active_channel_indices]
        line = f'{time_str};{mcu_ms};' + ';'.join(selected_vals) + '\n'
        log_queue.append(line)


def _writer_thread():
    global file_handle, is_logging
    while is_logging or len(log_queue) > 0:
        batch = []
        while len(log_queue) > 0 and len(batch) < 2000:
            batch.append(log_queue.popleft())

        if batch and file_handle:
            try:
                file_handle.writelines(batch)
                file_handle.flush()
            except Exception as e:
                print(f'[LOGGER ERROR] Ошибка записи: {e}')

        time.sleep(0.01)


def start_logger(selected_channels=None):
    """Запускает запись лога только для списка selected_channels"""
    global is_logging, log_file_path, file_handle, writer_thread_obj, active_channel_indices
    if is_logging:
        return

    # Если список не передан — берем все доступные каналы
    if not selected_channels:
        selected_channels = list(ALL_TELEMETRY_CHANNELS.keys())

    # Формируем список индексов для быстрого извлечения в log_packet
    valid_names = [name for name in selected_channels if name in ALL_TELEMETRY_CHANNELS]
    active_channel_indices = [ALL_TELEMETRY_CHANNELS[name] for name in valid_names]

    os.makedirs(LOGS_DIR, exist_ok=True)
    ts = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
    log_file_path = os.path.join(LOGS_DIR, f'telemetry_{ts}.csv')

    file_handle = open(log_file_path, mode='w', newline='', encoding='utf-8')
    file_handle.write('sep=;\n')

    header = ['Time', 'MCU_Time_ms'] + valid_names
    file_handle.write(';'.join(header) + '\n')
    file_handle.flush()

    log_queue.clear()
    is_logging = True
    writer_thread_obj = threading.Thread(target=_writer_thread, daemon=True)
    writer_thread_obj.start()
    print(f'[LOGGER] Старт записи ({len(valid_names)} каналов)! Файл: {log_file_path}')


def stop_logger():
    global is_logging, file_handle, writer_thread_obj
    if not is_logging:
        return

    print('[LOGGER] Остановка записи...')
    is_logging = False

    if writer_thread_obj and writer_thread_obj.is_alive():
        writer_thread_obj.join(timeout=2.0)

    if file_handle:
        try:
            file_handle.flush()
            os.fsync(file_handle.fileno())
            file_handle.close()
        except Exception as e:
            print(f'[LOGGER ERROR] Ошибка закрытия: {e}')
        finally:
            file_handle = None

    print(f'[LOGGER] Файл закрыт: {log_file_path}')