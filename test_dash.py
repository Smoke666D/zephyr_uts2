import asyncio
import struct
from nicegui import ui
import serial
from serial.tools import list_ports

# --- Настройка бинарного протокола телеметрии ---
TELEMETRY_FORMAT = '<B' + 'f' * 66
PACKET_SIZE = struct.calcsize(TELEMETRY_FORMAT)

# Глобальные переменные портов
tel_ser = None
shell_ser = None
serial_buffer = bytearray()

# Ссылки на элементы UI для динамического обновления
ui_labels = {}

# Имена линий enable
ENABLE_LINES = [
    'EN_DUT2_PSU',
    'EN_DUT3_PSU',
    'EN_P12V',
    'EN_P40V',
    'EN_VA',
    'EN_USB_OUT',
    'EN_USB_BOOT',
    'EN_USB_TOP',
]

# Светодиоды
LED_LINES = [
    {'id': 1, 'name': 'Красный'},
    {'id': 2, 'name': 'Зеленый'},
    {'id': 3, 'name': 'Синий'},
]


def get_available_ports():
  ports = [p.device for p in list_ports.comports()]
  return ports if ports else ['Нет портов']


# --- Интерфейс ---
ui.query('body').classes('bg-slate-900 text-white p-2')

ui.label('UTS2 Embedded Dashboard').classes(
    'text-xl font-bold mb-2 text-sky-400'
)

