import time
import allure
import pytest

def read_dut_din(target) -> list[int]:
    """Вспомогательная функция отправки INPut:DIN? по RTT и парсинга ответа [0, 0, 0, 0]"""
    target.read_output()  # очистка RTT-буфера перед запросом
    target.send_cmd("INPut:DIN?")
    time.sleep(0.15)  # даем время прошивке обработать опрос
    
    raw = target.read_output()
    allure.attach(raw, name="Raw RTT DIN Readout", attachment_type=allure.attachment_type.TEXT)

    # Ищем строку с 4 значениями через запятую
    for line in raw.splitlines():
        clean = line.strip()
        if "INPut:DIN?" in clean:
            continue
        if "," in clean:
            try:
                vals = [int(v.strip()) for v in clean.split(",")]
                if len(vals) == 4:
                    return vals
            except ValueError:
                pass
                
    pytest.fail(f"Не удалось распарсить статус DIN от DUT. Полный лог RTT:\n'{raw}'")


@allure.feature("Discrete Inputs Tests")
@allure.story("DIN Verification & Pull-Up Control")
class TestDiscreteInputs:

    @pytest.fixture(autouse=True)
    def setup_teardown_din(self, bench, target):
        """Исходный сетап: выходы стенда в IN, подтяжки DUT выключены"""
        bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, IN; DOUT 15, IN")
        target.send_cmd("OUTPut:OD PU_ON3, OFF; OD PU_ON4, OFF")
        time.sleep(0.02)
        yield
        # Teardown
        bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, IN; DOUT 15, IN")
        target.send_cmd("OUTPut:OD PU_ON3, OFF; OD PU_ON4, OFF")

    @allure.step("Шаг 0. Базовая проверка сетапа (все входы 0,0,0,0)")
    def test_step0_initial_state(self, bench, target):
        with allure.step("Проверка состояния INPut:DIN? при отключенных подтяжках"):
            din_states = read_dut_din(target)
            assert din_states == [0, 0, 0, 0], f"Исходное состояние входов не нулевое: {din_states}"

    @allure.step("Этап 1. Бегущая единица со стенда (DOUT 12..15 -> DIN 1..4)")
    def test_step1_running_one_from_bench(self, bench, target):
        # Каналы стенда и ожидаемые состояния [DIN1, DIN2, DIN3, DIN4]
        test_matrix = [
            (12, [1, 0, 0, 0]),
            (13, [0, 1, 0, 0]),
            (14, [0, 0, 1, 0]),
            (15, [0, 0, 0, 1]),
        ]

        for active_ch, expected_din in test_matrix:
            with allure.step(f"1.1 Подача HIGH на DOUT {active_ch} стенда, остальные IN"):
                cmd_parts = []
                for ch in [12, 13, 14, 15]:
                    state = "HIGH" if ch == active_ch else "IN"
                    cmd_parts.append(f"DOUT {ch}, {state}")
                
                packet = "OUTP:" + "; ".join(cmd_parts)
                bench.send(packet)
                time.sleep(0.05)

            with allure.step(f"1.2 Чтение DIN на DUT, ожидается {expected_din}"):
                actual_din = read_dut_din(target)
                assert actual_din == expected_din, f"Ошибка для DOUT {active_ch}! Ожидалось {expected_din}, получено {actual_din}"

    @allure.step("Этап 2. Проверка внутренних подтяжек PU_ON3 и PU_ON4 на DUT")
    def test_step2_pullup_and_bench_override(self, bench, target):
        # Обязательно возвращаем стенд в IN перед стартом этапа 2
        bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, IN; DOUT 15, IN")
        time.sleep(0.02)

        # --- Часть А: Подтяжка PU_ON3 (DIN3) ---
        with allure.step("2.1 Включение подтяжки PU_ON3 на DUT (PU_ON3=ON, PU_ON4=OFF)"):
            target.send_cmd("OUTPut:OD PU_ON3, ON; OD PU_ON4, OFF")
            time.sleep(0.05)
            
            actual_din = read_dut_din(target)
            assert actual_din == [0, 0, 1, 0], f"Подтяжка PU_ON3 не подняла DIN3! Ожидалось [0,0,1,0], получено {actual_din}"

        with allure.step("2.2 Пережатие DIN3 стендом в LOW (DOUT 14=LOW)"):
            bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, LOW; DOUT 15, IN")
            time.sleep(0.05)
            
            actual_din = read_dut_din(target)
            assert actual_din == [0, 0, 0, 0], f"Стенд не смогла притянуть DIN3 к Земле! Ожидалось [0,0,0,0], получено {actual_din}"

        # --- Часть Б: Подтяжка PU_ON4 (DIN4) ---
        with allure.step("2.3 Переключение подтяжки на PU_ON4 (PU_ON3=OFF, PU_ON4=ON)"):
            # Сначала сбрасываем стенд в IN
            bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, IN; DOUT 15, IN")
            target.send_cmd("OUTPut:OD PU_ON3, OFF; OD PU_ON4, ON")
            time.sleep(0.09)
            
            actual_din = read_dut_din(target)
            assert actual_din == [0, 0, 0, 1], f"Подтяжка PU_ON4 не подняла DIN4! Ожидалось [0,0,0,1], получено {actual_din}"

        with allure.step("2.4 Пережатие DIN4 стендом в LOW (DOUT 15=LOW)"):
            bench.send("OUTP:DOUT 12, IN; DOUT 13, IN; DOUT 14, IN; DOUT 15, LOW")
            time.sleep(0.05)
            
            actual_din = read_dut_din(target)
            assert actual_din == [0, 0, 0, 0], f"Стенд не смог притянуть DIN4 к Земле! Ожидалось [0,0,0,0], получено {actual_din}"