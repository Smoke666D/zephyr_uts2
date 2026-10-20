from collections import deque
import os
import threading
import time

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
LOGS_DIR = os.path.join(BASE_DIR, 'logs')

log_queue = deque()
is_logging = False
log_file_path = None
file_handle = None
writer_thread_obj = None


def log_packet(unpacked_data):
  """Записывает пакет в лог.

  unpacked_data[0] = 0xBE
  unpacked_data[1] = timestamp_ms (uint32_t с микроконтроллера)
  unpacked_data[2:] = 78 полей float
  """
  global is_logging
  if is_logging:
    mcu_ms = int(unpacked_data[1])

    # Формируем читаемое время: ММ:СС.мс
    total_sec = mcu_ms // 1000
    mins = (total_sec // 60) % 60
    hours = (total_sec // 3600)
    secs = total_sec % 60
    millis = mcu_ms % 1000
    time_str = f'{hours:02d}:{mins:02d}:{secs:02d}.{millis:03d}'

    # Пишем: Time_Formatted; MCU_Time_ms; все_остальные_параметры
    row = [time_str, mcu_ms] + list(unpacked_data[2:])
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
  global is_logging, log_file_path, file_handle, writer_thread_obj
  if is_logging:
    return

  os.makedirs(LOGS_DIR, exist_ok=True)
  from datetime import datetime

  ts = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
  log_file_path = os.path.join(LOGS_DIR, f'telemetry_{ts}.csv')

  file_handle = open(log_file_path, mode='w', newline='', encoding='utf-8')
  file_handle.write('sep=;\n')

  header = [
      'Time',
      'MCU_Time_ms',
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