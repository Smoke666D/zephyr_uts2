@echo off
chcp 65001 >nul
echo ==========================================
echo   Очистка кэша Python (__pycache__)...
echo ==========================================

REM Удаление всех папок __pycache__ в текущей папке и подпапках
for /d /r . %%d in (__pycache__) do (
    if exist "%%d" (
        echo Удаление "%%d"...
        rd /s /q "%%d"
    )
)

REM Удаление всех файлов .pyc и .pyo
del /s /q /f *.pyc >nul 2>&1
del /s /q /f *.pyo >nul 2>&1

echo Кэш успешно очищен!

echo ==========================================
echo   Установка и обновление библиотек...
echo ==========================================

python -m pip install --upgrade pip
python -m pip install nicegui pyserial matplotlib pandas

echo ==========================================
echo   Запуск дашборда UTS2...
echo ==========================================

python app.py
pause