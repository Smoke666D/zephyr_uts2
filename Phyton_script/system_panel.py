import asyncio
import time
from nicegui import ui

# Хранилище ссылок на элементы интерфейса
uid_inputs = {
    'MU': None,
    'IU': None,
    'CU': None,
}

sn_inputs = {
    'MU': None,
    'IU': None,
    'CU': None,
}

hw_inputs = {
    'MU': None,
    'IU': None,
    'CU': None,
}

user_inputs = {
    'MU': None,
    'IU': None,
    'CU': None,
}


def notify_user(msg: str, type: str = 'info'):
    """Безопасный вывод уведомлений (не падает в фоновых задачах)"""
    try:
        ui.notify(msg, type=type)
    except Exception:
        print(f'[{type.upper()}]: {msg}')


async def query_scpi_command(
    curr_shell, cmd_str: str, filter_prefix: str, timeout: float = 0.8
) -> str:
    """Шлёт запрос в Shell и возвращает очищенный ответ прибора"""
    curr_shell.reset_input_buffer()
    cmd = f'{cmd_str}\r\n'.encode()
    curr_shell.write(cmd)
    print(f'[SCPI TX]: {cmd_str}')

    start_t = time.time()
    accum = ''
    while time.time() - start_t < timeout:
        if curr_shell.in_waiting > 0:
            accum += curr_shell.read(curr_shell.in_waiting).decode(
                'utf-8', errors='ignore'
            )
            lines = [l.strip() for l in accum.splitlines() if l.strip()]
            filtered = [l for l in lines if filter_prefix.upper() not in l.upper()]
            if filtered:
                return filtered[-1].replace('"', '').strip()
        await asyncio.sleep(0.03)

    lines = [l.strip() for l in accum.splitlines() if l.strip()]
    filtered = [l for l in lines if filter_prefix.upper() not in l.upper()]
    return filtered[-1].replace('"', '').strip() if filtered else ''


async def read_all_uids_async(shell_ref):
    """Вычитывает последовательно UID, SN, HW и USER для всех узлов (МУ, ИУ, КУ)"""
    curr_shell = shell_ref.get('ser')
    if not curr_shell or not curr_shell.is_open:
        notify_user(
            'SCPI порт закрыт! Сначала подключитесь к стенду.', type='warning'
        )
        return

    # 1. Чтение UID МУ, ИУ, КУ
    uid_mu = await query_scpi_command(curr_shell, 'SYST:UID? MU', 'SYST:UID?')
    if uid_mu and uid_inputs['MU']:
        uid_inputs['MU'].set_value(uid_mu)
    await asyncio.sleep(0.04)

    uid_iu = await query_scpi_command(curr_shell, 'SYST:UID? IU', 'SYST:UID?')
    if uid_iu and uid_inputs['IU']:
        uid_inputs['IU'].set_value(uid_iu)
    await asyncio.sleep(0.04)

    uid_cu = await query_scpi_command(curr_shell, 'SYST:UID? CU', 'SYST:UID?')
    if uid_cu and uid_inputs['CU']:
        uid_inputs['CU'].set_value(uid_cu)
    await asyncio.sleep(0.04)

    # 2. Чтение серийных номеров МУ, ИУ и КУ
    sn_mu = await query_scpi_command(curr_shell, 'SYST:SER? MU', 'SYST:SER?')
    if sn_mu and sn_inputs['MU']:
        sn_inputs['MU'].set_value(sn_mu)
    await asyncio.sleep(0.04)

    sn_iu = await query_scpi_command(curr_shell, 'SYST:SER? IU', 'SYST:SER?')
    if sn_iu and sn_inputs['IU']:
        sn_inputs['IU'].set_value(sn_iu)
    await asyncio.sleep(0.04)

    sn_cu = await query_scpi_command(curr_shell, 'SYST:SER? CU', 'SYST:SER?')
    if sn_cu and sn_inputs['CU']:
        sn_inputs['CU'].set_value(sn_cu)
    await asyncio.sleep(0.04)

    # 3. Чтение аппаратных ревизий HW (ccc.a.mm.dd.ee) МУ, ИУ и КУ
    hw_mu = await query_scpi_command(curr_shell, 'SYST:HARD? MU', 'SYST:HARD?')
    if hw_mu and hw_inputs['MU']:
        hw_inputs['MU'].set_value(hw_mu)
    await asyncio.sleep(0.04)

    hw_iu = await query_scpi_command(curr_shell, 'SYST:HARD? IU', 'SYST:HARD?')
    if hw_iu and hw_inputs['IU']:
        hw_inputs['IU'].set_value(hw_iu)
    await asyncio.sleep(0.04)

    hw_cu = await query_scpi_command(curr_shell, 'SYST:HARD? CU', 'SYST:HARD?')
    if hw_cu and hw_inputs['CU']:
        hw_inputs['CU'].set_value(hw_cu)
    await asyncio.sleep(0.04)

    # 4. Чтение пользовательских данных USER МУ, ИУ и КУ
    user_mu = await query_scpi_command(curr_shell, 'SYST:USER? MU', 'SYST:USER?')
    if user_mu and user_inputs['MU']:
        user_inputs['MU'].set_value(user_mu)
    await asyncio.sleep(0.04)

    user_iu = await query_scpi_command(curr_shell, 'SYST:USER? IU', 'SYST:USER?')
    if user_iu and user_inputs['IU']:
        user_inputs['IU'].set_value(user_iu)
    await asyncio.sleep(0.04)

    user_cu = await query_scpi_command(curr_shell, 'SYST:USER? CU', 'SYST:USER?')
    if user_cu and user_inputs['CU']:
        user_inputs['CU'].set_value(user_cu)

    notify_user('Параметры системы успешно вычитаны', type='positive')


