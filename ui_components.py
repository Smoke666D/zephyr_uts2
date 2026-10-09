import asyncio
from nicegui import ui
import serial
from serial.tools import list_ports

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
LED_LINES = [
    {'id': 1, 'name': 'Красный'},
    {'id': 2, 'name': 'Зеленый'},
    {'id': 3, 'name': 'Синий'},
]

# Глобальные словари для связи логов с фоновым потоком
can_logs_dict = {}
lin_logs_dict = {}
raw_logs_dict = {}


def get_available_ports():
  ports = [p.device for p in list_ports.comports()]
  return ports if ports else ['Нет портов']


def build_dashboard(ports_refs, ui_labels):
  tel_ref = ports_refs['tel']
  shell_ref = ports_refs['shell']

  with ui.column().classes('w-full gap-2') as container:

    # --- БЛОК 1: ПОРТЫ ---
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
      with ui.row().classes('w-full gap-2'):
        # Телеметрия
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

          def toggle_tel():
            curr = tel_ref.get('ser')
            if curr and curr.is_open:
              curr.close()
              tel_ref['ser'] = None
              tel_btn.text = 'Открыть'
              tel_btn.classes(remove='bg-red-600', add='bg-sky-600')
              ui.notify('Телеметрия закрыта', type='warning')
            else:
              try:
                p_name = tel_select.value
                if not p_name or p_name == 'Нет портов':
                  raise ValueError('Порт не выбран!')
                tel_ref['ser'] = serial.Serial(
                    p_name, baudrate=115200, timeout=0.01
                )
                tel_btn.text = 'Закрыть'
                tel_btn.classes(remove='bg-sky-600', add='bg-red-600')
                ui.notify(f'Телеметрия: {p_name}', type='positive')
              except Exception as e:
                ui.notify(f'Ошибка: {e}', type='negative')

          tel_btn = ui.button('Открыть', on_click=toggle_tel).classes(
              'mt-2 bg-sky-600 text-white text-xs py-1 px-2'
          )

        # Shell
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

          def toggle_shell():
            curr = shell_ref.get('ser')
            if curr and curr.is_open:
              curr.close()
              shell_ref['ser'] = None
              shell_btn.text = 'Открыть'
              shell_btn.classes(remove='bg-red-600', add='bg-emerald-600')
              ui.notify('Shell закрыт', type='warning')
            else:
              try:
                p_name = shell_select.value
                if not p_name or p_name == 'Нет портов':
                  raise ValueError('Порт не выбран!')
                shell_ref['ser'] = serial.Serial(
                    p_name, baudrate=115200, timeout=0.01
                )
                shell_btn.text = 'Закрыть'
                shell_btn.classes(remove='bg-emerald-600', add='bg-red-600')
                ui.notify(f'Shell открыт на {p_name}', type='positive')
              except Exception as e:
                ui.notify(f'Ошибка: {e}', type='negative')

          shell_btn = ui.button('Открыть', on_click=toggle_shell).classes(
              'mt-2 bg-emerald-600 text-white text-xs py-1 px-2'
          )

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
          if not (tel_ref.get('ser') and tel_ref['ser'].is_open):
            toggle_tel()
          ui.notify(f'Телеметрия: {found_tel}', type='positive')
        if found_shell:
          shell_select.value = found_shell
          if not (shell_ref.get('ser') and shell_ref['ser'].is_open):
            toggle_shell()
          ui.notify(f'Shell: {found_shell}', type='positive')

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

    # --- БЛОК 2: ENABLE ---
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
      ui.label('Управление линиями enable').classes(
          'text-xs font-semibold text-yellow-300 mb-1'
      )

      def make_en_handler(c_num, c_name):
        def handler(e):
          state = 'ON' if e.value else 'OFF'
          cmd = f'en_set {c_num} {state}\r\n'
          curr_shell = shell_ref.get('ser')
          if curr_shell and curr_shell.is_open:
            curr_shell.write(cmd.encode('utf-8'))
            ui.notify(f'[{c_name}] Отправлено: {cmd.strip()}', type='info')
          else:
            ui.notify('Shell порт закрыт!', type='warning')

        return handler

      with ui.grid(columns=4).classes('w-full gap-1'):
        for idx, name in enumerate(ENABLE_LINES):
          ch_num = idx + 1
          ui.checkbox(
              f'{ch_num}. {name}',
              value=True,
              on_change=make_en_handler(ch_num, name),
          ).classes('text-white text-xs')

    # --- БЛОК 3: СВЕТОДИОДЫ ---
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
      ui.label('Управление светодиодами').classes(
          'text-xs font-semibold text-yellow-300 mb-1'
      )

      def make_led_handler(l_id, l_name):
        def handler(e):
          state = 'ON' if e.value else 'OFF'
          cmd = f'led_set {l_id} {state}\r\n'
          curr_shell = shell_ref.get('ser')
          if curr_shell and curr_shell.is_open:
            curr_shell.write(cmd.encode('utf-8'))
            ui.notify(
                f'[Светодиод {l_name}] Отправлено: {cmd.strip()}', type='info'
            )
          else:
            ui.notify('Shell порт закрыт!', type='warning')

        return handler

      with ui.row().classes('w-full gap-4'):
        for led in LED_LINES:
          ui.checkbox(
              f'{led["id"]}. {led["name"]}',
              value=False,
              on_change=make_led_handler(led['id'], led['name']),
          ).classes('text-white text-xs')

    # --- БЛОК 4: IO (1-18) ---
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
      ui.label('Управление линиями ввода/вывода (IO 1-18)').classes(
          'text-xs font-semibold text-yellow-300 mb-1'
      )

      def on_io_change(ch_num, select_element):
        val_map = {'INPUT': 'IN', 'HIGH': 'HI', 'LOW': 'LO'}
        short_state = val_map.get(select_element.value, 'IN')
        cmd = f'out_set {ch_num} {short_state}\r\n'
        curr_shell = shell_ref.get('ser')
        if curr_shell and curr_shell.is_open:
          curr_shell.write(cmd.encode('utf-8'))
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
                'w-32 bg-slate-800 text-[10px] text-white'
            )
            io_select.on(
                'update:model-value',
                lambda e, c=i, s=io_select: on_io_change(c, s),
            )

    # --- БЛОК 5: ЦАП И РЕЗИСТОРЫ ---
    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
      ui.label('Управление ЦАП и подстроечными резисторами').classes(
          'text-xs font-semibold text-yellow-300 mb-1.5'
      )
      with ui.row().classes('w-full gap-4 items-center'):
        # ЦАП
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
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                  curr_shell.write(cmd.encode('utf-8'))
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

        # Резистор 1
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
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                  curr_shell.write(cmd.encode('utf-8'))
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
              ui.notify(
                  'Введите корректное число для Резистора 1', type='negative'
              )

          res1_input.on('keydown.enter', send_res1)
          ui.button('Set R1', on_click=send_res1).classes(
              'bg-emerald-600 text-white text-xs py-1 px-2'
          )

        # Резистор 2
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
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                  curr_shell.write(cmd.encode('utf-8'))
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
              ui.notify(
                  'Введите корректное число для Резистора 2', type='negative'
              )

          res2_input.on('keydown.enter', send_res2)
          ui.button('Set R2', on_click=send_res2).classes(
              'bg-emerald-600 text-white text-xs py-1 px-2'
          )

    # --- БЛОК 6: 3 CAN ИНФЕЙСА ---
    for can_idx in range(1, 4):
      with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        ui.label(f'CAN Интерфейс {can_idx}').classes(
            'text-xs font-semibold text-yellow-300 mb-1'
        )

        with ui.row().classes('w-full gap-2 items-center'):
          can_speed_data = ui.select(
              options=['1000', '500', '250', '125'],
              value='1000',
              label='Скорость Data (кб/с)',
          ).props('dark outlined dense').classes(
              'w-36 bg-slate-700 text-xs text-white rounded'
          )
          can_speed_data.disable()

          def update_can_mode(e, ds=can_speed_data):
            if e.value == 'CANFD':
              ds.enable()
            else:
              ds.disable()
              ds.set_value('1000')

          can_mode = ui.select(
              options=['Classic', 'CANFD'],
              value='Classic',
              label='Режим',
              on_change=update_can_mode,
          ).props('dark outlined dense').classes(
              'w-28 bg-slate-700 text-xs text-white rounded'
          )

          can_speed_nom = ui.select(
              options=['1000', '500', '250', '125'],
              value='500',
              label='Скорость (кб/с)',
          ).props('dark outlined dense').classes(
              'w-32 bg-slate-700 text-xs text-white rounded'
          )

        ui.label('Входящие сообщения:').classes(
            'text-[10px] text-slate-400 mt-1 mb-0.5'
        )
        can_log = ui.log(max_lines=30).classes(
            'w-full h-20 bg-slate-900 text-green-400 font-mono text-[11px] p-1 rounded'
        )
        can_log.push(f'CAN{can_idx} готов к работе...')
        can_logs_dict[can_idx] = can_log

        with ui.row().classes('w-full gap-2 items-center mt-1.5'):
          can_id_input = ui.input(placeholder='ID (например, 123)').props(
              'dark outlined dense input-class="text-xs"'
          ).classes('w-36 bg-slate-700 text-xs text-white rounded')

          can_data_input = ui.input(
              placeholder='Данные через пробел (например: 11 22 AA)'
          ).props('dark outlined dense input-class="text-xs"').classes(
              'flex-1 bg-slate-700 text-xs text-white rounded'
          )

          def send_can_msg(
              c_idx=can_idx,
              id_inp=can_id_input,
              data_inp=can_data_input,
              log=can_log,
          ):
            cid = id_inp.value.strip()
            cdata = data_inp.value.strip()
            if not cid:
              ui.notify(f'CAN{c_idx}: Укажите ID сообщения!', type='warning')
              return

            cmd = f'can send {c_idx} {cid} {cdata}\r\n'
            curr_shell = shell_ref.get('ser')

            if curr_shell and curr_shell.is_open:
              curr_shell.write(cmd.encode('utf-8'))
              log.push(f'TX -> ID: {cid} | DATA: {cdata}')
              id_inp.set_value('')
              data_inp.set_value('')
            else:
              ui.notify(
                  'Shell порт закрыт! Невозможно отправить пакет',
                  type='warning',
              )

          can_id_input.on('keydown.enter', send_can_msg)
          can_data_input.on('keydown.enter', send_can_msg)
          ui.button('Отправить', on_click=send_can_msg).classes(
              'bg-sky-600 text-white text-xs py-1 px-3 font-bold'
          )

    # --- БЛОК 7: ДАШБОРД ДАТЧИКОВ ---
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

      # Датчики тока
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
                ui.label('А').classes(
                    'text-[11px] text-slate-400 leading-none'
                )

    # --- БЛОК 8: 4 LIN ИНФЕЙСА ---
