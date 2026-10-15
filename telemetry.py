import asyncio
import struct
import threading
import time
from filters import MovingAverageFilter
from globals_data import telemetry_data_cache
from logger import log_packet
from nicegui import ui
from oscilloscope import update_oscilloscope_data

# 1 байт (magic 0xBE) + 78 полей float
TELEMETRY_FORMAT = '<B' + 'f' * 78
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)  # 313 байт

# Потокобезопасный буфер для передачи данных в UI
incoming_queue = []
queue_lock = threading.Lock()


def telemetry_reader_loop(
    tel_ser_ref,
    ui_labels,
    can_logs_dict=None,
    lin_logs_dict=None,
    raw_logs_dict=None,
):
  """Запускает отдельный системный поток для чтения COM-порта на максимальной скорости"""

  def _thread_target():
    serial_buffer = bytearray()
    while True:
      current_ser = tel_ser_ref.get('ser')
      if current_ser and current_ser.is_open:
        try:
          if current_ser.in_waiting > 0:
            chunk = current_ser.read(current_ser.in_waiting)
            if chunk:
              serial_buffer.extend(chunk)

          # Если буфер разросся слишком сильно (отставание), чистим хвост, чтобы не было лагов
          if len(serial_buffer) > 32768:
            serial_buffer = serial_buffer[-16384:]

          # Жесткий разбор строго по размеру пакета телеметрии (313 байт)
          while len(serial_buffer) >= PACKET_SIZE:
            # Ищем маркер 0xBE
            idx = serial_buffer.find(b'\xbe')
            if idx == -1:
              # Маркер не найден — очищаем весь буфер
              serial_buffer.clear()
              break

            # Если маркер не в самом начале, отрезаем мусор перед ним
            if idx > 0:
              del serial_buffer[:idx]

            # Проверяем, хватит ли данных на целый пакет после отсечения мусора
            if len(serial_buffer) < PACKET_SIZE:
              break

            # Вырезаем пакет
            pkt = serial_buffer[:PACKET_SIZE]

            # Дополнительная проверка целостности по первому байту
            if pkt[0] == 0xBE:
              try:
                unpacked = struct.unpack(TELEMETRY_FORMAT, pkt)
                # Складываем в потокобезопасную очередь абсолютно все пакеты
                with queue_lock:
                  incoming_queue.append(unpacked)
              except Exception as e:
                print(f'Ошибка распаковки пакета: {e}')

            # Сдвигаем буфер на размер обработанного пакета
            del serial_buffer[:PACKET_SIZE]

        except Exception as e:
          print(f'Ошибка в потоке чтения порта: {e}')
          time.sleep(0.01)
      else:
        time.sleep(0.05)

  # Стартуем фоновый демон-поток
  t = threading.Thread(target=_thread_target, daemon=True)
  t.start()

  # Функция пакетного обновления UI (вызывается таймером NiceGUI с частотой ~20 Гц)
  def ui_update_callback():
    # 1. Забираем ВСЕ накопившиеся пакеты из очереди за эти 50 мс
    with queue_lock:
      if not incoming_queue:
        return
      packets_batch = list(
          incoming_queue
      )  # Копируем всё, что успело прилететь (~50 пакетов при 1000 Гц)
      incoming_queue.clear()  # Очищаем очередь для новых данных

    try:
      # --- ЭТАП 1: ОБРАБОТКА ВСЕХ ДАННЫХ (БЕЗ UI) ---
      # Мы обязаны сохранить и прогнать через математику КАЖДЫЙ пакет, чтобы не терять точки
      for unpacked in packets_batch:
        # Пишем в лог на диск абсолютно ВСЕ пакеты (1000 раз в сек)
        log_packet(unpacked)

        # Передаем в осциллограф ВСЕ точки (чтобы график не был рваным)
        update_oscilloscope_data(unpacked[9], unpacked[10])

      # --- ЭТАП 2: ОБНОВЛЕНИЕ ТЕКСТОВЫХ ИНДИКАТОРОВ (UI) ---
      # Для текстовых label-ов берем только САМЫЙ СВЕЖИЙ пакет из пачки, чтобы браузер не лагал
      latest_pkt = packets_batch[-1]

      ui_labels['rf_power'].set_text(f'{latest_pkt[1]:.2f}')
      ui_labels['ntc'].set_text(f'{latest_pkt[2]:.2f}')
      ui_labels['ext_vsense'].set_text(f'{latest_pkt[3]:.2f}')
      ui_labels['brd_detect'].set_text(f'{latest_pkt[4]:.2f}')
      ui_labels['temp_gnd'].set_text(f'{latest_pkt[5]:.2f}')
      ui_labels['temp_vdd'].set_text(f'{latest_pkt[6]:.2f}')
      ui_labels['light_gnd'].set_text(f'{latest_pkt[7]:.2f}')
      ui_labels['light_vdd'].set_text(f'{latest_pkt[8]:.2f}')

      for i in range(18):
        val = latest_pkt[9 + i]
        ui_labels[f'ao_{i}'].set_text(f'{val:.2f}')

      for i in range(6):
        ui_labels['vsense_%d' % i].set_text(f'{latest_pkt[27+i]:.2f}')
      for i in range(8):
        ui_labels['env_%d' % i].set_text(f'{latest_pkt[43+i]:.2f}')

      curr_offset = 51
      for i in range(1, 9):
        v_val = latest_pkt[curr_offset]
        i_val = latest_pkt[curr_offset + 1]
        e_val = latest_pkt[curr_offset + 2]
        ui_labels['curr_v_%d' % i].set_text(f'{v_val:.2f}')
        ui_labels['curr_i_%d' % i].set_text(f'{i_val:.4f}')
        ui_labels['curr_e_%d' % i].set_text(f'{e_val:.6f}')
        curr_offset += 3

      for i in range(1, 5):
        lin_v = latest_pkt[74 + i]
        if f'lin_v_{i}' in ui_labels:
          ui_labels[f'lin_v_{i}'].set_text(f'{lin_v:.2f}')

    except Exception as e:
      print(f'Ошибка обработки пакета/UI: {e}')

  # Регистрируем таймер обновления интерфейса (20 раз в секунду — плавно для глаз и без лагов)
  ui.timer(0.05, ui_update_callback)