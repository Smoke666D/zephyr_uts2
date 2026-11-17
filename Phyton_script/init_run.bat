@echo off
chcp 65001 >nul

echo ==========================================
echo   [1/4] Проверка наличия Python...
echo ==========================================

python --version >nul 2>&1
if errorlevel 1 goto NO_PYTHON

echo Python успешно обнаружен:
python --version

echo.
echo ==========================================
echo   [2/4] Очистка кэша Python (__pycache__)...
echo ==========================================

for /d /r . %%d in (__pycache__) do (
    if exist "%%d" rd /s /q "%%d"
)
del /s /q /f *.pyc >nul 2>&1
del /s /q /f *.pyo >nul 2>&1
echo Кэш успешно очищен!

echo.
echo ==========================================
echo   [3/4] Установка всех необходимых библиотек...
echo ==========================================

REM Обновление базовых инструментов упаковки
python -m pip install --upgrade pip setuptools wheel

REM Установка полного стека библиотек для проекта
python -m pip install nicegui pyserial matplotlib pandas openpyxl

echo.
echo ==========================================
echo   [4/4] Запуск дашборда UTS2...
echo ==========================================

python app.py

echo.
echo Приложение завершило работу.
pause
exit /b 0

:NO_PYTHON
echo.
echo =======================================================================
echo [ОШИБКА] Python не найден в системе или не добавлен в переменную PATH!
echo.
echo 1. Установите Python с официального сайта: https://www.python.org/
echo 2. При установке ОБЯЗАТЕЛЬНО поставьте галочку:
echo    "Add python.exe to PATH" (внизу первого окна установщика).
echo =======================================================================
echo.
pause
exit /b 1