# Главный контейнер для всех панелей с возможностью перетаскивания (Drag-and-Drop)
with ui.column().classes('w-full gap-2') as main_sortable_container:

  # --- БЛОК 1: УПРАВЛЕНИЕ ПОРТАМИ (Компактный) ---
  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    with ui.row().classes('w-full gap-2'):

      # --- Блок Телеметрии ---
      with ui.card().classes('bg-slate-700/50 p-2 flex-1'):
        ui.label('Телеметрия (Бинарный)').classes(
            'text-sm font-semibold text-sky-300 mb-1'
        )
        ports_list = get_available_ports()
        tel_select = ui.select(
            options=ports_list,
            value=ports_list[0] if ports_list else None,
            label='Порт',
        ).props('dark outlined dense color=sky-400').classes(
            'w-full bg-slate-700 rounded text-sm'
        )


        def open_telemetry_port(p_name):
          global tel_ser
          if tel_ser and tel_ser.is_open:
            tel_ser.close()
          try:
            tel_ser = serial.Serial(p_name, baudrate=115200, timeout=0.01)
            tel_btn.text = 'Закрыть'
            tel_btn.classes(remove='bg-sky-600', add='bg-red-600')
            ui.notify(f'Телеметрия: {p_name}', type='positive')
          except Exception as e:
            ui.notify(f'Ошибка: {e}', type='negative')


        def toggle_tel():
          global tel_ser
          if tel_ser and tel_ser.is_open:
            tel_ser.close()
            tel_btn.text = 'Открыть'
            tel_btn.classes(remove='bg-red-600', add='bg-sky-600')
            ui.notify('Телеметрия закрыта', type='warning')
          else:
            try:
              p_name = tel_select.value
              if not p_name or p_name == 'Нет портов':
                raise ValueError('Порт не выбран!')
              open_telemetry_port(p_name)
            except Exception as e:
              ui.notify(f'Ошибка: {e}', type='negative')


        tel_btn = ui.button('Открыть', on_click=toggle_tel).classes(
            'mt-2 bg-sky-600 text-white text-xs py-1 px-2'
        )

      # --- Блок Shell ---
      with ui.card().classes('bg-slate-700/50 p-2 flex-1'):
        ui.label('Shell (Команды)').classes(
            'text-sm font-semibold text-emerald-300 mb-1'
        )
        shell_select = ui.select(
            options=ports_list,
            value=ports_list[0] if ports_list else None,
            label='Порт',
        ).props('dark outlined dense color=emerald-400').classes(
            'w-full bg-slate-700 rounded text-sm'
        )


        def open_shell_port(p_name):
          global shell_ser
          if shell_ser and shell_ser.is_open:
            shell_ser.close()
          try:
            shell_ser = serial.Serial(p_name, baudrate=115200, timeout=0.01)
            shell_btn.text = 'Закрыть'
            shell_btn.classes(remove='bg-emerald-600', add='bg-red-600')
            ui.notify(f'Shell: {p_name}', type='positive')
          except Exception as e:
            ui.notify(f'Ошибка: {e}', type='negative')


        def toggle_shell():
          global shell_ser
          if shell_ser and shell_ser.is_open:
            shell_ser.close()
            shell_btn.text = 'Открыть'
            shell_btn.classes(remove='bg-emerald-600', add='bg-red-600')
            ui.notify('Shell закрыт', type='warning')
          else:
            try:
              p_name = shell_select.value
              if not p_name or p_name == 'Нет портов':
                raise ValueError('Порт не выбран!')
              open_shell_port(p_name)
            except Exception as e:
              ui.notify(f'Ошибка: {e}', type='negative')


        shell_btn = ui.button('Открыть', on_click=toggle_shell).classes(
            'mt-2 bg-emerald-600 text-white text-xs py-1 px-2'
        )

    # --- КНОПКИ АВТОПОИСКА И ОБНОВЛЕНИЯ ---
    async def global_auto_scan():
      ui.notify('Автопоиск портов...', type='info')
      current_ports = get_available_ports()
      tel_select.options = current_ports
      shell_select.options = current_ports

      found_tel, found_shell = None, None

      for p_name in current_ports:
        if p_name == 'Нет портов':
          continue
        if not found_tel:
          try:
            s = serial.Serial(p_name, baudrate=115200, timeout=0.05)
            s.reset_input_buffer()
            start_t = asyncio.get_event_loop().time()
            is_tel = False
            while (asyncio.get_event_loop().time() - start_t) < 1.0:
              if s.in_waiting > 0:
                if b'\xbe' in s.read(s.in_waiting):
                  is_tel = True
                  break
              await asyncio.sleep(0.05)
            s.close()
            if is_tel:
              found_tel = p_name
              continue
          except Exception:
            pass

        if not found_shell:
          try:
            s = serial.Serial(p_name, baudrate=115200, timeout=0.2)
            s.reset_input_buffer()
            s.write(b'\r\n')
            await asyncio.sleep(0.2)
            if s.in_waiting > 0:
              resp = s.read(s.in_waiting)
              if len(resp) > 1 and any(
                  c in resp for c in [b'~', b'>', b'$', b'#', b'\r', b'\n']
              ):
                found_shell = p_name
            s.close()
          except Exception:
            pass

      if found_tel:
        tel_select.value = found_tel
        open_telemetry_port(found_tel)
        ui.notify(f'Телеметрия: {found_tel}', type='positive')
      else:
        ui.notify('Телеметрия не найдена', type='warning')

      if found_shell:
        shell_select.value = found_shell
        open_shell_port(found_shell)
        ui.notify(f'Shell: {found_shell}', type='positive')
      else:
        ui.notify('Shell не найден', type='warning')

    def refresh_ports():
      p = get_available_ports()
      tel_select.options = p
      shell_select.options = p
      ui.notify('Порты обновлены', type='info')

    with ui.row().classes('w-full gap-2 mt-2 items-center px-1'):
      ui.button('Обновить порты', on_click=refresh_ports).classes(
          'bg-slate-700 text-white text-xs py-1 px-2'
      )
      ui.button('Автопоиск портов', on_click=global_auto_scan).classes(
          'bg-amber-600 text-white font-bold text-xs py-1 px-2'
      )

  # --- БЛОК 2: ПАНЕЛЬ УПРАВЛЕНИЯ ЛИНИЯМИ ENABLE ---
  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Управление линиями enable').classes(
        'text-xs font-semibold text-yellow-300 mb-1'
    )


    def on_ch_change(ch_num, name, e):
      state = 'ON' if e.value else 'OFF'
      cmd = f'en_set {ch_num} {state}\r\n'
      if shell_ser and shell_ser.is_open:
        shell_ser.write(cmd.encode('utf-8'))
        ui.notify(f'[{name}] Отправлено: {cmd.strip()}', type='info')
      else:
        ui.notify('Shell порт закрыт!', type='warning')


    with ui.grid(columns=4).classes('w-full gap-1'):
      for idx, name in enumerate(ENABLE_LINES):
        ch_num = idx + 1
        ui.checkbox(
            f'{ch_num}. {name}',
            value=True,
            on_change=lambda e, c=ch_num, n=name: on_ch_change(c, n, e),
        ).classes('text-white text-xs')

  # --- БЛОК 3: ПАНЕЛЬ УПРАВЛЕНИЯ СВЕТОДИОДАМИ ---
  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Управление светодиодами').classes(
        'text-xs font-semibold text-yellow-300 mb-1'
    )


    def on_led_change(led_id, name, e):
      state = 'ON' if e.value else 'OFF'
      cmd = f'led_set {led_id} {state}\r\n'
      if shell_ser and shell_ser.is_open:
        shell_ser.write(cmd.encode('utf-8'))
        ui.notify(f'[Светодиод {name}] Отправлено: {cmd.strip()}', type='info')
      else:
        ui.notify('Shell порт закрыт!', type='warning')


    with ui.row().classes('w-full gap-4'):
      for led in LED_LINES:
        ui.checkbox(
            f'{led["id"]}. {led["name"]}',
            value=False,
            on_change=lambda e, lid=led['id'], lname=led['name']: on_led_change(
                lid, lname, e
            ),
        ).classes('text-white text-xs')

  # --- БЛОК 4: ПАНЕЛЬ УПРАВЛЕНИЯ 18 КАНАЛАМИ IO ---
  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Управление линиями ввода/вывода (IO 1-18)').classes(
        'text-xs font-semibold text-yellow-300 mb-1'
    )


    def on_io_change(ch_num, select_element):
      val_map = {'INPUT': 'IN', 'HIGH': 'HI', 'LOW': 'LO'}
      short_state = val_map.get(select_element.value, 'IN')
      cmd = f'out_set {ch_num} {short_state}\r\n'
      if shell_ser and shell_ser.is_open:
        shell_ser.write(cmd.encode('utf-8'))
        ui.notify(f'[IO {ch_num}] Отправлено: {cmd.strip()}', type='info')
      else:
        ui.notify('Shell порт закрыт!', type='warning')


    with ui.grid(columns=6).classes('w-full gap-1'):
      for i in range(1, 19):
        with ui.card().classes(
            'bg-slate-700 p-1 flex-row items-center justify-between rounded'
        ):
          ui.label(f'IO {i}').classes('text-[11px] text-white font-bold')
          io_select = ui.select(
              options=['INPUT', 'HIGH', 'LOW'], value='INPUT'
          ).props('dark outlined dense').classes(
              'w-20 bg-slate-800 text-[10px] text-white'
          )
          io_select.on(
              'update:model-value', lambda e, c=i, s=io_select: on_io_change(c, s)
          )

  # --- БЛОК 5: ПАНЕЛЬ УПРАВЛЕНИЯ АНАЛОГОВЫМИ ВЫХОДАМИ И РЕЗИСТОРАМИ ---
  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Управление ЦАП и подстроечными резисторами').classes(
        'text-xs font-semibold text-yellow-300 mb-1.5'
    )

    with ui.row().classes('w-full gap-4 items-center'):

      # 1. Управление ЦАП
      with ui.row().classes('items-center gap-1.5 flex-1'):
        ui.label('ЦАП:').classes('text-xs text-white font-medium')

        dac_input = ui.input(placeholder='15.00 - 30.00', value='15.00').props(
            'dark outlined dense type=number input-class="text-right"'
        ).classes('w-28 bg-slate-700 text-xs text-white rounded')

        ui.label('В').classes('text-xs text-slate-300 font-medium')

        def send_dac():
          try:
            val = float(dac_input.value)
            if 15.00 <= val <= 30.00:
              cmd = f'dac_set {val:.2f}\r\n'
              if shell_ser and shell_ser.is_open:
                shell_ser.write(cmd.encode('utf-8'))
                ui.notify(f'[ЦАП] Отправлено: {cmd.strip()}', type='positive')
              else:
                ui.notify('Shell порт закрыт!', type='warning')
            else:
              ui.notify(
                  'Значение ЦАП должно быть от 15.00 до 30.00', type='warning'
              )
          except ValueError:
            ui.notify('Введите корректное число для ЦАП', type='negative')

        dac_input.on('keydown.enter', send_dac)
        ui.button('Set DAC', on_click=send_dac).classes(
            'bg-sky-600 text-white text-xs py-1 px-2'
        )

      # 2. Резистор 1
      with ui.row().classes('items-center gap-1.5'):
        ui.label('Резистор 1:').classes('text-xs text-white font-medium')

        res1_input = ui.input(placeholder='0-100', value='0.00').props(
            'dark outlined dense type=number input-class="text-right"'
        ).classes('w-20 bg-slate-700 text-xs text-white rounded')

        ui.label('кОм').classes('text-xs text-slate-300 font-medium')

        def send_res1():
          try:
            val = float(res1_input.value)
            if 0.00 <= val <= 100.00:
              cmd = f'adj_res_set 1 {val:.2f}\r\n'
              if shell_ser and shell_ser.is_open:
                shell_ser.write(cmd.encode('utf-8'))
                ui.notify(
                    f'[Резистор 1] Отправлено: {cmd.strip()}', type='positive'
                )
              else:
                ui.notify('Shell порт закрыт!', type='warning')
            else:
              ui.notify(
                  'Значение резистора должно быть от 0.00 до 100.00',
                  type='warning',
              )
          except ValueError:
            ui.notify('Введите корректное число для Резистора 1', type='negative')

        res1_input.on('keydown.enter', send_res1)
        ui.button('Set R1', on_click=send_res1).classes(
            'bg-emerald-600 text-white text-xs py-1 px-2'
        )

      # 3. Резистор 2
      with ui.row().classes('items-center gap-1.5'):
        ui.label('Резистор 2:').classes('text-xs text-white font-medium')

        res2_input = ui.input(placeholder='0-100', value='0.00').props(
            'dark outlined dense type=number input-class="text-right"'
        ).classes('w-20 bg-slate-700 text-xs text-white rounded')

        ui.label('кОм').classes('text-xs text-slate-300 font-medium')

        def send_res2():
          try:
            val = float(res2_input.value)
            if 0.00 <= val <= 100.00:
              cmd = f'adj_res_set 2 {val:.2f}\r\n'
              if shell_ser and shell_ser.is_open:
                shell_ser.write(cmd.encode('utf-8'))
                ui.notify(
                    f'[Резистор 2] Отправлено: {cmd.strip()}', type='positive'
                )
              else:
                ui.notify('Shell порт закрыт!', type='warning')
            else:
              ui.notify(
                  'Значение резистора должно быть от 0.00 до 100.00',
                  type='warning',
              )
          except ValueError:
            ui.notify('Введите корректное число для Резистора 2', type='negative')

        res2_input.on('keydown.enter', send_res2)
        ui.button('Set R2', on_click=send_res2).classes(
            'bg-emerald-600 text-white text-xs py-1 px-2'
        )

  # --- БЛОК 6: ДАШБОРД ДАТЧИКОВ ---
  def create_metric_card(title, key, unit=''):
    with ui.card().classes(
        'bg-slate-700 py-3 px-3 flex-1 min-w-[130px] rounded flex flex-row justify-between items-center cursor-default'
    ):
      ui.label(title).classes(
          'text-xs text-slate-300 font-medium leading-none m-0'
      )
      with ui.row().classes('items-center gap-1 m-0'):
        lbl = ui.label('0.00').classes(
            'text-base font-mono text-emerald-400 font-bold leading-none m-0'
        )
        if unit:
          ui.label(unit).classes(
              'text-xs text-slate-400 font-medium leading-none m-0'
          )
      ui_labels[key] = lbl


  with ui.column().classes('w-full gap-2 cursor-grab'):
    ui.label('Дашборд датчиков').classes('text-sm font-bold text-sky-300')

    with ui.row().classes('w-full gap-1'):
      create_metric_card('RF Power', 'rf_power', unit='дБ')
      create_metric_card('NTC', 'ntc')
      create_metric_card('Ext VSense', 'ext_vsense')
      create_metric_card('Brd Detect', 'brd_detect')
      create_metric_card('Температура МУ', 'temp_gnd', unit='°C')
      create_metric_card('Температура ИУ', 'temp_vdd', unit='°C')
      create_metric_card('Освещенность (DA22)', 'light_gnd', unit='лк')
      create_metric_card('Освещенность (DA23)', 'light_vdd', unit='лк')

    ui.label('Environment Voltages').classes(
        'text-xs font-semibold text-slate-400 mt-1'
    )
    with ui.row().classes('w-full gap-1'):
      for idx, name in enumerate(
          ['P3V3', 'P5V0', 'VIN', 'VDOUT1', 'P40V', 'USB', 'VDOUT2', 'VDOUT3']
      ):
        create_metric_card(name, f'env_{idx}', unit='В')

    ui.label('AO Channels (1-18)').classes(
        'text-xs font-semibold text-slate-400 mt-1'
    )
    with ui.grid(columns=6).classes('w-full gap-1'):
      for i in range(18):
        create_metric_card(f'AO {i+1}', f'ao_{i}', unit='В')

    ui.label('AIN VSense (1-6)').classes(
        'text-xs font-semibold text-slate-400 mt-1'
    )
    with ui.grid(columns=6).classes('w-full gap-1'):
      for i in range(6):
        create_metric_card(f'VSense {i+1}', f'vsense_{i}', unit='В')

    # --- ДАТЧИКИ ТОКА И НАПРЯЖЕНИЯ ---
    ui.label('Датчики тока и напряжения (1-8)').classes(
        'text-xs font-semibold text-slate-400 mt-1'
    )
    with ui.grid(columns=4).classes('w-full gap-1'):
      for i in range(1, 9):
        with ui.card().classes(
            'bg-slate-700 py-2.5 px-3 rounded flex flex-col justify-between cursor-default'
        ):
          ui.label(f'Датчик тока {i}').classes(
              'text-xs text-yellow-300 font-bold leading-none mb-1.5'
          )

          with ui.row().classes('w-full justify-between items-center'):
            ui.label('U:').classes('text-[11px] text-slate-300 leading-none')
            with ui.row().classes('items-center gap-1'):
              ui_labels[f'curr_v_{i}'] = ui.label('0.00').classes(
                  'text-sm font-mono text-emerald-400 font-bold leading-none'
              )
              ui.label('В').classes('text-[11px] text-slate-400 leading-none')

          with ui.row().classes('w-full justify-between items-center mt-1.5'):
            ui.label('I:').classes('text-[11px] text-slate-300 leading-none')
            with ui.row().classes('items-center gap-1'):
              ui_labels[f'curr_i_{i}'] = ui.label('0.0000').classes(
                  'text-sm font-mono text-sky-400 font-bold leading-none'
              )
              ui.label('А').classes('text-[11px] text-slate-400 leading-none')

