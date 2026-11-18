import asyncio
import struct
import threading
import time
import zlib
from globals_data import telemetry_data_cache
from logger import log_packet
from nicegui import ui
import serial

# 4 байта magic + 1 uint32 (timestamp_ms) + 68 float + 1 uint32 (crc32) = 284 байта
MAGIC_BYTES = b'\xaa\x55\xbe\xef'
TELEMETRY_FORMAT = '<II' + 'f' * 68 + 'I'
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)  # 284 байта

latest_packet = None
packet_lock = threading.Lock()

# Счётчики диагностики связи
crc_error_count = 0
valid_packet_count = 0


def telemetry_reader_loop(
    tel_ser_ref,
    ui_labels,
    can_logs_dict=None,
    lin_logs_dict=None,
    raw_logs_dict=None,
):
  """Системный поток чтения COM-порта с проверкой целостности по CRC-32."""

  def _thread_target():
    nonlocal tel_ser_ref
    global latest_packet, crc_error_count, valid_packet_count
    serial_buffer = bytearray()
    synchronized = False

    while True:
      current_ser = tel_ser_ref.get('ser')
      if current_ser and getattr(current_ser, 'is_open', False):
        try:
          n = current_ser.in_waiting
          if n > 0:
            chunk = current_ser.read(max(n, 1024))
            if chunk:
              serial_buffer.extend(chunk)

          # Защита от переполнения ОЗУ
          if len(serial_buffer) > 65536:
            serial_buffer = serial_buffer[-16384:]
            synchronized = False

          # 1. Синхронизация по 4-байтовому маркеру и шагу 284 байта
          while not synchronized and len(serial_buffer) >= (PACKET_SIZE * 2):
            idx = serial_buffer.find(MAGIC_BYTES)
            if idx == -1:
              tail = serial_buffer[-3:]
              serial_buffer.clear()
              serial_buffer.extend(tail)
              break

            if idx > 0:
              del serial_buffer[:idx]

            if (
                len(serial_buffer) >= (PACKET_SIZE + 4)
                and serial_buffer[PACKET_SIZE : PACKET_SIZE + 4] == MAGIC_BYTES
            ):
              synchronized = True
              break
            else:
              del serial_buffer[0]

          # 2. Потоковый разбор пакетов с проверкой CRC-32
          while synchronized and len(serial_buffer) >= PACKET_SIZE:
            if serial_buffer[:4] == MAGIC_BYTES:
              pkt = serial_buffer[:PACKET_SIZE]

              # Вычисляем CRC-32 по первым 280 байтам (без поля crc32)
              calc_crc = zlib.crc32(pkt[:-4]) & 0xFFFFFFFF
              # Извлекаем переданный CRC-32 из последних 4 байт
              (recv_crc,) = struct.unpack('<I', pkt[-4:])

              if calc_crc == recv_crc:
                # CRC СОВПАЛ — пакет 100% целый!
                try:
                  # Отрезаем последнее поле crc32, чтобы кортеж остался прежней длины для логгера
                  unpacked_full = struct.unpack(TELEMETRY_FORMAT, pkt)
                  unpacked_data = unpacked_full[:-1]

                  valid_packet_count += 1
                  log_packet(unpacked_data)

                  with packet_lock:
                    latest_packet = unpacked_data
                except Exception as e:
                  print(f'Ошибка распаковки: {e}')

                del serial_buffer[:PACKET_SIZE]
              else:
                # CRC НЕ СОВПАЛ — пакет побился в канале связи!
                crc_error_count += 1
                print(
                    f'[CRC-32 ERROR #{crc_error_count}] Пакет отброшен! '
                    f'Ожидался: 0x{calc_crc:08X}, Пришел: 0x{recv_crc:08X} '
                    f'(Успешных пакетов: {valid_packet_count})'
                )
                # Сдвигаемся на 4 байта и пересинхронизируемся, НЕ ЗАПИСЫВАЯ мусор в лог
                del serial_buffer[:4]
                synchronized = False
                break
            else:
              synchronized = False
              break

        except (serial.SerialException, OSError, PermissionError) as e:
          print(f'[TELEMETRY]: Устройство отключено ({e}). Освобождаем порт...')
          try:
            current_ser.close()
          except Exception:
            pass
          tel_ser_ref['ser'] = None
          serial_buffer.clear()
          synchronized = False
          time.sleep(0.2)
        except Exception as e:
          print(f'Ошибка чтения COM: {e}')
          time.sleep(0.01)
      else:
        time.sleep(0.05)

  threading.Thread(target=_thread_target, daemon=True).start()

  def ui_update_callback():
    global latest_packet
    with packet_lock:
      if latest_packet is None:
        return
      latest_pkt = latest_packet

    try:
      ui_labels['rf_power'].set_text(f'{latest_pkt[2]:.2f}')
      ui_labels['ntc'].set_text(f'{latest_pkt[3]:.2f}')
      ui_labels['ext_vsense'].set_text(f'{latest_pkt[4]:.2f}')
      ui_labels['brd_detect'].set_text(f'{latest_pkt[5]:.2f}')
      ui_labels['temp_gnd'].set_text(f'{latest_pkt[6]:.2f}')
      ui_labels['temp_vdd'].set_text(f'{latest_pkt[7]:.2f}')
      ui_labels['light_gnd'].set_text(f'{latest_pkt[8]:.2f}')
      ui_labels['light_vdd'].set_text(f'{latest_pkt[9]:.2f}')

      for i in range(18):
        val = latest_pkt[10 + i]
        telemetry_data_cache[f'ao_{i}'] = val
        ui_labels[f'ao_{i}'].set_text(f'{val:.2f}')

      for i in range(6):
        val = latest_pkt[28 + i]
        telemetry_data_cache[f'vsense_{i}'] = val
        ui_labels['vsense_%d' % i].set_text(f'{val:.2f}')

      for i in range(8):
        val = latest_pkt[34 + i]
        telemetry_data_cache[f'env_{i}'] = val
        ui_labels['env_%d' % i].set_text(f'{val:.2f}')

      curr_offset = 42
      for i in range(1, 9):
        v_val = latest_pkt[curr_offset]
        i_val = latest_pkt[curr_offset + 1]
        p_val = latest_pkt[curr_offset + 2]

        telemetry_data_cache[f'curr_v_{i}'] = v_val
        telemetry_data_cache[f'curr_i_{i}'] = i_val
        telemetry_data_cache[f'curr_p_{i}'] = p_val

        ui_labels[f'curr_v_{i}'].set_text(f'{v_val:.2f}')
        ui_labels[f'curr_i_{i}'].set_text(f'{i_val:.4f}')

        if f'curr_p_{i}' in ui_labels:
          ui_labels[f'curr_p_{i}'].set_text(f'{p_val:.4f}')
        elif f'curr_e_{i}' in ui_labels:
          ui_labels[f'curr_e_{i}'].set_text(f'{p_val:.4f}')
        telemetry_data_cache[f'curr_v_{i}'] = v_val
        telemetry_data_cache[f'curr_i_{i}'] = i_val
        telemetry_data_cache[f'curr_p_{i}'] = p_val
        curr_offset += 3

      for i in range(1, 5):
        lin_v = latest_pkt[65 + i]
        telemetry_data_cache[f'lin_v_{i}'] = lin_v
        if f'lin_v_{i}' in ui_labels:
          ui_labels[f'lin_v_{i}'].set_text(f'{lin_v:.2f}')

    except Exception as e:
      print(f'Ошибка обновления UI: {e}')

  ui.timer(0.33, ui_update_callback)