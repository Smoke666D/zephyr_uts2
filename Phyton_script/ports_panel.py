import asyncio
import inspect
from nicegui import ui
import serial
from serial.tools import list_ports


def get_available_ports():
    """Сканирует систему и возвращает список доступных COM-портов"""
    ports = [p.device for p in list_ports.comports()]
    return ports if ports else ['Нет портов']


def build_ports_panel(ports_refs, on_device_connected=None):
    """
    Компактная панель управления портами связи в одну линию
    с узкими селектами портов:
    [Телеметрия] [SCPI / Команды] [Кнопки: Обновить / Автопоиск]
    """
    tel_ref = ports_refs['tel']
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2 w-full cursor-grab'):
        with ui.row().classes('w-full gap-2 items-center'):

            # --- 1. Карточка Телеметрии (Узкий селект w-28) ---
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600'):
                ui.label('Телеметрия (Бинарный)').classes('text-xs font-semibold text-sky-300 mb-1')

                ports_list = get_available_ports()
                with ui.row().classes('w-full items-center gap-2'):
                    tel_select = ui.select(
                        options=ports_list,
                        value=ports_list[0] if ports_list else None,
                        label='Порт',
                    ).props('dark outlined dense color=sky-400').classes('w-28 bg-slate-700 rounded text-xs')

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
                                    p_name, baudrate=3000000, timeout=0.01
                                )
                                tel_btn.text = 'Закрыть'
                                tel_btn.classes(remove='bg-sky-600', add='bg-red-600')
                                ui.notify(f'Телеметрия: {p_name}', type='positive')
                            except Exception as e:
                                ui.notify(f'Ошибка: {e}', type='negative')

                    tel_btn = ui.button('Открыть', on_click=toggle_tel).classes(
                        'bg-sky-600 hover:bg-sky-500 text-white text-xs font-bold py-1.5 px-3 rounded shadow whitespace-nowrap'
                    )

            # --- 2. Карточка SCPI / Команды (Узкий селект w-28) ---
            with ui.card().classes('bg-slate-700/50 p-2 flex-1 rounded border border-slate-600'):
                ui.label('SCPI / Команды').classes('text-xs font-semibold text-emerald-300 mb-1')

                with ui.row().classes('w-full items-center gap-2'):
                    shell_select = ui.select(
                        options=ports_list,
                        value=ports_list[0] if ports_list else None,
                        label='Порт',
                    ).props('dark outlined dense color=emerald-400').classes('w-28 bg-slate-700 rounded text-xs')

                    def toggle_shell():
                        curr = shell_ref.get('ser')
                        if curr and curr.is_open:
                            curr.close()
                            shell_ref['ser'] = None
                            shell_btn.text = 'Открыть'
                            shell_btn.classes(remove='bg-red-600', add='bg-emerald-600')
                            ui.notify('SCPI порт закрыт', type='warning')
                        else:
                            try:
                                p_name = shell_select.value
                                if not p_name or p_name == 'Нет портов':
                                    raise ValueError('Порт не выбран!')
                                shell_ref['ser'] = serial.Serial(
                                    p_name, baudrate=115200, timeout=0.05
                                )
                                shell_btn.text = 'Закрыть'
                                shell_btn.classes(remove='bg-emerald-600', add='bg-red-600')
                                ui.notify(f'SCPI открыт на {p_name}', type='positive')
                            except Exception as e:
                                ui.notify(f'Ошибка: {e}', type='negative')

                    shell_btn = ui.button('Открыть', on_click=toggle_shell).classes(
                        'bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-bold py-1.5 px-3 rounded shadow whitespace-nowrap'
                    )

            # --- 3. Кнопки «Обновить» и «Автопоиск» справа ---
            with ui.column().classes('gap-1.5 w-44 justify-center'):
                def refresh_ports():
                    p = get_available_ports()
                    tel_select.options = p
                    shell_select.options = p
                    ui.notify('Порты обновлены', type='info')

                ui.button('Обновить порты', on_click=refresh_ports).classes(
                    'w-full bg-slate-700 hover:bg-slate-600 text-white font-bold text-xs py-1.5 rounded shadow'
                )

                async def global_auto_scan():
                    ui.notify('Автопоиск портов (SCPI *IDN? + Телеметрия)...', type='info')
                    current_ports = get_available_ports()
                    tel_select.options = current_ports
                    shell_select.options = current_ports
                    found_tel, found_shell = None, None

                    for p_name in current_ports:
                        if p_name == 'Нет портов':
                            continue

                        # Поиск Телеметрии (0xBE)
                        if not found_tel:
                            try:
                                s = serial.Serial(p_name, baudrate=3000000, timeout=0.05)
                                s.reset_input_buffer()
                                start_t = asyncio.get_event_loop().time()
                                is_tel = False
                                while (asyncio.get_event_loop().time() - start_t) < 0.6:
                                    if s.in_waiting > 0:
                                        if b'\xbe' in s.read(s.in_waiting):
                                            is_tel = True
                                            break
                                    await asyncio.sleep(0.04)
                                s.close()
                                if is_tel:
                                    found_tel = p_name
                                    continue
                            except Exception:
                                pass

                        # Поиск SCPI порта по запросу *IDN?
                        if not found_shell:
                            try:
                                s = serial.Serial(p_name, baudrate=115200, timeout=0.2)
                                s.reset_input_buffer()
                                s.reset_output_buffer()
                                s.write(b'*IDN?\r\n')
                                await asyncio.sleep(0.15)

                                resp = ""
                                if s.in_waiting > 0:
                                    resp = s.read(s.in_waiting).decode('utf-8', errors='ignore')

                                if 'Author-ALARM,UST2' in resp:
                                    print(f'[SCPI DETECT]: Найден прибор на {p_name}! Ответ: {resp.strip()}')
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

                        curr_scpi = shell_ref.get('ser')
                        if curr_scpi and curr_scpi.is_open:
                            curr_scpi.write(b'*RST\r\n')
                            print('[SCPI TX]: *RST отправлен')

                        ui.notify(f'SCPI найден на {found_shell} (Выполнен *RST)', type='positive')

                        # Безопасный вызов и ожидание коллбэка
                        if on_device_connected:
                            await asyncio.sleep(0.2)
                            res = on_device_connected()
                            if inspect.isawaitable(res):
                                await res
                    else:
                        ui.notify('SCPI порт (Author-ALARM,UST2) не найден!', type='warning')

                ui.button('Автопоиск портов', on_click=global_auto_scan).classes(
                    'w-full bg-amber-600 hover:bg-amber-500 text-white font-bold text-xs py-1.5 rounded shadow'
                )