async def write_serial_number(shell_ref, unit: str):
    """Записывает серийный номер в целевой узел: SYST:SER <unit>, <sn>"""
    curr_shell = shell_ref.get('ser')
    if not curr_shell or not curr_shell.is_open:
        notify_user('SCPI порт закрыт!', type='warning')
        return

    inp = sn_inputs.get(unit)
    if not inp:
        return

    sn_val_str = inp.value.strip()
    if not sn_val_str:
        notify_user(f'Введите серийный номер для {unit}!', type='warning')
        return

    try:
        sn_int = int(sn_val_str, 0)
        if not (0 <= sn_int <= 4294967295):
            raise ValueError()
    except ValueError:
        notify_user(
            'Некорректный SN! Допустимо целое 32-бит без знака (0..4294967295)',
            type='negative',
        )
        return

    cmd_str = f'SYST:SER {unit}, {sn_val_str}\r\n'
    curr_shell.reset_input_buffer()
    curr_shell.write(cmd_str.encode('utf-8'))
    print(f'[SCPI TX]: {cmd_str.strip()}')

    notify_user(f'Отправлена запись SN для {unit}: {sn_val_str}', type='info')

    await asyncio.sleep(0.15)
    updated_sn = await query_scpi_command(
        curr_shell, f'SYST:SER? {unit}', 'SYST:SER?'
    )
    if updated_sn:
        inp.set_value(updated_sn)
        notify_user(f'SN {unit} успешно сохранен: {updated_sn}', type='positive')


async def write_hardware_version(shell_ref, unit: str):
    """Записывает аппаратную ревизию в целевой узел: SYST:HARD <unit>, "<hw_str>" """
    curr_shell = shell_ref.get('ser')
    if not curr_shell or not curr_shell.is_open:
        notify_user('SCPI порт закрыт!', type='warning')
        return

    inp = hw_inputs.get(unit)
    if not inp:
        return

    raw_str = inp.value.strip().replace('"', '')
    if not raw_str:
        notify_user(f'Введите HW ревизию для {unit}!', type='warning')
        return

    parts = raw_str.split('.')
    if len(parts) != 5:
        notify_user(
            'Формат HW: ccc.a.mm.dd.ee (строго 5 чисел через точку)!',
            type='negative',
        )
        return

    try:
        ccc = int(parts[0])
        a = int(parts[1])
        mm = int(parts[2])
        dd = int(parts[3])
        ee = int(parts[4])
        if not (
            0 <= ccc <= 999
            and 0 <= a <= 9
            and 0 <= mm <= 99
            and 0 <= dd <= 99
            and 0 <= ee <= 99
        ):
            raise ValueError()
    except ValueError:
        notify_user(
            'Диапазоны: ccc(0..999), a(0..9), mm(0..99), dd(0..99), ee(0..99)!',
            type='negative',
        )
        return

    normalized_hw = f'{ccc:03d}.{a:d}.{mm:02d}.{dd:02d}.{ee:02d}'
    inp.set_value(normalized_hw)

    cmd_str = f'SYST:HARD {unit}, "{normalized_hw}";*OPC?\r\n'
    curr_shell.reset_input_buffer()
    curr_shell.write(cmd_str.encode('utf-8'))
    print(f'[SCPI TX]: {cmd_str.strip()}')

    notify_user(f'Запись HW {unit}: {normalized_hw}...', type='info')

    start_t = time.time()
    accum = ''
    is_ok = False
    while time.time() - start_t < 1.5:
        if curr_shell.in_waiting > 0:
            accum += curr_shell.read(curr_shell.in_waiting).decode(
                'utf-8', errors='ignore'
            )
            lines = [l.strip() for l in accum.splitlines() if l.strip()]
            if '1' in lines:
                is_ok = True
                break
        await asyncio.sleep(0.04)

    if is_ok:
        notify_user(
            f'HW {unit} успешно прошит в EEPROM: {normalized_hw}', type='positive'
        )
    else:
        await asyncio.sleep(0.1)
        actual_hw = await query_scpi_command(
            curr_shell, f'SYST:HARD? {unit}', 'SYST:HARD?'
        )
        if actual_hw:
            inp.set_value(actual_hw)
            notify_user(f'Текущее значение HW {unit}: {actual_hw}', type='warning')


