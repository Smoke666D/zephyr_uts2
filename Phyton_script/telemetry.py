import asyncio
import struct
import threading
import time
from globals_data import telemetry_data_cache
from logger import log_packet
from nicegui import ui

# 1 байт (0xBE) + 1 uint32 (timestamp_ms) + 68 float = 277 байт
TELEMETRY_FORMAT = '<BI' + 'f' * 68
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)  # 277 байт

# Хранилище только самого последнего пакета для дашборда
latest_packet = None
packet_lock = threading.Lock()


def telemetry_reader_loop(
    tel_ser_ref,
    ui_labels,
    can_logs_dict=None,
    lin_logs_dict=None,
    raw_logs_dict=None,
):
  """
  Системный поток: вычитывает COM-порт и пишет в логгер на полной скорости (1 кГц).
  UI таймер: обновляет экран ровно 3 раза в секунду (каждые 330 мс).
  """
  def _thread_target():
    nonlocal tel_ser_ref
    global latest_packet
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

          # Защита от переполнения буфера в ОЗУ
          if len(serial_buffer) > 65536:
            serial_buffer = serial_buffer[-16384:]
            synchronized = False

          # 1. Синхронизация по маркеру 0xBE и шагу 277 байт
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

          # 2. Потоковый разбор пакетов на полной скорости (1 кГц)
          while synchronized and len(serial_buffer) >= PACKET_SIZE:
            if serial_buffer[0] == 0xBE:
              pkt = serial_buffer[:PACKET_SIZE]
              try:
                unpacked = struct.unpack(TELEMETRY_FORMAT, pkt)

                # --- ЭТАП 1: 100% ПАКЕТОВ УХОДЯТ В ЛОГ НА ДИСК (1 кГц без потерь) ---
                log_packet(unpacked)

                # --- ЭТАП 2: СОХРАНЯЕМ СВЕЖИЙ СРЕЗ ДЛЯ ДАШБОРДА ---
                with packet_lock:
                  latest_packet = unpacked

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

  # Запуск системного потока чтения
  threading.Thread(target=_thread_target, daemon=True).start()

  # Функция обновления дашборда
  def ui_update_callback():
    global latest_packet
    with packet_lock:
      if latest_packet is None:
        return
      latest_pkt = latest_packet

    try:
      # Обновление основных датчиков
      ui_labels['rf_power'].set_text(f'{latest_pkt[2]:.2f}')
      ui_labels['ntc'].set_text(f'{latest_pkt[3]:.2f}')
      ui_labels['ext_vsense'].set_text(f'{latest_pkt[4]:.2f}')
      ui_labels['brd_detect'].set_text(f'{latest_pkt[5]:.2f}')
      ui_labels['temp_gnd'].set_text(f'{latest_pkt[6]:.2f}')
      ui_labels['temp_vdd'].set_text(f'{latest_pkt[7]:.2f}')
      ui_labels['light_gnd'].set_text(f'{latest_pkt[8]:.2f}')
      ui_labels['light_vdd'].set_text(f'{latest_pkt[9]:.2f}')

      # AO каналы (1-18)
      for i in range(18):
        val = latest_pkt[10 + i]
        ui_labels[f'ao_{i}'].set_text(f'{val:.2f}')

      # VSense (1-6)
      for i in range(6):
        ui_labels['vsense_%d' % i].set_text(f'{latest_pkt[28 + i]:.2f}')

      # Environment (1-8)
      for i in range(8):
        ui_labels['env_%d' % i].set_text(f'{latest_pkt[34 + i]:.2f}')

      # Датчики тока, напряжения и мощности (8 каналов)
      curr_offset = 42
      for i in range(1, 9):
        v_val = latest_pkt[curr_offset]
        i_val = latest_pkt[curr_offset + 1]
        p_val = latest_pkt[curr_offset + 2]

        ui_labels[f'curr_v_{i}'].set_text(f'{v_val:.2f}')
        ui_labels[f'curr_i_{i}'].set_text(f'{i_val:.4f}')

        # Вывод мощности (4 знака после запятой)
        if f'curr_p_{i}' in ui_labels:
          ui_labels[f'curr_p_{i}'].set_text(f'{p_val:.4f}')
        elif f'curr_e_{i}' in ui_labels:
          ui_labels[f'curr_e_{i}'].set_text(f'{p_val:.4f}')

        curr_offset += 3

      # Напряжения LIN (1-4)
      for i in range(1, 5):
        lin_v = latest_pkt[65 + i]
        if f'lin_v_{i}' in ui_labels:
          ui_labels[f'lin_v_{i}'].set_text(f'{lin_v:.2f}')

    except Exception as e:
      print(f'Ошибка обновления UI: {e}')

  # Таймер вывода на дашборд: ровно 3 раза в секунду (каждые 330 мс)
  ui.timer(0.33, ui_update_callback)