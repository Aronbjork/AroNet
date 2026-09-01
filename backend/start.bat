@echo off
REM AroNet Backend Startup Script

echo ===============================================
echo AroNet Manufacturing Inventory System
echo ===============================================

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python is not installed or not in PATH
    echo Please install Python 3.8+ from python.org
    pause
    exit /b 1
)

REM Check if virtual environment exists
if not exist "venv" (
    echo Creating virtual environment...
    python -m venv venv
)

REM Activate virtual environment
call venv\Scripts\activate.bat

REM Install requirements if not already installed
pip show Flask >nul 2>&1
if errorlevel 1 (
    echo Installing dependencies...
    pip install -r requirements.txt
)

REM Initialize database if needed
if not exist "inventory.db" (
    echo Initializing database...
    python -c "from database import init_db, seed_demo_data; init_db(); seed_demo_data()"
)

REM Start the Flask app
echo.
echo ===============================================
echo Starting Flask server...
echo Open browser: http://localhost:5000
echo Press Ctrl+C to stop
echo ===============================================
echo.

python app.py
