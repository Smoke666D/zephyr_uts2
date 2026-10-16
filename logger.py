from collections import deque
from datetime import datetime, timedelta
import os
import threading
import time

__all__ = ['start_logger', 'stop_logger', 'log_packet']

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

log_queue = deque()
is_logging = False
log_file_path = None
file_handle = None
start_time_real = None
packet_counter = 0
writer_thread_obj = None


def log_packet(unpacked_data):
  """Вызывается из телеметрии при приеме каждого пакета."""
  global start_time_real, packet_counter
  if is_logging:
    packet_time = start_time_real + timedelta(milliseconds=packet_counter)
    timestamp = packet_time.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
    packet_counter += 1

    row = [timestamp] + list(unpacked_data[1:])
    row_str = [str(x) for x in row]
    log_queue.append(';'.join(row_str) + '\n')


def _writer_thread():
  global file_handle, is_logging
  while is_logging or len(log_queue) > 0:
    batch = []
    while len(log_queue) > 0 and len(batch) < 1000:
      batch.append(log_queue.popleft())

    if batch and file_handle:
      try:
        file_handle.writelines(batch)
        file_handle.flush()
      except Exception as e:
        print(f'[LOGGER ERROR] Ошибка записи: {e}')

    time.sleep(0.01)


def start_logger():
  global is_logging, log_file_path, file_handle, start_time_real, packet_counter, writer_thread_obj
  if is_logging:
    return

  os.makedirs(LOGS_DIR, exist_ok=True)
  timestamp = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
  log_file_path = os.path.join(LOGS_DIR, f'telemetry_{timestamp}.csv')

  file_handle = open(log_file_path, mode='w', newline='', encoding='utf-8')

  # Спец-строка для Excel (разделитель ;)
  file_handle.write('sep=;\n')

  header = [
      'Timestamp',
      'RF_Power',
      'NTC',
      'Ext_VSense',
      'Brd_Detect',
      'Temp_MU',
      'Temp_IU',
      'Light_DA22',
      'Light_DA23',
  ]
  header.extend([f'AO_{i}' for i in range(1, 19)])
  header.extend([f'VSense_{i}' for i in range(1, 7)])
  header.extend([f'Step7_{i}' for i in range(1, 6)])
  header.extend([f'Step8_{i}' for i in range(1, 6)])
  env_names = [
      'P3V3',
      'P5V0',
      'VIN',
      'VDOUT1',
      'P40V',
      'USB',
      'VDOUT2',
      'VDOUT3',
  ]
  header.extend([f'Env_{name}' for name in env_names])
  for i in range(1, 9):
    header.extend([f'Cur_V_{i}', f'Cur_I_{i}', f'Cur_E_{i}'])
  header.extend([f'LIN_V_{i}' for i in range(1, 5)])

  file_handle.write(';'.join(header) + '\n')
  file_handle.flush()

  start_time_real = datetime.now()
  packet_counter = 0
  log_queue.clear()

  is_logging = True
  writer_thread_obj = threading.Thread(target=_writer_thread, daemon=True)
  writer_thread_obj.start()
  print(f'[LOGGER] Старт записи! Файл: {log_file_path}')


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