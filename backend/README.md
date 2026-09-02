# AroNet Backend - Manufacturing Inventory System

## Quick Start

### 1. Install Python Dependencies
```powershell
cd backend
pip install -r requirements.txt
```

### 2. Initialize Database
```powershell
python -c "from database import init_db, seed_demo_data; init_db(); seed_demo_data()"
```

### 3. Run the Server
```powershell
python app.py
```

The system will start at **http://localhost:5000**

## API Endpoints

### Parts Management
- `GET /api/parts` - List all parts
- `POST /api/parts` - Create new part
- `PUT /api/parts/<id>` - Update part

### Operations
- `GET /api/operations` - List all operations
- `POST /api/operations` - Create new operation

### Products
- `GET /api/products` - List all products with operations & parts
- `POST /api/products` - Create new product
- `PUT /api/products/<id>` - Update a product, its operations, and its required parts
- `GET /api/products/export.csv` - Export products with one row per required part
- `POST /api/products/import.csv` - Import products, workflows, and required parts

### CSV Exports
- `GET /api/inventory/export.csv` - Current stock in a Fortnox-mappable layout
- `POST /api/inventory/import.csv` - Import stock levels
- `GET /api/production-times/export.csv` - Actual time per job
  (add `?status=completed` for finished jobs only)

### Full Database Backup
- `GET /api/backup/export.zip` - Every table as one CSV per file inside a zip -
  a complete snapshot, not just parts or products.
- `POST /api/backup/import.zip` - Restore from that zip. This **replaces every
  row in every table** with the backup's contents, including primary keys, so
  relationships (which parts a product needs, which jobs belong to which batch)
  come back intact. Must be sent as multipart form data with the zip as `file`
  and a form field `confirm` set to exactly `REPLACE ALL DATA`, or the request
  is rejected - this isn't a merge or an upsert like the parts/products CSVs,
  it's a full restore.

All CSV files are semicolon separated and UTF-8 with a BOM, so Excel opens them
directly. The product CSV has one row per required part, repeating the product
columns, and lists the workflow as `Operation A | Operation B` in the
`operations` column:

```
product_code;product_name;quantity_to_build;operations;part_number;part_name;quantity_per_unit
CS20;CS20 Industrial Dehumidifier;15;Laser Cutting | Bending;PN-MOTOR-01;Motor 3HP;2
CS20;CS20 Industrial Dehumidifier;15;Laser Cutting | Bending;PN-STEEL-FRAME;Steel Frame;1
```

Importing matches products on `product_code` (creating missing ones) and
replaces that product's required parts with the rows in the file. Parts are
matched by `part_number` and operations by name — both must already exist, so
import the inventory CSV first. Leaving the `operations` column empty keeps the
product's existing workflow.

### Job Queue
- `GET /api/jobs?device_id=DEVICE1` - Get jobs for a device
- `POST /api/jobs` - Create jobs for a product batch
- `PUT /api/jobs/<id>/assign` - Assign job to device
- `PUT /api/jobs/<id>/start` - Start a job
- `PUT /api/jobs/<id>/complete` - Complete job & decrement inventory

### Device Status
- `GET /api/devices/<device_id>/status` - Get device status & next job
- `POST /api/devices/<device_id>/status` - Update device status

## Database Schema

### Parts Table
```
id, part_number (unique), name, description, quantity, reorder_level, unit, created_at
```

### Operations Table
```
id, name (unique), description, created_at
```

### Products Table
```
id, product_code (unique), name, quantity_to_build, created_at
```

### Job Queue
```
id, product_id, operation_id, batch_number, status, assigned_device_id, started_at, completed_at, created_at
```

### Device Status
```
device_id (pk), last_seen, wifi_signal, current_job_id, status
```

## Demo Data

The system comes pre-seeded with:
- **Product**: CS20 (Industrial Dehumidifier) - 15 units to build
- **Operations**: Laser Cutting, Bending, Wiring, Assembly, Final Assembly, Inspection
- **Parts**: Motor, Compressor, Steel Frame, Wiring Kit

## Testing with cURL

```bash
# Get all parts
curl http://localhost:5000/api/parts

# Create a new part
curl -X POST http://localhost:5000/api/parts \
  -H "Content-Type: application/json" \
  -d '{"part_number":"PN-TEST-01","name":"Test Part","quantity":50}'

# Get device status
curl http://localhost:5000/api/devices/DISPLAY-01/status

# Create jobs for product 1, quantity 5
curl -X POST http://localhost:5000/api/jobs \
  -H "Content-Type: application/json" \
  -d '{"product_id":1,"quantity":5}'

# Complete a job
curl -X PUT http://localhost:5000/api/jobs/1/complete \
  -H "Content-Type: application/json" \
  -d '{"device_id":"DISPLAY-01"}'
```

## Web Dashboard

Open browser to: **http://localhost:5000**

Features:
- Dashboard with real-time stats
- Add/view parts inventory
- Define operations/jobs
- Create products with operations
- Generate job batches
- Monitor job status

## Next Steps for ESP32 Integration

The ESP32 devices will:
1. Connect to WiFi
2. Call `GET /api/devices/<device_id>/status` to get next job
3. Display job on LVGL screen
4. On job completion, call `PUT /api/jobs/<id>/complete`

See `esp32_device_client.h` (coming next) for integration.
