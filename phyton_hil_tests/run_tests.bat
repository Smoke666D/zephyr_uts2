@echo off
chcp 65001 > nul
cd /d %~dp0

:: Настройки по умолчанию
set DEFAULT_PORT=COM11
set DEFAULT_MCU=STM32F407VG

echo === Активация виртуального окружения ===
if not exist "venv" (
    echo [ERROR] Виртуальное окружение не найдено! Сначала запустите setup_env.bat
    goto error
)
call venv\Scripts\activate.bat

echo.
echo === Очистка старых результатов Allure ===
if exist "allure-results" rmdir /s /q allure-results

echo.
echo === Запуск HIL тестов через python -m pytest ===
echo Порт стенда: %DEFAULT_PORT%
echo MCU: %DEFAULT_MCU%
echo.

:: Запуск pytest с автоматическим созданием allure-results и очисткой
python -m pytest -x --scpi-port=%DEFAULT_PORT% --mcu-device=%DEFAULT_MCU% --alluredir=allure-results --clean-alluredir %*

echo.
echo ========================================================
echo Тесты завершены!
echo Для просмотра интерактивного отчета выполните в консоли:
echo   allure serve allure-results
echo ========================================================
echo.
pause
exit /b

:error
echo.
echo [FAIL] Не удалось запустить тесты.
pause
exit /b