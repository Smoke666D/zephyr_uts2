import time
import allure
import pytest

@allure.feature("Sensors Tests")
@allure.story("Accelerometer Validation via RTT")
class TestAccelerometer:

    @allure.step("Запрос и проверка показаний акселерометра через RTT")
    def test_accelerometer_axes_rtt(self, target):
        time.sleep(0.1)
        
        with allure.step("1. Очистка буфера и отправка команды 'SENS:ACC?' на плату"):
            # Очищаем старый мусор из RTT-буфера
            target.read_output()
            
            # Шлем команду в прошивку DUT
            target.send_cmd("SENS:ACC?")
            
            # Даем прошивке время обработать команду и выдать ответ в RTT
            time.sleep(0.2)

        with allure.step("2. Чтение ответа из RTT-буфера"):
            response = target.read_output()
            print(f"RTT Accelerometer response: '{response}'")
            allure.attach(response, name="RTT Raw Response", attachment_type=allure.attachment_type.TEXT)

            # Извлекаем из лога нужную строку (если плата выводит что-то лишнее, 
            # ищем строку, содержащую запятые и цифры)
            response_line = ""
            for line in response.splitlines():
                if "," in line and any(c.isdigit() for c in line):
                    # Отсекаем возможное эхо самой команды, если плата его печатает
                    if "SENS:ACC?" not in line:
                        response_line = line.strip()
                        break
            
            if not response_line:
                # Если в цикле не нашли, попробуем взять последний непустой кусок
                lines = [l.strip() for l in response.splitlines() if l.strip() and "SENS:ACC?" not in l]
                if lines:
                    response_line = lines[-1]

            assert response_line, f"Не удалось получить ответ от платы по RTT. Полный лог:\n{response}"
            allure.attach(response_line, name="Extracted Data Line", attachment_type=allure.attachment_type.TEXT)

        with allure.step("3. Парсинг значений X, Y, Z"):
            try:
                # Ожидаем строку формата "-0.229824,-0.268128,-9.88243"
                parts = [float(val.strip()) for val in response_line.split(",")]
                assert len(parts) == 3, f"Ожидалось 3 компоненты (X, Y, Z), получено: {len(parts)}"
                x, y, z = parts
            except ValueError as e:
                pytest.fail(f"Не удалось распарсить строку '{response_line}': {e}")

            parsed_info = f"X = {x} m/s^2\nY = {y} m/s^2\nZ = {z} m/s^2"
            allure.attach(parsed_info, name="Parsed Axes", attachment_type=allure.attachment_type.TEXT)

        with allure.step("4. Проверка допусков осей (X, Y < 1.0, Z ~ -9.8)"):
            assert abs(x) < 1.0, f"Ускорение по оси X вышло за пределы: {x}"
            assert abs(y) < 1.0, f"Ускорение по оси Y вышло за пределы: {y}"
            assert -10.5 < z < -8.5, f"Ускорение по оси Z некорректно для перевернутой платы: {z}"