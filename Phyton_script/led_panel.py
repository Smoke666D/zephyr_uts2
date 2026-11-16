from nicegui import ui

SCPI_LEDS = [
    {'target': 'RED', 'name': 'Красный'},
    {'target': 'GRE', 'name': 'Зеленый'},
    {'target': 'BLU', 'name': 'Синий'},
]


def build_led_panel(ports_refs):
  """Строит панель управления светодиодами (SCPI DISPlay:LED:STATe)"""
  shell_ref = ports_refs['shell']

  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Подсистема индикации (DISPlay:LED)').classes(
        'text-xs font-semibold text-yellow-300 mb-1'
    )

    def make_scpi_led_handler(target_name, led_label):

      def handler(e):
        state_val = 'ON' if e.value else 'OFF'
        cmd = f'DISP:LED:STAT {target_name}, {state_val}\r\n'
        curr_shell = shell_ref.get('ser')
        if curr_shell and curr_shell.is_open:
          curr_shell.write(cmd.encode('utf-8'))
          ui.notify(f'[{led_label}] SCPI: {cmd.strip()}', type='info')
        else:
          ui.notify('SCPI порт закрыт!', type='warning')

      return handler

    with ui.row().classes('w-full gap-6 items-center px-1'):
      for item in SCPI_LEDS:
        ui.checkbox(
            item['name'],
            value=False,
            on_change=make_scpi_led_handler(item['target'], item['name']),
        ).classes('text-white text-xs')