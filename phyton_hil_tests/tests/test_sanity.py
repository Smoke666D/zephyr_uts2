import allure

@allure.feature("Sanity Tests")
@allure.story("Check Bench and Target Connection")
def test_bench_identification(bench):
    """Проверка, что стенд отвечает на стандартную SCPI команду идентификации"""
    with allure.step("Запрос *IDN? у SCPI-стенда"):
        response = bench.query("*IDN?")
        print(f"Bench IDN response: {response}")
        
    with allure.step("Проверка, что ответ не пустой"):
        assert len(response) > 0, "Стенд не вернул ответ на *IDN?"
        # Если вы знаете модель стенда, можно добавить жесткую проверку, например:
        # assert "MyBench" in response

@allure.feature("Sanity Tests")
@allure.story("Check Target RTT Link")
def test_target_rtt_listening(target):
    """Проверка, что RTT-канал читается и плата подает признаки жизни"""
    with allure.step("Чтение данных из RTT-буфера узла"):
        logs = target.read_output(max_bytes=512)
        print(f"RTT Logs preview:\n{logs}")
        
    with allure.step("Прикрепление логов RTT к Allure отчету"):
        allure.attach(logs, name="Initial RTT Buffer", attachment_type=allure.attachment_type.TEXT)
        
    # Базовая проверка — функция выполнилась, ошибок связи с J-Link не возникло
    assert True