# Активируем функцию Drag-and-Drop для главного контейнера (требует NiceGUI >= 3.11)
main_sortable_container.make_sortable()


# --- ФОНОВЫЙ ПОТОК ЧТЕНИЯ ТЕЛЕМЕТРИИ ---
async def telemetry_reader_loop():
  global serial_buffer
  while True:
    await asyncio.sleep(0.02)
    if tel_ser and tel_ser.is_open:
      try:
        data = tel_ser.read(tel_ser.in_waiting or 1)
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
                ui_labels[f'vsense_{i}'].set_text(f'{unpacked[27+i]:.2f}')
              for i in range(8):
                ui_labels[f'env_{i}'].set_text(f'{unpacked[43+i]:.2f}')

              curr_offset = 51
              for i in range(1, 9):
                v_val = unpacked[curr_offset]
                i_val = unpacked[curr_offset + 1]

                ui_labels[f'curr_v_{i}'].set_text(f'{v_val:.2f}')
                ui_labels[f'curr_i_{i}'].set_text(f'{i_val:.4f}')

                curr_offset += 2

              serial_buffer = serial_buffer[PACKET_SIZE:]
            else:
              serial_buffer.pop(0)
      except Exception as e:
        print(f'Ошибка чтения телеметрии: {e}')


ui.timer(0.1, telemetry_reader_loop, once=True)

ui.run(port=8080, title='UTS2 Dashboard', favicon=None)