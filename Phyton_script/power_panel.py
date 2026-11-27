import asyncio
import time
from nicegui import ui

# Поддерживаемые каналы подсистемы OUTPut:POWer (строго в порядке ответа OUTP:POW?)
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

# Состояние панели для доступа из функции синхронизации
_panel_state = {
    'checkboxes': [],
    'bulk_updating': False,
}


def _notify_safe(msg: str, type: str = 'info'):
    try:
        ui.notify(msg, type=type)
    except Exception:
        print(f'[POWER {type.upper()}]: {msg}')


async def sync_power_states_async(shell_ref):
    """Отправляет OUTP:POW?, разбирает ответ и актуализирует 8 чекбоксов."""
    curr_shell = shell_ref.get('ser')
    if not curr_shell or not getattr(curr_shell, 'is_open', False):
        return False

    try:
        curr_shell.reset_input_buffer()
        curr_shell.write(b'OUTP:POW?\r\n')
        print('[SCPI TX]: OUTP:POW?')

        start_t = time.time()
        accum = ''
        resp_line = ''

        # Ждем полную строку с переводом строки \n и 8 значениями через запятую
        while time.time() - start_t < 1.0:
            if curr_shell.in_waiting > 0:
                accum += curr_shell.read(curr_shell.in_waiting).decode(
                    'utf-8', errors='ignore'
                )
                if '\n' in accum:
                    lines = [l.strip() for l in accum.splitlines() if l.strip()]
                    for l in lines:
                        if 'OUTP:POW?' in l.upper():
                            continue
                        parts = [p.strip() for p in l.split(',')]
                        if len(parts) >= len(ENABLE_LINES):
                            resp_line = l
                            break
                    if resp_line:
                        break
            await asyncio.sleep(0.03)

        if not resp_line:
            print(f'[POWER SYNC TIMEOUT]: Нет ответа на OUTP:POW?. Буфер: {repr(accum)}')
            return False

        print(f'[SCPI RX]: {resp_line}')
        parts = [p.strip() for p in resp_line.split(',')]

        # Обновляем чекбоксы без отправки команд обратно в прибор
        _panel_state['bulk_updating'] = True
        try:
            for idx, cb in enumerate(_panel_state['checkboxes']):
                if idx < len(parts):
                    cb.value = parts[idx] in ('1', 'ON', 'on', 'True', 'true')
        finally:
            _panel_state['bulk_updating'] = False

        _notify_safe(f'Питание синхронизировано: {resp_line}', type='info')
        return True

    except Exception as e:
        print(f'[POWER SYNC ERROR]: {e}')
        return False


def build_power_panel(ports_refs):
    """Строит компактную однострочную карточку управления питанием"""
    shell_ref = ports_refs['shell']
    _panel_state['checkboxes'] = []

    with ui.card().classes('bg-slate-800 py-1.5 px-3 w-full cursor-grab'):
        ui.label('Управление питанием (OUTPut:POWer)').classes(
            'text-xs font-bold text-yellow-300 text-center w-full mb-1 tracking-wide leading-none'
        )

        def pwr_all_on():
            curr_shell = shell_ref.get('ser')
            if curr_shell and getattr(curr_shell, 'is_open', False):
                curr_shell.write(b'OUTP:POW:ALL ON\r\n')
                ui.notify('SCPI: OUTP:POW:ALL ON', type='positive')
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

            _panel_state['bulk_updating'] = True
            try:
                for cb in _panel_state['checkboxes']:
                    cb.value = True
            finally:
                _panel_state['bulk_updating'] = False

        def pwr_all_off():
            curr_shell = shell_ref.get('ser')
            if curr_shell and getattr(curr_shell, 'is_open', False):
                curr_shell.write(b'OUTP:POW:ALL OFF\r\n')
                ui.notify('SCPI: OUTP:POW:ALL OFF', type='info')
            else:
                ui.notify('SCPI порт закрыт!', type='warning')

            _panel_state['bulk_updating'] = True
            try:
                for cb in _panel_state['checkboxes']:
                    cb.value = False
            finally:
                _panel_state['bulk_updating'] = False

        def make_en_handler(c_name):
            def handler(e):
                if _panel_state['bulk_updating']:
                    return
                state = 'ON' if e.value else 'OFF'
                cmd = f'OUTP:POW {c_name}, {state}\r\n'
                curr_shell = shell_ref.get('ser')
                if curr_shell and getattr(curr_shell, 'is_open', False):
                    curr_shell.write(cmd.encode('utf-8'))
                    ui.notify(f'[{c_name}] SCPI: {cmd.strip()}', type='info')
                else:
                    ui.notify('SCPI порт закрыт!', type='warning')

            return handler

        # Единая горизонтальная линия: чекбоксы равномерно слева + кнопки размера w-44 справа
        with ui.row().classes('w-full items-center justify-between gap-4 flex-nowrap'):

            # 8 чекбоксов равномерно распределены по всему оставшемуся месту
            with ui.row().classes('flex-1 items-center justify-between pr-2 flex-nowrap'):
                for idx, name in enumerate(ENABLE_LINES):
                    ch_num = idx + 1
                    cb = ui.checkbox(
                        f'{ch_num}.{name}',
                        value=False,
                        on_change=make_en_handler(name),
                    ).props('dense').classes('text-white text-xs font-medium whitespace-nowrap')
                    _panel_state['checkboxes'].append(cb)

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