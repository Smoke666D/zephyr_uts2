import asyncio
import struct
import threading
import time
from globals_data import telemetry_data_cache
from logger import log_packet
from nicegui import ui

# 1 байт (0xBE) + 1 uint32 (timestamp_ms) + 78 float = 317 байт
TELEMETRY_FORMAT = '<BI' + 'f' * 78
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)  # 317 байт

incoming_queue = []
queue_lock = threading.Lock()


def telemetry_reader_loop(
    tel_ser_ref,
    ui_labels,
    can_logs_dict=None,
    lin_logs_dict=None,
    raw_logs_dict=None,
):
  def _thread_target():
    serial_buffer = bytearray()
    synchronized = False

    while True:
      current_ser = tel_ser_ref.get('ser')
      if current_ser and current_ser.is_open:
        try:
          n = current_ser.in_waiting
          if n > 0:
            chunk = current_ser.read(max(n, 1024))
            if chunk:
              serial_buffer.extend(chunk)

          if len(serial_buffer) > 65536:
            serial_buffer = serial_buffer[-16384:]
            synchronized = False

          # 1. Синхронизация: ищем 0xBE и проверяем шаг ровно 317 байт
          while not synchronized and len(serial_buffer) >= (PACKET_SIZE * 2):
            idx = serial_buffer.find(b'\xbe')
            if idx == -1:
              serial_buffer.clear()
              break

            if idx > 0:
              del serial_buffer[:idx]

            if (
                len(serial_buffer) >= (PACKET_SIZE + 1)
                and serial_buffer[PACKET_SIZE] == 0xBE
            ):
              synchronized = True
              break
            else:
              del serial_buffer[0]

          # 2. Потоковый разбор пакетов
          while synchronized and len(serial_buffer) >= PACKET_SIZE:
            if serial_buffer[0] == 0xBE:
              pkt = serial_buffer[:PACKET_SIZE]
              try:
                unpacked = struct.unpack(TELEMETRY_FORMAT, pkt)
                with queue_lock:
                  incoming_queue.append(unpacked)
              except Exception as e:
                print(f'Ошибка распаковки: {e}')
              del serial_buffer[:PACKET_SIZE]
            else:
              synchronized = False
              break

        except Exception as e:
          print(f'Ошибка чтения COM: {e}')
          time.sleep(0.005)
      else:
        time.sleep(0.05)

  threading.Thread(target=_thread_target, daemon=True).start()

  def ui_update_callback():
    with queue_lock:
      if not incoming_queue:
        return
      packets_batch = list(incoming_queue)
      incoming_queue.clear()

    try:
      # Записываем в логгер абсолютно все пакеты
      for unpacked in packets_batch:
        log_packet(unpacked)

      # Для текстовых виджетов берем последний срез
      # unpacked[0] = magic (0xBE)
      # unpacked[1] = timestamp_ms (uint32)
      # unpacked[2..] = float поля
      latest_pkt = packets_batch[-1]

      ui_labels['rf_power'].set_text(f'{latest_pkt[2]:.2f}')
      ui_labels['ntc'].set_text(f'{latest_pkt[3]:.2f}')
      ui_labels['ext_vsense'].set_text(f'{latest_pkt[4]:.2f}')
      ui_labels['brd_detect'].set_text(f'{latest_pkt[5]:.2f}')
      ui_labels['temp_gnd'].set_text(f'{latest_pkt[6]:.2f}')
      ui_labels['temp_vdd'].set_text(f'{latest_pkt[7]:.2f}')
      ui_labels['light_gnd'].set_text(f'{latest_pkt[8]:.2f}')
      ui_labels['light_vdd'].set_text(f'{latest_pkt[9]:.2f}')

      # AO каналы (смещены на +1 из-за timestamp_ms: индексы 10..27)
      for i in range(18):
        val = latest_pkt[10 + i]
        ui_labels[f'ao_{i}'].set_text(f'{val:.2f}')

      for i in range(6):
        ui_labels['vsense_%d' % i].set_text(f'{latest_pkt[28 + i]:.2f}')
      for i in range(8):
        ui_labels['env_%d' % i].set_text(f'{latest_pkt[44 + i]:.2f}')

      curr_offset = 52
      for i in range(1, 9):
        v_val = latest_pkt[curr_offset]
        i_val = latest_pkt[curr_offset + 1]
        e_val = latest_pkt[curr_offset + 2]
        ui_labels[f'curr_v_{i}'].set_text(f'{v_val:.2f}')
        ui_labels[f'curr_i_{i}'].set_text(f'{i_val:.4f}')
        ui_labels[f'curr_e_{i}'].set_text(f'{e_val:.6f}')
        curr_offset += 3

      for i in range(1, 5):
        lin_v = latest_pkt[75 + i]
        if f'lin_v_{i}' in ui_labels:
          ui_labels[f'lin_v_{i}'].set_text(f'{lin_v:.2f}')

    except Exception as e:
      print(f'Ошибка обновления UI: {e}')

  ui.timer(0.05, ui_update_callback)