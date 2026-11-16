from nicegui import ui


def build_sensors_panel(ui_labels):
    """Строит дашборд основных датчиков, напряжений Environment, AO каналов и VSense"""

    def create_metric_card(title, key, unit=''):
        with ui.card().classes(
            'bg-slate-700 py-3 px-3 flex-1 min-w-[130px] rounded flex flex-row '
            'justify-between items-center cursor-default'
        ):
            ui.label(title).classes('text-xs text-slate-300 font-medium leading-none m-0')
            with ui.row().classes('items-center gap-1 m-0'):
                lbl = ui.label('0.00').classes(
                    'text-base font-mono text-emerald-400 font-bold leading-none m-0'
                )
                if unit:
                    ui.label(unit).classes('text-xs text-slate-400 font-medium leading-none m-0')
            ui_labels[key] = lbl

    with ui.column().classes('w-full gap-2 cursor-grab'):
        ui.label('Дашборд датчиков').classes('text-sm font-bold text-sky-300')

        # Основные датчики платы
        with ui.row().classes('w-full gap-1'):
            create_metric_card('RF Power', 'rf_power', unit='дБ')
            create_metric_card('NTC', 'ntc')
            create_metric_card('Ext VSense', 'ext_vsense')
            create_metric_card('Brd Detect', 'brd_detect')
            create_metric_card('Температура МУ', 'temp_gnd', unit='°C')
            create_metric_card('Температура ИУ', 'temp_vdd', unit='°C')
            create_metric_card('Освещенность (DA22)', 'light_gnd', unit='лк')
            create_metric_card('Освещенность (DA23)', 'light_vdd', unit='лк')

        # Шины вторичного питания (Environment)
        ui.label('Environment Voltages').classes('text-xs font-semibold text-slate-400 mt-1')
        with ui.row().classes('w-full gap-1'):
            for idx, name in enumerate([
                'P3V3',
                'P5V0',
                'VIN',
                'VDOUT1',
                'P40V',
                'USB',
                'VDOUT2',
                'VDOUT3',
            ]):
                create_metric_card(name, f'env_{idx}', unit='В')

        # Аналоговые каналы AO (1-18)
        ui.label('AO Channels (1-18)').classes('text-xs font-semibold text-slate-400 mt-1')
        with ui.grid(columns=6).classes('w-full gap-1'):
            for i in range(18):
                create_metric_card(f'AO {i+1}', f'ao_{i}', unit='В')

        # Измерительные линии AIN VSense (1-6)
        ui.label('AIN VSense (1-6)').classes('text-xs font-semibold text-slate-400 mt-1')
        with ui.grid(columns=6).classes('w-full gap-1'):
            for i in range(6):
                create_metric_card(f'VSense {i+1}', f'vsense_{i}', unit='В')