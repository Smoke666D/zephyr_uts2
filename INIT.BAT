@echo off
chcp 65001 >nul
echo ==========================================
echo   Установка библиотек глобально...
echo ==========================================

python -m pip install --upgrade pip
pip install nicegui pyserial

echo ==========================================
echo   Запуск дашборда...
echo ==========================================

python test_dash.py
pause