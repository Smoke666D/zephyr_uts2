from nicegui import ui


def build_dout_panel(ports_refs):
  """Строит панель управления 18 дискретными драйверами (SCPI OUTPut:DOUT 1–18)"""
  shell_ref = ports_refs['shell']

  with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
    ui.label('Управление дискретными драйверами (OUTPut:DOUT 1–18)').classes(
        'text-sm font-bold text-yellow-300 text-center w-full mb-2'
        ' tracking-wide'
    )

    def on_dout_change(ch_num, select_element):
      state = select_element.value
      cmd = f'OUTP:DOUT {ch_num}, {state}\r\n'
      curr_shell = shell_ref.get('ser')
      if curr_shell and curr_shell.is_open:
        curr_shell.write(cmd.encode('utf-8'))
        ui.notify(f'[DOUT {ch_num}] SCPI: {cmd.strip()}', type='info')
      else:
        ui.notify('SCPI порт закрыт!', type='warning')

    with ui.grid(columns=6).classes('w-full gap-1'):
      for i in range(1, 19):
        with ui.card().classes(
            'bg-slate-700 p-1 flex-row items-center justify-between rounded'
        ):
          ui.label(f'DOUT {i}').classes('text-[11px] text-white font-bold')

          if i <= 16:
            options_list = ['IN', 'LOW', 'HIGH', 'SHORT']
          else:
            options_list = ['IN', 'LOW', 'HIGH']

          dout_select = (
              ui.select(options=options_list, value='IN')
              .props('dark outlined dense')
              .classes('w-28 bg-slate-800 text-[10px] text-white')
          )
          dout_select.on(
              'update:model-value',
              lambda e, c=i, s=dout_select: on_dout_change(c, s),
          )