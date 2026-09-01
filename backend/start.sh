#!/bin/bash
# AroNet Backend Startup Script for Linux/macOS

echo "==============================================="
echo "AroNet Manufacturing Inventory System"
echo "==============================================="

# Check if Python is installed
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python 3 is not installed"
    echo "Please install Python 3.8+ first"
    exit 1
fi

# Create virtual environment if needed
if [ ! -d "venv" ]; then
    echo "Creating virtual environment..."
    python3 -m venv venv
fi

# Activate virtual environment
source venv/bin/activate

# Install requirements if needed
if ! pip show Flask &> /dev/null; then
    echo "Installing dependencies..."
    pip install -r requirements.txt
fi

# Initialize database if needed
if [ ! -f "inventory.db" ]; then
    echo "Initializing database..."
    python -c "from database import init_db, seed_demo_data; init_db(); seed_demo_data()"
fi

# Start Flask app
echo ""
echo "==============================================="
echo "Starting Flask server..."
echo "Open browser: http://localhost:5000"
echo "Press Ctrl+C to stop"
echo "==============================================="
echo ""

python app.py
