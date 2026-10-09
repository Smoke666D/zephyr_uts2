import asyncio
import struct

# Увеличили количество float с 66 до 70 (добавилось 4 напряжения для LIN 1-4)
TELEMETRY_FORMAT = '<B' + 'f' * 70
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)


async def telemetry_reader_loop(tel_ser_ref, ui_labels, can_logs_dict=None, lin_logs_dict=None):
  serial_buffer = bytearray()

  while True:
    await asyncio.sleep(0.01)

    current_ser = tel_ser_ref.get('ser')
    if current_ser and current_ser.is_open:
      try:
        if current_ser.in_waiting > 0:
          data = current_ser.read(current_ser.in_waiting)
          if data:
            serial_buffer.extend(data)

        if len(serial_buffer) > 4096:
          serial_buffer = serial_buffer[-2048:]

        while len(serial_buffer) >= 2:
          magic = serial_buffer[0]

          # --- 1. ПАКЕТ CAN (0xCB) ---
          if magic == 0xCB:
            if len(serial_buffer) >= 7:
              can_num = serial_buffer[1]
              dlc = serial_buffer[6]
              total_can_size = 7 + dlc

              if len(serial_buffer) >= total_can_size:
                can_pkt = serial_buffer[:total_can_size]
                _, _, can_id, can_dlc = struct.unpack('<BBIB', can_pkt[:7])
                payload = can_pkt[7 : 7 + can_dlc]

                if can_logs_dict and can_num in can_logs_dict:
                  data_hex = payload.hex(' ').upper()
                  can_logs_dict[can_num].push(
                      f'RX <- ID: 0x{can_id:X} | DLC: {can_dlc} | DATA:'
                      f' {data_hex}'
                  )

                del serial_buffer[:total_can_size]
                continue
              else:
                break
            else:
              break

          # --- 2. ПАКЕТ ТЕЛЕМЕТРИИ (0xBE) ---
          elif magic == 0xBE:
            if len(serial_buffer) >= PACKET_SIZE:
              pkt = serial_buffer[:PACKET_SIZE]
              unpacked = struct.unpack(TELEMETRY_FORMAT, pkt)

              ui_labels['rf_power'].set_text(f'{unpacked[1]:.2f}')
              ui_labels['ntc'].set_text(f'{unpacked[2]:.2f}')
              ui_labels['ext_vsense'].set_text(f'{unpacked[3]:.2f}')
              ui_labels['brd_detect'].set_text(f'{unpacked[4]:.2f}')
              ui_labels['temp_gnd'].set_text(f'{unpacked[5]:.2f}')
              ui_labels['temp_vdd'].set_text(f'{unpacked[6]:.2f}')
              ui_labels['light_gnd'].set_text(f'{unpacked[7]:.2f}')
              ui_labels['light_vdd'].set_text(f'{unpacked[8]:.2f}')

              for i in range(18):
                ui_labels[f'ao_{i}'].set_text(f'{unpacked[9+i]:.2f}')
              for i in range(6):
                ui_labels['vsense_%d' % i].set_text(f'{unpacked[27+i]:.2f}')
              for i in range(8):
                ui_labels['env_%d' % i].set_text(f'{unpacked[43+i]:.2f}')

              # Датчики тока (8 пар = 16 float, индексы 51 - 66)
              curr_offset = 51
              for i in range(1, 9):
                v_val = unpacked[curr_offset]
                i_val = unpacked[curr_offset + 1]
                ui_labels[f'curr_v_{i}'].set_text(f'{v_val:.2f}')
                ui_labels[f'curr_i_{i}'].set_text(f'{i_val:.4f}')
                curr_offset += 2

              # Напряжения LIN 1-4 (индексы 67, 68, 69, 70)
              for i in range(1, 5):
                lin_v = unpacked[66 + i]
                if f'lin_v_{i}' in ui_labels:
                  ui_labels[f'lin_v_{i}'].set_text(f'{lin_v:.2f}')

              del serial_buffer[:PACKET_SIZE]
              continue
            else:
              break
          else:
            serial_buffer.pop(0)

      except Exception as e:
        print(f'Ошибка бинарного чтения: {e}')
        await asyncio.sleep(0.1)