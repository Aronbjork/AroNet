# AroNet Quick Setup Guide

## Today's MVP: Get Backend Running + Test API

### Step 1: Install Python (if not already installed)
Download from https://www.python.org/downloads/ (Python 3.8+)

### Step 2: Start Backend Server
```powershell
cd d:\Programmering\AroNet\backend
.\start.bat
```

Expected output:
```
===============================================
AroNet Manufacturing Inventory System
===============================================
Creating virtual environment...
Installing dependencies...
Initializing database...
✓ Demo data seeded successfully

===============================================
Starting Flask server...
Open browser: http://localhost:5000
Press Ctrl+C to stop
===============================================

 * Serving Flask app 'app'
 * Debug mode: on
 * Running on http://0.0.0.0:5000
```

### Step 3: Open Web Dashboard
Visit **http://localhost:5000** in your browser

You should see:
- Dashboard tab with 6 stat cards (parts, inventory, jobs, devices, etc.)
- Parts, Operations, Products, and Jobs tabs
- Pre-seeded demo data (CS20 product, 4 parts, 6 operations)

### Step 4: Test Create a Production Run
1. Go to **Jobs** tab
2. Select product "CS20"
3. Set quantity to "2" (to build 2 units)
4. Click "Create Jobs"
5. See 12 jobs created (2 units × 6 operations each)
6. Check the jobs table below

### Step 5: Test API with cURL or Postman

```bash
# Get all parts
curl http://localhost:5000/api/parts

# Get all products
curl http://localhost:5000/api/products

# Get jobs for a device
curl http://localhost:5000/api/devices/DISPLAY-01/status

# Start a job
curl -X PUT http://localhost:5000/api/jobs/1/start \
  -H "Content-Type: application/json"

# Complete a job (decrement inventory)
curl -X PUT http://localhost:5000/api/jobs/1/complete \
  -H "Content-Type: application/json" \
  -d '{"device_id":"DISPLAY-01"}'
```

## Next: Integrate with ESP32

Once backend is running smoothly:

### 1. Review Device Client API
- Open `main/aronet_device_client.h`
- All functions you'll call from ESP32

### 2. Review GUI Example
- Open `main/aronet_device_gui.c`
- Shows how to build LVGL screens
- Button handlers for job start/complete
- State machine for device workflow