async def write_user_data(shell_ref, unit: str):
    """Записывает пользовательские данные в целевой узел: SYST:USER <unit>, <val>"""
    curr_shell = shell_ref.get('ser')
    if not curr_shell or not curr_shell.is_open:
        notify_user('SCPI порт закрыт!', type='warning')
        return

    inp = user_inputs.get(unit)
    if not inp:
        return

    user_val_str = inp.value.strip()
    if not user_val_str:
        notify_user(f'Введите значение USER для {unit}!', type='warning')
        return

    try:
        user_int = int(user_val_str, 0)
        if not (0 <= user_int <= 4294967295):
            raise ValueError()
    except ValueError:
        notify_user(
            'Некорректное значение USER! Допустимо целое 32-бит (0..4294967295)',
            type='negative',
        )
        return

    cmd_str = f'SYST:USER {unit}, {user_val_str};*OPC?\r\n'
    curr_shell.reset_input_buffer()
    curr_shell.write(cmd_str.encode('utf-8'))
    print(f'[SCPI TX]: {cmd_str.strip()}')

    notify_user(f'Запись USER {unit}: {user_val_str}...', type='info')

    start_t = time.time()
    accum = ''
    is_ok = False
    while time.time() - start_t < 1.5:
        if curr_shell.in_waiting > 0:
            accum += curr_shell.read(curr_shell.in_waiting).decode(
                'utf-8', errors='ignore'
            )
            lines = [l.strip() for l in accum.splitlines() if l.strip()]
            if '1' in lines:
                is_ok = True
                break
        await asyncio.sleep(0.04)

    if is_ok:
        notify_user(
            f'USER {unit} успешно сохранен: {user_val_str}', type='positive'
        )
    else:
        await asyncio.sleep(0.1)
        actual_user = await query_scpi_command(
            curr_shell, f'SYST:USER? {unit}', 'SYST:USER?'
        )
        if actual_user:
            inp.set_value(actual_user)
            notify_user(f'Текущее значение USER {unit}: {actual_user}', type='warning')


