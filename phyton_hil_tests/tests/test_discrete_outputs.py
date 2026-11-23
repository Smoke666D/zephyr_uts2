import time
import allure
import pytest

@allure.feature("Discrete Outputs & Feedback Tests")
@allure.story("Full Verification of OD Outputs and ADC Feedbacks (Fully Batched)")
class TestDiscreteOutputs:

    @pytest.fixture(autouse=True)
    def setup_teardown_initial_state(self, bench, target):
        """Сброс состояния стенда и DUT в исходное (пакетно)"""
        # Сброс выходов стенда в LOW одним пакетом
        bench.send("OUTP:DOUT 1, LOW; DOUT 2, LOW; DOUT 3, LOW; DOUT 4, LOW; DOUT 5, LOW")
        
        # Сброс всех OD выходов DUT в OFF одним пакетом по RTT
        target.send_cmd("OUTPut:OD OD1, OFF; OD2, OFF; OD3, OFF; OD4, OFF; OD5, OFF")
        
        time.sleep(0.1)
        yield
        
        # Teardown
        bench.send("OUTP:DOUT 1, LOW; DOUT 2, LOW; DOUT 3, LOW; DOUT 4, LOW; DOUT 5, LOW")
        target.send_cmd("OUTPut:OD OD1, OFF; OD2, OFF; OD3, OFF; OD4, OFF; OD5, OFF")

    @allure.step("Шаг 1. Проверка исходного состояния (все LOW, все OFF)")
    def test_step1_baseline(self, bench, target):
        with allure.step("1.1 Пакетная установка всех выходов стенда в LOW, выключение DUT OD"):
            bench.send("OUTP:DOUT 1, LOW; DOUT 2, LOW; DOUT 3, LOW; DOUT 4, LOW; DOUT 5, LOW")
            target.send_cmd("OUTPut:OD OD1, OFF; OD2, OFF; OD3, OFF; OD4, OFF; OD5, OFF")
            time.sleep(0.05)

        with allure.step("1.2 Проверка общего тока утечки (SENS:CURR? BRD_HIGH < 1mA)"):
            curr_str = bench.query("SENS:CURR? BRD_HIGH")
            allure.attach(curr_str, name="Raw BRD_HIGH Current", attachment_type=allure.attachment_type.TEXT)
            current_ma = float(curr_str) * 1000.0
            assert current_ma < 1.0, f"Ток утечки слишком высокий: {current_ma:.3f} мА"

        with allure.step("1.3 Проверка фонового напряжения на входах AIN_AO (все < 1В)"):
            volt_str = bench.query("SENSe:VOLTage? AIN_AO1, AIN_AO2, AIN_AO3, AIN_AO4, AIN_AO5")
            allure.attach(volt_str, name="Raw AIN_AO Voltages", attachment_type=allure.attachment_type.TEXT)
            
            voltages = [float(v.strip()) for v in volt_str.split(",")]
            for idx, volt in enumerate(voltages, start=1):
                assert volt < 1.0, f"Канал AIN_AO{idx} имеет высокий фон: {volt} В"

    @allure.step("Шаг 2. Бегущая единица со стенда (проверка обратной связи AIN)")
    def test_step2_running_one_from_bench(self, bench):
        for active_channel in range(1, 6):
            with allure.step(f"2.1 Пакетная установка HIGH для канала {active_channel}, остальные LOW"):
                cmd_parts = []
                for i in range(1, 6):
                    state = "HIGH" if i == active_channel else "LOW"
                    cmd_parts.append(f"DOUT {i}, {state}")
                
                packet_cmd = "OUTP:" + "; ".join(cmd_parts)
                bench.send(packet_cmd)
                time.sleep(0.02)

            with allure.step(f"2.2 Проверка токов и напряжений для активного канала {active_channel}"):
                curr_str = bench.query("SENS:CURR? BRD_HIGH")
                current_ma = float(curr_str) * 1000.0
                assert current_ma < 2.0, f"Ток на шаге канала {active_channel} превысил 2мА: {current_ma:.3f}мА"

                volt_str = bench.query("SENSe:VOLTage? AIN_AO1, AIN_AO2, AIN_AO3, AIN_AO4, AIN_AO5")
                voltages = [float(v.strip()) for v in volt_str.split(",")]

                for idx, volt in enumerate(voltages, start=1):
                    if idx == active_channel:
                        assert volt > 11.0, f"На канале AIN_AO{idx} нет 11В (текущее: {volt}В)"
                    else:
                        assert volt < 2.0, f"На пассивном канале AIN_AO{idx} обнаружена наводка: {volt}В"

    @allure.step("Шаг 3. Проверка коммутации Open Drain со стороны DUT (поштучно через RTT)")
    def test_step3_dut_od_switching(self, bench, target):
        for ch in range(1, 6):
            with allure.step(f"3.1 Пакетная настройка стенда: линия {ch} в HIGH, остальные в LOW"):
                cmd_parts = []
                for i in range(1, 6):
                    state = "HIGH" if i == ch else "LOW"
                    cmd_parts.append(f"DOUT {i}, {state}")
                
                packet_cmd = "OUTP:" + "; ".join(cmd_parts)
                bench.send(packet_cmd)
                time.sleep(0.02)
                
                v_before = float(bench.query(f"SENSe:VOLTage? AIN_AO{ch}"))
                assert v_before > 11.0, f"Исходное напряжение на AIN_AO{ch} до включения DUT низкое: {v_before}В"

            with allure.step(f"3.2 Включение выхода DUT по RTT: OUTPut:OD OD{ch}, ON"):
                # Возвращаем точечную отправку конкретного канала на DUT, как было до этого
                target.send_cmd(f"OUTPut:OD OD{ch}, ON")
                time.sleep(0.05)

            with allure.step(f"3.3 Проверка просадки напряжения (< 1В) и роста тока (11-20 мА) для канала {ch}"):
                v_after = float(bench.query(f"SENSe:VOLTage? AIN_AO{ch}"))
                assert v_after < 1.0, f"Напряжение на AIN_AO{ch} не просело при включении OD: {v_after}В"

                curr_str = bench.query(f"SENS:CURR? BRD_HIGH")
                current_ma = float(curr_str) * 1000.0
                assert 11.0 <= current_ma <= 20.0, f"Ток нагрузки для канала {ch} вне диапазона 11-20 мА: {current_ma:.3f} мА"

            with allure.step(f"3.4 Выключение выхода DUT по RTT: OUTPut:OD OD{ch}, OFF"):
                target.send_cmd(f"OUTPut:OD OD{ch}, OFF")
                time.sleep(0.02)

                v_restored = float(bench.query(f"SENSe:VOLTage? AIN_AO{ch}"))
                assert v_restored > 11.0, f"Напряжение на AIN_AO{ch} не восстановилось после выключения OD: {v_restored}В"