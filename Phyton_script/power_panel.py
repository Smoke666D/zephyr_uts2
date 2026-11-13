from nicegui import ui

# Поддерживаемые каналы подсистемы OUTPut:POWer
ENABLE_LINES = [
    'DUT2_PSU',
    'DUT3_PSU',
    'P12V',
    'P40V',
    'VA',
    'USB_OUT',
    'USB_BOOT',
    'USB_TOP',
]


def build_power_panel(ports_refs):
    """Строит карточку управления питанием (SCPI OUTPut:POWer)"""
    shell_ref = ports_refs['shell']
    enable_checkboxes = []
    bulk_updating = False

    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        ui.label('Управление питанием (OUTPut:POWer)').classes(
            'text-sm font-bold text-yellow-300 text-center w-full mb-2 tracking-wide'
        )

        def pwr_all_on():
            nonlocal bulk_updating
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
                curr_shell.write(b'OUTP:POW:ALL ON\r\n')
                ui.notify('SCPI: OUTP:POW:ALL ON', type='positive')
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

            bulk_updating = True
            for cb in enable_checkboxes:
                cb.value = True
            bulk_updating = False

        def pwr_all_off():
            nonlocal bulk_updating
            curr_shell = shell_ref.get('ser')
            if curr_shell and curr_shell.is_open:
                curr_shell.write(b'OUTP:POW:ALL OFF\r\n')
                ui.notify('SCPI: OUTP:POW:ALL OFF', type='info')
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

            bulk_updating = True
            for cb in enable_checkboxes:
                cb.value = False
            bulk_updating = False

        def make_en_handler(c_name):
            def handler(e):
                if bulk_updating:
                    return
                state = 'ON' if e.value else 'OFF'
                cmd = f'OUTP:POW {c_name}, {state}\r\n'
                curr_shell = shell_ref.get('ser')
                if curr_shell and curr_shell.is_open:
                    curr_shell.write(cmd.encode('utf-8'))
                    ui.notify(f'[{c_name}] SCPI: {cmd.strip()}', type='info')
                else:
                    ui.notify('SCPI порт закрыт!', type='warning')

            return handler

        with ui.row().classes('w-full gap-3 items-center'):
            with ui.grid(columns=4).classes('flex-1 gap-1'):
                for idx, name in enumerate(ENABLE_LINES):
                    ch_num = idx + 1
                    cb = ui.checkbox(
                        f'{ch_num}. {name}',
                        value=False,
                        on_change=make_en_handler(name),
                    ).classes('text-white text-xs')
                    enable_checkboxes.append(cb)

            with ui.column().classes('gap-1.5 w-44 justify-center'):
                ui.button(
                    'ВКЛЮЧИТЬ ВСЕ',
                    on_click=pwr_all_on,
                ).props('color=positive text-color=white').classes(
                    'w-full font-bold text-xs py-2 rounded shadow bg-green-600'
                )

                ui.button(
                    'ВЫКЛЮЧИТЬ ВСЕ',
                    on_click=pwr_all_off,
                ).props('color=negative text-color=white').classes(
                    'w-full font-bold text-xs py-2 rounded shadow bg-red-600'
                )