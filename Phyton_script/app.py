import asyncio
import os
import sys
from nicegui import app, ui
from telemetry import telemetry_reader_loop
from ui_components import build_dashboard

# Словари для хранения открытых дескрипторов портов
ports_refs = {'tel': {'ser': None}, 'shell': {'ser': None}}
ui_labels = {}

# Настройка стилей страницы
ui.query('body').classes('bg-slate-900 text-white p-2')

# --- ЗАЩИТА ОТ ДУБЛИРУЮЩИХСЯ ВКЛАДОК (ЗАКРЫТИЕ СТАРЫХ СЕССИЙ) ---
ui.add_head_html("""
<script>
    // Создаем межвкладочный канал связи
    const bc = new BroadcastChannel('uts2_dashboard_single_instance');

    // 1. При открытии этой новой вкладки приказываем всем старым закрыться
    bc.postMessage({ action: 'close_old_instance', time: Date.now() });

    // 2. Слушаем команды от других вкладок
    bc.onmessage = (event) => {
        if (event.data && event.data.action === 'close_old_instance') {
            console.log('[SYSTEM]: Обнаружена новая вкладка дашборда. Закрываем эту...');
            // Пытаемся закрыть вкладку скриптом
            window.open('', '_self', '');
            window.close();

            // Если браузер запретил закрытие вкладки скриптом — блокируем экран и гасим сокет
            document.body.innerHTML = `
                <div style="display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;background:#0f172a;color:#f87171;font-family:sans-serif;text-align:center;">
                    <h2 style="font-size:24px;margin-bottom:8px;">⚠️ Сессия перехвачена</h2>
                    <p style="color:#94a3b8;font-size:14px;">Дашборд запущен в новом окне или вкладке.<br>Эта вкладка отключена во избежание конфликтов COM-портов.</p>
                </div>
            `;
            if (window.socket) window.socket.close();
        }
    };
</script>
""")


def close_all_ports():
    """Безопасно закрывает все открытые COM-порты"""
    for key, ref in ports_refs.items():
        s = ref.get('ser')
        if s and s.is_open:
            try:
                s.close()
                ref['ser'] = None
                print(f'[SYSTEM]: COM-порт {key} успешно закрыт.')
            except Exception as e:
                print(f'[SYSTEM ERROR]: Ошибка закрытия порта {key}: {e}')


async def shutdown_application():
    """Процедура корректного завершения программы по кнопке ВЫХОД"""
    ui.notify('Завершение работы приложения...', type='warning')

    # 1. Закрываем все аппаратные COM-порты
    close_all_ports()

    # 2. Закрываем вкладку в браузере
    try:
        await ui.run_javascript(
            'window.open("", "_self", ""); window.close();', timeout=0.3
        )
    except Exception:
        pass

    await asyncio.sleep(0.2)

    # 3. Завершаем сервер NiceGUI и процесс Python
    try:
        app.shutdown()
    finally:
        os._exit(0)


# Шапка дашборда с кнопкой "ВЫХОД"
with ui.row().classes('w-full justify-between items-center mb-2 px-1'):
    ui.label('UTS2 Embedded Dashboard').classes(
        'text-xl font-bold text-sky-400 tracking-wide'
    )

    # Кнопка Выход
    ui.button('ВЫХОД', on_click=shutdown_application).classes(
        'bg-red-700 hover:bg-red-600 text-white font-bold text-xs py-1.5 px-4 rounded shadow'
    )

# Автоматическое закрытие портов при системном прерывании (Ctrl+C в консоли)
app.on_shutdown(close_all_ports)

# Автозавершение Python-процесса, если вкладка браузера закрыта крестиком
disconnect_timer = None


def on_client_disconnect(*args, **kwargs):
    global disconnect_timer

    async def delayed_shutdown():
        # Даем 3 секунды форы (на случай, если пользователь просто нажал F5)
        await asyncio.sleep(3.0)
        print('[SYSTEM]: Вкладка браузера закрыта. Завершение работы...')
        close_all_ports()
        try:
            app.shutdown()
        finally:
            os._exit(0)

    disconnect_timer = asyncio.create_task(delayed_shutdown())


app.on_disconnect(on_client_disconnect)

# Построение интерфейса
can_logs, lin_logs, raw_logs = build_dashboard(ports_refs, ui_labels)

# Фоновый цикл чтения телеметрии
ui.timer(
    0.1,
    lambda: telemetry_reader_loop(
        ports_refs['tel'], ui_labels, can_logs, lin_logs, raw_logs
    ),
    once=True,
)

# Запуск веб-сервера NiceGUI
ui.run(port=8080, title='UTS2 Dashboard', favicon=None, reload=False)