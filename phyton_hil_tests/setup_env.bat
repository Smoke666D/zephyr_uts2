@echo off
cd /d %~dp0

echo [1/4] Creating virtual environment (venv)...
python -m venv venv

echo [2/4] Activating virtual environment...
call venv\Scripts\activate.bat

echo [3/4] Upgrading pip...
python -m pip install --upgrade pip

echo [4/4] Installing requirements...
pip install -r requirements.txt

echo.
echo === DONE! Environment is ready. ===
pause