def build_system_panel(ports_refs):
    """Строит карточку 'Система': заголовок по центру, узкие поля и кнопка справа по центру"""
    global uid_inputs, sn_inputs, hw_inputs, user_inputs
    shell_ref = ports_refs['shell']

    with ui.card().classes('bg-slate-800 p-2.5 w-full cursor-grab'):
        ui.label('Система (SYSTem)').classes(
            'text-sm font-bold text-yellow-300 text-center w-full mb-2 tracking-wide'
        )

        with ui.row().classes('w-full gap-3 items-center'):

            # --- СЕТКА ИЗ 3 КОЛОНОК (МУ | ИУ | КУ) ---
            with ui.row().classes('flex-1 gap-2 items-start'):

                # --- КОЛОНКА 1: МАТЕРИНСКИЙ УЗЕЛ (МУ) ---
                with ui.card().classes(
                    'bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex'
                    ' flex-col gap-1.5'
                ):
                    ui.label('Материнский узел (MU)').classes(
                        'text-xs font-bold text-sky-300 leading-none mb-0.5'
                    )

                    # UID МУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('UID:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        uid_inputs['MU'] = (
                            ui.input(placeholder='0x...-0x...-0x...')
                            .props(
                                'dark outlined dense readonly input-class="font-mono'
                                ' text-emerald-400 font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[210px]')
                        )

                    # SN МУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('SN:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        sn_inputs['MU'] = (
                            ui.input(placeholder='SN (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-amber-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_serial_number(shell_ref, 'MU'),
                        ).classes(
                            'bg-emerald-600 hover:bg-emerald-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

                    # HW МУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('HW:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        hw_inputs['MU'] = (
                            ui.input(placeholder='001.0.01.01.00')
                            .props(
                                'dark outlined dense input-class="font-mono text-cyan-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_hardware_version(shell_ref, 'MU'),
                        ).classes(
                            'bg-cyan-600 hover:bg-cyan-500 text-white text-[9px] font-bold'
                            ' py-1 px-2 rounded shadow'
                        )

                    # USER МУ (активный ввод + кнопка Set)
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('USER:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        user_inputs['MU'] = (
                            ui.input(placeholder='USER (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-purple-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_user_data(shell_ref, 'MU'),
                        ).classes(
                            'bg-purple-600 hover:bg-purple-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

                # --- КОЛОНКА 2: ИНТЕРФЕЙСНЫЙ УЗЕЛ (ИУ) ---
                with ui.card().classes(
                    'bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex'
                    ' flex-col gap-1.5'
                ):
                    ui.label('Интерфейсный узел (IU)').classes(
                        'text-xs font-bold text-sky-300 leading-none mb-0.5'
                    )

                    # UID ИУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('UID:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        uid_inputs['IU'] = (
                            ui.input(placeholder='0x...')
                            .props(
                                'dark outlined dense readonly input-class="font-mono'
                                ' text-emerald-400 font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[210px]')
                        )

                    # SN ИУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('SN:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        sn_inputs['IU'] = (
                            ui.input(placeholder='SN (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-amber-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_serial_number(shell_ref, 'IU'),
                        ).classes(
                            'bg-emerald-600 hover:bg-emerald-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

                    # HW ИУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('HW:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        hw_inputs['IU'] = (
                            ui.input(placeholder='001.0.01.01.00')
                            .props(
                                'dark outlined dense input-class="font-mono text-cyan-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_hardware_version(shell_ref, 'IU'),
                        ).classes(
                            'bg-cyan-600 hover:bg-cyan-500 text-white text-[9px] font-bold'
                            ' py-1 px-2 rounded shadow'
                        )

                    # USER ИУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('USER:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        user_inputs['IU'] = (
                            ui.input(placeholder='USER (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-purple-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_user_data(shell_ref, 'IU'),
                        ).classes(
                            'bg-purple-600 hover:bg-purple-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

                # --- КОЛОНКА 3: КОММУТАЦИОННЫЙ УЗЕЛ (КУ) ---
                with ui.card().classes(
                    'bg-slate-700/50 p-2 flex-1 rounded border border-slate-600 flex'
                    ' flex-col gap-1.5'
                ):
                    ui.label('Коммутационный узел (CU)').classes(
                        'text-xs font-bold text-sky-300 leading-none mb-0.5'
                    )

                    # UID КУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('UID:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        uid_inputs['CU'] = (
                            ui.input(placeholder='0x...')
                            .props(
                                'dark outlined dense readonly input-class="font-mono'
                                ' text-emerald-400 font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[210px]')
                        )

                    # SN КУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('SN:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        sn_inputs['CU'] = (
                            ui.input(placeholder='SN (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-amber-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_serial_number(shell_ref, 'CU'),
                        ).classes(
                            'bg-emerald-600 hover:bg-emerald-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

                    # HW КУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('HW:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        hw_inputs['CU'] = (
                            ui.input(placeholder='001.0.01.01.00')
                            .props(
                                'dark outlined dense input-class="font-mono text-cyan-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_hardware_version(shell_ref, 'CU'),
                        ).classes(
                            'bg-cyan-600 hover:bg-cyan-500 text-white text-[9px] font-bold'
                            ' py-1 px-2 rounded shadow'
                        )

                    # USER КУ
                    with ui.row().classes('w-full items-center gap-1'):
                        ui.label('USER:').classes(
                            'text-[10px] text-slate-300 font-semibold w-9'
                        )
                        user_inputs['CU'] = (
                            ui.input(placeholder='USER (uint32)')
                            .props(
                                'dark outlined dense input-class="font-mono text-purple-300'
                                ' font-bold text-[10px]"'
                            )
                            .classes('flex-1 bg-slate-900 rounded max-w-[155px]')
                        )

                        ui.button(
                            'Set',
                            on_click=lambda: write_user_data(shell_ref, 'CU'),
                        ).classes(
                            'bg-purple-600 hover:bg-purple-500 text-white text-[9px]'
                            ' font-bold py-1 px-2 rounded shadow'
                        )

            # --- ПРАВАЯ ЧАСТЬ: КНОПКА ПО ЦЕНТРУ СТРОКИ ---
            with ui.column().classes(
                'justify-center items-center w-36 self-center px-1'
            ):
                ui.button(
                    'Прочитать все данные',
                    on_click=lambda: read_all_uids_async(shell_ref),
                ).classes(
                    'w-full bg-sky-600 hover:bg-sky-500 text-white font-bold text-xs'
                    ' py-2.5 px-3 rounded shadow text-center leading-tight'
                )