### 3. Integrate into main.c
Add to your existing main.c:
```c
#include "aronet_device_client.h"

void app_main(void) {
    // ...existing init...
    
    // Initialize AroNet
    aronet_device_setup("DISPLAY-01", "192.168.1.100");  // Change IP to your backend
    
    // Main loop
    while (1) {
        aronet_device_update();  // Call frequently
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

### 4. Implement Device Client (C)
You'll need to implement `aronet_device_client.h`:
- HTTP requests using `esp_http_client`
- JSON parsing using `cJSON`
- WiFi connection handling
- Device status reporting loop

## Database File Location

SQLite database is created at: `backend/inventory.db`

To reset and start fresh:
```powershell
rm .\backend\inventory.db
.\backend\start.bat
```

## Deployment to RPi 3A+

When ready to deploy to RPi:

1. SSH into RPi:
   ```bash
   ssh pi@raspberrypi.local
   ```

2. Install Python & dependencies:
   ```bash
   sudo apt install python3 python3-pip
   sudo pip3 install -r requirements.txt
   ```

3. Copy backend folder to RPi:
   ```bash
   scp -r backend/ pi@raspberrypi.local:~/AroNet/
   ```

4. Run on RPi:
   ```bash
   ssh pi@raspberrypi.local
   cd ~/AroNet/backend
   python3 app.py
   ```

5. Update ESP32 to use RPi IP (instead of localhost):
   ```c
   aronet_device_setup("DISPLAY-01", "192.168.1.X");  // RPi IP
   ```

## Start AroNet Automatically on Raspberry Pi

The included systemd service starts AroNet after the Pi has network access and restarts it if it exits.

1. Update the checkout and install the production server:
   ```bash
   cd ~/AroNet
   git pull
   cd backend
   source venv/bin/activate
   pip install -r requirements.txt
   deactivate
   ```

2. Install and enable the service:
   ```bash
   sudo cp ~/AroNet/deploy/aronet.service /etc/systemd/system/aronet.service
   sudo systemctl daemon-reload
   sudo systemctl enable --now aronet.service
   ```

3. Verify it is running:
   ```bash
   sudo systemctl status aronet.service
   curl http://127.0.0.1:5000/api/dashboard/stats
   ```

Useful commands:

```bash
sudo systemctl restart aronet.service
sudo systemctl stop aronet.service
sudo journalctl -u aronet.service -f
```

## Shipping a Change to the Raspberry Pi

The Pi runs the same Git checkout that lives on the Windows machine, pulled from
https://github.com/Aronbjork/AroNet. Backend changes are plain Python and
templates, so there is nothing to compile — the Pi only needs the new files and a
restart. `git pull` never touches `backend/inventory.db`, so your live stock and
job history survive every deploy.

### 1. Push from Windows

```powershell
cd d:\Programmering\AroNet
git status                       # see what changed
git add backend SETUP.md         # stage the files you actually changed
git commit -m "Describe the change"
git push
```

### 2. Pull on the Pi

```bash
ssh pi@raspberrypi.local
cd ~/AroNet
git pull
```

### 3. Restart the server

If AroNet runs as the systemd service:

```bash
sudo systemctl restart aronet.service
sudo systemctl status aronet.service        # should say active (running)
```

If you start it by hand instead, stop the old process with `Ctrl+C` in its SSH
window and start it again:

```bash
cd ~/AroNet/backend
python3 app.py
```

A hand-started server dies when that SSH session closes. Use
`sudo systemctl restart aronet.service`, or start it inside `tmux`, if you want
it to keep running after you log out.

Only rerun `pip install -r backend/requirements.txt` when `requirements.txt`
itself changed in the pull.

### 4. Check the change landed

```bash
curl http://127.0.0.1:5000/api/dashboard/stats
curl -s http://127.0.0.1:5000/api/production-times/export.csv | head -3
```

Then open `http://<pi-ip>:5000` from a browser on the same network.

### If `git pull` refuses to run

The Pi should be a read-only mirror of GitHub — never edit files there. If it
complains about local changes anyway, throw the Pi-side edits away and take the
GitHub version:

```bash
cd ~/AroNet
git checkout -- .          # discard local edits (keeps inventory.db, which is untracked)
git pull
```

### Note on the service file

`deploy/aronet.service` is written for a user named `aronet` with the checkout at
`/home/aronet/AroNet`. If you log in as `pi` and pull to `/home/pi/AroNet`, edit
`User=`, `Group=`, `WorkingDirectory=`, and `ExecStart=` in
`/etc/systemd/system/aronet.service` to say `pi` and `/home/pi/AroNet`, then run
`sudo systemctl daemon-reload && sudo systemctl restart aronet.service`.

## Troubleshooting

### Backend won't start
- Check Python version: `python --version` (need 3.8+)
- Delete `backend/inventory.db` and try again
- Check port 5000 is not in use: `netstat -ano | findstr :5000`

### Browser can't connect to http://localhost:5000
- Backend crashed - check console for errors
- Firewall blocking - add exception for Python
- Port 5000 in use - kill other process or change port in app.py

### Database error
- Delete `backend/inventory.db`
- Run `start.bat` again - will reinitialize

## Architecture Recap

```
┌─────────────────────────────────────────────────────────┐
│  You Are Here: Backend Running ✓                        │
├─────────────────────────────────────────────────────────┤
│  ✓ REST API with full CRUD for inventory                 │
│  ✓ Web dashboard for managing products/parts/jobs        │
│  ✓ SQLite database with audit log                        │
│  ✓ Device status tracking                               │
│                                                          │
│  Next: Implement ESP32 device client & GUI               │
│  Parallelize: Can test API before firmware complete      │
└─────────────────────────────────────────────────────────┘
```

---

**Questions?** Check `backend/README.md` for API details or `main/aronet_device_client.h` for device integration.