# --- БЛОК 8: 4 LIN ИНФЕЙСА ---
    ui.label('Интерфейсы LIN (1-4)').classes(
        'text-sm font-bold text-sky-300 mt-2'
    )

    def make_lin_block(l_idx):
      with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        with ui.row().classes('w-full justify-between items-center mb-1'):
          ui.label(f'LIN Интерфейс {l_idx}').classes(
              'text-xs font-semibold text-yellow-300'
          )

          # Функция-обработчик для конкретного индекса l_idx через аргумент по умолчанию
          def handle_pull_down(e, idx=l_idx):
            state = 'ON' if e.value else 'OFF'
            cmd = f'linpb_set {idx} {state}\r\n'
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
              curr_shell.write(cmd.encode('utf-8'))
              ui.notify(f'[LIN{idx} PULL_DOWN] {state}', type='info')
            else:
              ui.notify('Shell порт закрыт!', type='warning')

          # Используем штатный on_change для корректной поимки клика
          ui.checkbox(
              'PULL_DOWN', value=False, on_change=handle_pull_down
          ).classes('text-white text-xs')

          with ui.row().classes('items-center gap-1'):
            ui.label('U:').classes('text-[11px] text-slate-300')
            lin_v_lbl = ui.label('0.00').classes(
                'text-xs font-mono text-emerald-400 font-bold'
            )
            ui.label('В').classes('text-[11px] text-slate-400')
            ui_labels[f'lin_v_{l_idx}'] = lin_v_lbl

        ui.label('Входящие пакеты:').classes(
            'text-[10px] text-slate-400 mt-1 mb-0.5'
        )
        lin_log = ui.log(max_lines=20).classes(
            'w-full h-16 bg-slate-900 text-cyan-400 font-mono text-[11px] p-1 rounded'
        )
        lin_log.push(f'LIN{l_idx} готов...')
        lin_logs_dict[l_idx] = lin_log

        with ui.row().classes('w-full gap-2 items-center mt-1.5'):
          lin_data_input = ui.input(
              placeholder='Данные через пробел (например: 10 20 AA)'
          ).props('dark outlined dense input-class="text-xs"').classes(
              'flex-1 bg-slate-700 text-xs text-white rounded'
          )

          def send_lin_msg(idx=l_idx, data_inp=lin_data_input, log=lin_log):
            ldata = data_inp.value.strip()
            if not ldata:
              ui.notify(f'LIN{idx}: Введите данные для отправки!', type='warning')
              return

            cmd = f'lin_send {idx} {ldata}\r\n'
            curr_shell = shell_ref.get('ser')

            if curr_shell and curr_shell.is_open:
              curr_shell.write(cmd.encode('utf-8'))
              log.push(f'TX -> {ldata}')
              data_inp.set_value('')
            else:
              ui.notify('Shell порт закрыт!', type='warning')

          lin_data_input.on('keydown.enter', send_lin_msg)
          ui.button('Отправить', on_click=send_lin_msg).classes(
              'bg-sky-600 text-white text-xs py-1 px-3 font-bold'
          )

    for lin_idx in range(1, 5):
      make_lin_block(lin_idx)

  container.make_sortable()
  return can_logs_dict, lin_logs_dict