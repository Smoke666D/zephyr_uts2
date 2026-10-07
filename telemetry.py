import asyncio
import struct

# Настройка бинарного протокола телеметрии (1 байт magic + 66 float)
TELEMETRY_FORMAT = '<B' + 'f' * 66
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)


async def telemetry_reader_loop(tel_ser_ref, ui_labels):
  """Фоновый поток чтения бинарной телеметрии из COM-порта"""
  serial_buffer = bytearray()
  while True:
    await asyncio.sleep(0.02)
    # Используем словарь для доступа к мутабельному объекту порта снаружи
    current_ser = tel_ser_ref.get('ser')
    if current_ser and current_ser.is_open:
      try:
        data = current_ser.read(current_ser.in_waiting or 1)
        if data:
          serial_buffer.extend(data)
          while len(serial_buffer) >= PACKET_SIZE:
            if serial_buffer[0] == 0xBE:
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

              curr_offset = 51
              for i in range(1, 9):
                v_val = unpacked[curr_offset]
                i_val = unpacked[curr_offset + 1]
                ui_labels['curr_v_%d' % i].set_text(f'{v_val:.2f}')
                ui_labels['curr_i_%d' % i].set_text(f'{i_val:.4f}')
                curr_offset += 2

              serial_buffer = serial_buffer[PACKET_SIZE:]
            else:
              serial_buffer.pop(0)
      except Exception as e:
        print(f'Ошибка чтения телеметрии: {e}')