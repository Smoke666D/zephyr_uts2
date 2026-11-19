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
    """Строит компактную однострочную карточку управления питанием"""
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 py-1.5 px-3 w-full cursor-grab'):
        enable_checkboxes = []
        bulk_updating = False

        ui.label('Управление питанием (OUTPut:POWer)').classes(
            'text-xs font-bold text-yellow-300 text-center w-full mb-1 tracking-wide leading-none'
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

        # Единая горизонтальная линия: чекбоксы равномерно слева + кнопки размера w-44 справа
        with ui.row().classes('w-full items-center justify-between gap-4 flex-nowrap'):
            
            # 8 чекбоксов равномерно распределены по всему оставшемуся месту (flex-1 justify-between)
            with ui.row().classes('flex-1 items-center justify-between pr-2 flex-nowrap'):
                for idx, name in enumerate(ENABLE_LINES):
                    ch_num = idx + 1
                    cb = ui.checkbox(
                        f'{ch_num}.{name}',
                        value=False,
                        on_change=make_en_handler(name),
                    ).props('dense').classes('text-white text-xs font-medium whitespace-nowrap')
                    enable_checkboxes.append(cb)

            # Кнопки точно такого же размера (w-44 py-1.5 text-xs), как в панели портов
            with ui.row().classes('gap-2 items-center flex-nowrap'):
                ui.button(
                    'ВКЛЮЧИТЬ ВСЕ',
                    on_click=pwr_all_on,
                ).props('color=positive text-color=white').classes(
                    'w-44 font-bold text-xs py-1.5 rounded shadow bg-green-600 whitespace-nowrap'
                )

                ui.button(
                    'ВЫКЛЮЧИТЬ ВСЕ',
                    on_click=pwr_all_off,
                ).props('color=negative text-color=white').classes(
                    'w-44 font-bold text-xs py-1.5 rounded shadow bg-red-600 whitespace-nowrap'
                )