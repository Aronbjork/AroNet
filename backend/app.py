from flask import Flask, Response, jsonify, request, render_template
from flask_cors import CORS
from database import init_db, get_db
from datetime import datetime
import csv
import io
import json

app = Flask(__name__, template_folder='templates', static_folder='static')
CORS(app)

# Initialize database schema on startup. Demo data is never auto-seeded here -
# it previously reappeared after every restart even when deleted through the UI,
# because seed_demo_data() ran unconditionally on every app start. Fresh installs
# still get one-time demo data from start.sh/start.bat (only when inventory.db
# doesn't exist yet); see database.seed_demo_data() to run it again by hand.
@app.before_request
def startup():
    if not hasattr(app, 'db_initialized'):
        init_db()
        app.db_initialized = True

# ============ API ENDPOINTS ============

# --- PARTS ---
@app.route('/api/parts', methods=['GET'])
def get_parts():
    """Get all parts with current inventory."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT * FROM parts ORDER BY part_number")
    parts = [dict(row) for row in c.fetchall()]
    conn.close()
    return jsonify(parts)

@app.route('/api/parts', methods=['POST'])
def create_part():
    """Create new part."""
    data = request.json
    conn = get_db()
    c = conn.cursor()
    try:
        c.execute("""INSERT INTO parts (part_number, name, description, quantity, unit)
                 VALUES (?, ?, ?, ?, ?)""",
              (data.get('part_number'), data.get('name'), data.get('description', ''),
               data.get('quantity', 0), data.get('unit', 'pcs')))
        conn.commit()
        part_id = c.lastrowid
        conn.close()
        return jsonify({'id': part_id, 'status': 'created'}), 201
    except Exception as e:
        conn.close()
        return jsonify({'error': str(e)}), 400

@app.route('/api/parts/<int:part_id>', methods=['PUT'])
def update_part(part_id):
    """Update part details."""
    data = request.json
    conn = get_db()
    c = conn.cursor()
    c.execute("""UPDATE parts SET name=?, description=?, reorder_level=?, unit=? 
                 WHERE id=?""",
              (data.get('name'), data.get('description', ''), 
               data.get('reorder_level', 10), data.get('unit', 'pcs'), part_id))
    conn.commit()
    conn.close()
    return jsonify({'status': 'updated'})

@app.route('/api/parts/<int:part_id>/adjust', methods=['POST'])
def adjust_part_quantity(part_id):
    """Add or trim stock while preserving an audit record."""
    data = request.json or {}
    quantity_change = data.get('quantity_change')
    reason = data.get('reason', 'Manual adjustment')
    if not isinstance(quantity_change, int) or quantity_change == 0:
        return jsonify({'error': 'Quantity change must be a non-zero whole number'}), 400

    conn = get_db()
    c = conn.cursor()
    c.execute("""UPDATE parts SET quantity = quantity + ?
                 WHERE id = ? AND quantity + ? >= 0""",
              (quantity_change, part_id, quantity_change))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Part not found or adjustment would make stock negative'}), 400
    c.execute("""INSERT INTO audit_log (part_id, operation, quantity_change, reason, device_id)
                 VALUES (?, 'manual_adjustment', ?, ?, 'dashboard')""",
              (part_id, quantity_change, reason))
    c.execute("SELECT quantity FROM parts WHERE id = ?", (part_id,))
    new_quantity = c.fetchone()[0]
    conn.commit()
    conn.close()
    return jsonify({'status': 'adjusted', 'quantity': new_quantity})

@app.route('/api/parts/<int:part_id>', methods=['DELETE'])
def delete_part(part_id):
    """Delete a part and its historic product references."""
    conn = get_db()
    c = conn.cursor()
    c.execute("DELETE FROM product_parts WHERE part_id = ?", (part_id,))
    c.execute("DELETE FROM audit_log WHERE part_id = ?", (part_id,))
    c.execute("DELETE FROM parts WHERE id = ?", (part_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Part not found'}), 404
    conn.commit()
    conn.close()
    return jsonify({'status': 'deleted'})

# --- OPERATIONS ---
@app.route('/api/operations', methods=['GET'])
def get_operations():
    """Get all operations."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT * FROM operations ORDER BY id")
    operations = [dict(row) for row in c.fetchall()]
    conn.close()
    return jsonify(operations)

@app.route('/api/operations', methods=['POST'])
def create_operation():
    """Create new operation."""
    data = request.json
    conn = get_db()
    c = conn.cursor()
    try:
        c.execute("""INSERT INTO operations (name, description, estimated_time_minutes) 
                     VALUES (?, ?, ?)""",
                  (data.get('name'), data.get('description', ''), data.get('estimated_time_minutes', 30)))
        conn.commit()
        op_id = c.lastrowid
        conn.close()
        return jsonify({'id': op_id, 'status': 'created'}), 201
    except Exception as e:
        conn.close()
        return jsonify({'error': str(e)}), 400

@app.route('/api/operations/<int:operation_id>', methods=['DELETE'])
def delete_operation(operation_id):
    """Delete an operation that is not in a product workflow or job queue."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT COUNT(*) FROM product_operations WHERE operation_id = ?", (operation_id,))
    mapped_products = c.fetchone()[0]
    c.execute("SELECT COUNT(*) FROM job_queue WHERE operation_id = ?", (operation_id,))
    queued_jobs = c.fetchone()[0]
    if mapped_products or queued_jobs:
        conn.close()
        return jsonify({'error': 'Remove this operation from products and delete its jobs before deleting it'}), 409
    c.execute("DELETE FROM operations WHERE id = ?", (operation_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Operation not found'}), 404
    conn.commit()
    conn.close()
    return jsonify({'status': 'deleted'})

# --- PRODUCTS ---
def parse_product_payload(data, require_all=True):
    """Validate a product payload and return (values, error)."""
    product_code = (data.get('product_code') or '').strip()
    name = (data.get('name') or '').strip()
    if require_all and (not product_code or not name):
        return None, 'Product code and name are required'

    quantity_to_build = data.get('quantity_to_build', 1)
    if not isinstance(quantity_to_build, int) or quantity_to_build < 1:
        return None, 'Quantity to build must be a positive whole number'

    parts = data.get('parts')
    if parts is not None:
        seen_part_ids = set()
        for part_spec in parts:
            part_id = part_spec.get('part_id')
            quantity_per_unit = part_spec.get('quantity_per_unit', 1)
            if not isinstance(part_id, int):
                return None, 'Every required part needs a part id'
            if not isinstance(quantity_per_unit, int) or quantity_per_unit < 1:
                return None, 'Quantity per unit must be a positive whole number'
            if part_id in seen_part_ids:
                return None, 'A part can only be listed once per product'
            seen_part_ids.add(part_id)

    operation_ids = data.get('operation_ids')
    if operation_ids is not None:
        if len(set(operation_ids)) != len(operation_ids):
            return None, 'An operation can only be listed once per product'

    return {
        'product_code': product_code,
        'name': name,
        'quantity_to_build': quantity_to_build,
        'operation_ids': operation_ids,
        'parts': parts,
    }, None

def replace_product_operations(cursor, product_id, operation_ids):
    """Replace the workflow of a product with the given ordered operations."""
    cursor.execute("DELETE FROM product_operations WHERE product_id = ?", (product_id,))
    for sequence_order, operation_id in enumerate(operation_ids):
        cursor.execute("""INSERT INTO product_operations (product_id, operation_id, sequence_order)
                          VALUES (?, ?, ?)""", (product_id, operation_id, sequence_order))

def replace_product_parts(cursor, product_id, parts):
    """Replace the bill of materials of a product with the given parts."""
    cursor.execute("DELETE FROM product_parts WHERE product_id = ?", (product_id,))
    for part_spec in parts:
        cursor.execute("""INSERT INTO product_parts (product_id, part_id, quantity_per_unit)
                          VALUES (?, ?, ?)""",
                       (product_id, part_spec['part_id'], part_spec.get('quantity_per_unit', 1)))

@app.route('/api/products', methods=['GET'])
def get_products():
    """Get all products with their operations and parts."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT * FROM products ORDER BY product_code")
    products = [dict(row) for row in c.fetchall()]
    
    for product in products:
        # Get operations for this product
        c.execute("""SELECT o.* FROM operations o 
                     JOIN product_operations po ON o.id = po.operation_id 
                     WHERE po.product_id = ? ORDER BY po.sequence_order""", (product['id'],))
        product['operations'] = [dict(row) for row in c.fetchall()]
        
        # Get parts for this product
        c.execute("""SELECT p.*, pp.quantity_per_unit FROM parts p 
                     JOIN product_parts pp ON p.id = pp.part_id 
                     WHERE pp.product_id = ?""", (product['id'],))
        product['parts'] = [dict(row) for row in c.fetchall()]
    
    conn.close()
    return jsonify(products)

@app.route('/api/products', methods=['POST'])
def create_product():
    """Create new product."""
    values, error = parse_product_payload(request.json or {})
    if error:
        return jsonify({'error': error}), 400

    conn = get_db()
    c = conn.cursor()
    try:
        c.execute("""INSERT INTO products (product_code, name, quantity_to_build)
                     VALUES (?, ?, ?)""",
                  (values['product_code'], values['name'], values['quantity_to_build']))
        product_id = c.lastrowid
        replace_product_operations(c, product_id, values['operation_ids'] or [])
        replace_product_parts(c, product_id, values['parts'] or [])
        conn.commit()
        conn.close()
        return jsonify({'id': product_id, 'status': 'created'}), 201
    except Exception as e:
        conn.rollback()
        conn.close()
        return jsonify({'error': str(e)}), 400

@app.route('/api/products/<int:product_id>', methods=['PUT'])
def update_product(product_id):
    """Update a product, including its workflow and its required parts."""
    values, error = parse_product_payload(request.json or {})
    if error:
        return jsonify({'error': error}), 400

    conn = get_db()
    c = conn.cursor()
    try:
        c.execute("SELECT id FROM products WHERE id = ?", (product_id,))
        if not c.fetchone():
            conn.close()
            return jsonify({'error': 'Product not found'}), 404

        c.execute("""UPDATE products SET product_code = ?, name = ?, quantity_to_build = ?
                     WHERE id = ?""",
                  (values['product_code'], values['name'], values['quantity_to_build'], product_id))
        if values['operation_ids'] is not None:
            replace_product_operations(c, product_id, values['operation_ids'])
        if values['parts'] is not None:
            replace_product_parts(c, product_id, values['parts'])
        conn.commit()
        conn.close()
        return jsonify({'status': 'updated'})
    except Exception as e:
        conn.rollback()
        conn.close()
        return jsonify({'error': str(e)}), 400

@app.route('/api/products/<int:product_id>', methods=['DELETE'])
def delete_product(product_id):
    """Delete a product and its definition links when it has no jobs."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT COUNT(*) FROM job_queue WHERE product_id = ?", (product_id,))
    if c.fetchone()[0]:
        conn.close()
        return jsonify({'error': 'Delete this product\'s jobs before deleting the product'}), 409
    c.execute("DELETE FROM product_operations WHERE product_id = ?", (product_id,))
    c.execute("DELETE FROM product_parts WHERE product_id = ?", (product_id,))
    c.execute("DELETE FROM products WHERE id = ?", (product_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Product not found'}), 404
    conn.commit()
    conn.close()
    return jsonify({'status': 'deleted'})

# --- JOB QUEUE ---
def next_batch_number(cursor, now=None):
    """Build a short batch number for today, such as BATCH-260902-01."""
    prefix = 'BATCH-' + (now or datetime.now()).strftime('%y%m%d')
    cursor.execute("SELECT batch_number FROM job_queue WHERE batch_number LIKE ?", (prefix + '-%',))
    highest = 0
    for row in cursor.fetchall():
        suffix = row['batch_number'].rsplit('-', 1)[-1]
        if suffix.isdigit():
            highest = max(highest, int(suffix))
    return f'{prefix}-{highest + 1:02d}'

@app.route('/api/jobs', methods=['GET'])
def get_jobs():
    """Get job queue (with optional filtering by device or status)."""
    device_id = request.args.get('device_id')
    status = request.args.get('status')
    limit = request.args.get('limit', type=int)
    
    conn = get_db()
    c = conn.cursor()
    
    query = """SELECT j.*, p.product_code, p.name as product_name, o.name as operation_name 
               FROM job_queue j 
               JOIN products p ON j.product_id = p.id 
               JOIN operations o ON j.operation_id = o.id 
               WHERE 1=1"""
    params = []
    
    if device_id:
        query += " AND j.assigned_device_id = ?"
        params.append(device_id)
    if status == 'available':
        query += " AND j.status IN ('pending', 'paused')"
    elif status:
        query += " AND j.status = ?"
        params.append(status)
    
    query += " ORDER BY j.created_at"
    if limit is not None:
        query += " LIMIT ?"
        params.append(max(1, min(limit, 50)))
    
    c.execute(query, params)
    jobs = [dict(row) for row in c.fetchall()]
    conn.close()
    return jsonify(jobs)

@app.route('/api/jobs', methods=['POST'])
def create_jobs():
    """Create jobs for a product build (batch)."""
    data = request.json
    product_id = data.get('product_id')
    quantity = data.get('quantity', 1)

    if not isinstance(quantity, int) or quantity < 1:
        return jsonify({'error': 'Quantity must be a positive whole number'}), 400

    conn = get_db()
    c = conn.cursor()
    batch_number = (data.get('batch_number') or '').strip() or next_batch_number(c)

    # Get operations for this product
    c.execute("""SELECT o.id FROM operations o 
                 JOIN product_operations po ON o.id = po.operation_id 
                 WHERE po.product_id = ? ORDER BY po.sequence_order""", (product_id,))
    operations = [row[0] for row in c.fetchall()]

    if not operations:
        conn.close()
        return jsonify({'error': 'The selected product has no operations'}), 400

    jobs_created = []
    try:
        for op_id in operations:
            c.execute("""INSERT INTO job_queue
                         (product_id, operation_id, batch_number, quantity, status)
                         VALUES (?, ?, ?, ?, 'pending')""",
                      (product_id, op_id, batch_number, quantity))
            jobs_created.append(c.lastrowid)
        
        conn.commit()
        conn.close()
        return jsonify({'jobs_created': len(jobs_created), 'job_ids': jobs_created}), 201
    except Exception as e:
        conn.close()
        return jsonify({'error': str(e)}), 400

@app.route('/api/jobs/<int:job_id>', methods=['DELETE'])
def delete_job(job_id):
    """Delete a queued job."""
    conn = get_db()
    c = conn.cursor()
    c.execute("DELETE FROM job_queue WHERE id = ?", (job_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Job not found'}), 404
    conn.commit()
    conn.close()
    return jsonify({'status': 'deleted'})

@app.route('/api/jobs/<int:job_id>/cancel', methods=['PUT'])
def cancel_job(job_id):
    """Legacy alias for pausing a job without deleting it."""
    return pause_job(job_id)

@app.route('/api/jobs/<int:job_id>/pause', methods=['PUT'])
def pause_job(job_id):
    """Pause an active job and preserve its accumulated work time."""
    conn = get_db()
    c = conn.cursor()
    c.execute("""UPDATE job_queue
                 SET status = 'paused', assigned_device_id = NULL,
                     elapsed_seconds = elapsed_seconds + CAST(
                         (julianday('now') - julianday(started_at)) * 86400 AS INTEGER),
                     started_at = NULL
                 WHERE id = ? AND status = 'in_progress'""", (job_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Only an in-progress job can be paused'}), 409
    conn.commit()
    conn.close()
    return jsonify({'status': 'paused'})

@app.route('/api/jobs/<int:job_id>/assign', methods=['PUT'])
def assign_job(job_id):
    """Assign a job to a device, or an operator working from the dashboard."""
    data = request.json or {}
    device_id = (data.get('device_id') or '').strip()
    if not device_id:
        return jsonify({'error': 'device_id is required'}), 400

    conn = get_db()
    c = conn.cursor()
    c.execute("UPDATE job_queue SET assigned_device_id = ?, status = 'assigned' WHERE id = ?",
              (device_id, job_id))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Job not found'}), 404
    conn.commit()
    conn.close()
    return jsonify({'status': 'assigned'})

@app.route('/api/jobs/<int:job_id>/start', methods=['PUT'])
def start_job(job_id):
    """Start a job."""
    conn = get_db()
    c = conn.cursor()
    c.execute("""UPDATE job_queue SET status = 'in_progress', started_at = CURRENT_TIMESTAMP
                 WHERE id = ? AND status IN ('pending', 'paused', 'assigned')""", (job_id,))
    if not c.rowcount:
        conn.close()
        return jsonify({'error': 'Job not found or cannot be started'}), 409
    conn.commit()
    conn.close()
    return jsonify({'status': 'started'})

@app.route('/api/jobs/<int:job_id>/complete', methods=['PUT'])
def complete_job(job_id):
    """Complete a job and consume its bill of materials on final operation."""
    conn = get_db()
    c = conn.cursor()
    
    try:
        c.execute("SELECT product_id, operation_id, quantity, status FROM job_queue WHERE id = ?", (job_id,))
        job = c.fetchone()
        if not job:
            conn.close()
            return jsonify({'error': 'Job not found'}), 404
        product_id, operation_id, job_quantity, job_status = job
        if job_status == 'completed':
            conn.close()
            return jsonify({'error': 'Job is already completed'}), 409

        c.execute("SELECT MAX(sequence_order) FROM product_operations WHERE product_id = ?", (product_id,))
        final_sequence = c.fetchone()[0]
        c.execute("""SELECT sequence_order FROM product_operations
                     WHERE product_id = ? AND operation_id = ?""", (product_id, operation_id))
        operation = c.fetchone()
        if not operation:
            conn.close()
            return jsonify({'error': 'Job operation is not part of its product workflow'}), 400

        if operation[0] == final_sequence:
            c.execute("""SELECT p.id, p.name, p.quantity, pp.quantity_per_unit
                         FROM parts p JOIN product_parts pp ON pp.part_id = p.id
                         WHERE pp.product_id = ?""", (product_id,))
            required_parts = c.fetchall()
            shortages = [row['name'] for row in required_parts
                         if row['quantity'] < row['quantity_per_unit'] * job_quantity]
            if shortages:
                conn.close()
                return jsonify({'error': 'Insufficient stock: ' + ', '.join(shortages)}), 409

            for part in required_parts:
                used_quantity = part['quantity_per_unit'] * job_quantity
                c.execute("UPDATE parts SET quantity = quantity - ? WHERE id = ?",
                          (used_quantity, part['id']))
                c.execute("""INSERT INTO audit_log (part_id, operation, quantity_change, reason, device_id)
                             VALUES (?, 'production_consumption', ?, ?, ?)""",
                          (part['id'], -used_quantity, f'Job {job_id} completed',
                           (request.json or {}).get('device_id', 'dashboard')))

        # Mark job complete and bank the time worked since it was last started
        c.execute("""UPDATE job_queue
                     SET status = 'completed', completed_at = CURRENT_TIMESTAMP,
                         elapsed_seconds = elapsed_seconds + CASE
                             WHEN started_at IS NULL THEN 0
                             ELSE CAST((julianday('now') - julianday(started_at)) * 86400 AS INTEGER)
                         END
                     WHERE id = ?""", (job_id,))
        conn.commit()
        conn.close()
        return jsonify({'status': 'completed'})
    except Exception as e:
        conn.close()
        return jsonify({'error': str(e)}), 400

# --- DEVICE STATUS ---
@app.route('/api/devices/<device_id>/status', methods=['GET', 'POST'])
def device_status(device_id):
    """Get or update device status and get next job."""
    conn = get_db()
    c = conn.cursor()
    
    if request.method == 'POST':
        # Update device status
        data = request.json
        c.execute("""INSERT INTO device_status (device_id, last_seen, wifi_signal, status) 
                     VALUES (?, CURRENT_TIMESTAMP, ?, ?)
                     ON CONFLICT(device_id) DO UPDATE SET last_seen=CURRENT_TIMESTAMP, 
                     wifi_signal=excluded.wifi_signal, status=excluded.status""",
                  (device_id, data.get('wifi_signal'), data.get('status', 'idle')))
        conn.commit()
    
    # Get device status
    c.execute("SELECT * FROM device_status WHERE device_id = ?", (device_id,))
    status = dict(c.fetchone() or {})
    
    # Get next pending job for this device
    c.execute("""SELECT j.id, j.product_id, j.operation_id, j.batch_number, j.quantity,
                        p.product_code, o.name as operation_name
                 FROM job_queue j 
                 JOIN products p ON j.product_id = p.id 
                 JOIN operations o ON j.operation_id = o.id 
                 WHERE j.status = 'pending' LIMIT 1""")
    next_job = dict(c.fetchone() or {})
    
    conn.close()
    return jsonify({'device_status': status, 'next_job': next_job})

# --- WEB DASHBOARD ---
@app.route('/')
def dashboard():
    """Render web dashboard."""
    return render_template('dashboard.html')

@app.route('/api/inventory/export.csv', methods=['GET'])
def export_inventory_csv():
    """Export the current inventory in a Fortnox-mappable CSV format."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT part_number, name, description, quantity, unit FROM parts ORDER BY part_number")
    parts = c.fetchall()
    conn.close()

    output = io.StringIO()
    writer = csv.writer(output, delimiter=';')
    writer.writerow(['Artikelnummer', 'Artikelnamn', 'Beskrivning', 'Inventerat antal', 'Enhet'])
    writer.writerows((part['part_number'], part['name'], part['description'] or '',
                      part['quantity'], part['unit']) for part in parts)
    return Response('\ufeff' + output.getvalue(), content_type='text/csv; charset=utf-8', headers={
        'Content-Disposition': 'attachment; filename=aronet-inventory.csv'
    })

@app.route('/api/inventory/import.csv', methods=['POST'])
def import_inventory_csv():
    """Import an AroNet inventory CSV exported by this application."""
    uploaded_file = request.files.get('file')
    if not uploaded_file or not uploaded_file.filename:
        return jsonify({'error': 'Choose a CSV file to import'}), 400

    try:
        content = uploaded_file.read().decode('utf-8-sig')
        rows = list(csv.DictReader(io.StringIO(content), delimiter=';'))
    except UnicodeDecodeError:
        return jsonify({'error': 'CSV must be UTF-8 encoded'}), 400

    required_fields = {'Artikelnummer', 'Artikelnamn', 'Inventerat antal', 'Enhet'}
    if not rows or not required_fields.issubset(rows[0].keys()):
        return jsonify({'error': 'CSV must use the exported AroNet inventory headers'}), 400

    parsed_rows = []
    for row_number, row in enumerate(rows, start=2):
        part_number = (row.get('Artikelnummer') or '').strip()
        name = (row.get('Artikelnamn') or '').strip()
        description = (row.get('Beskrivning') or '').strip()
        unit = (row.get('Enhet') or '').strip() or 'pcs'
        try:
            quantity = int((row.get('Inventerat antal') or '').strip())
        except ValueError:
            return jsonify({'error': f'Row {row_number}: Inventerat antal must be a whole number'}), 400
        if not part_number or not name or quantity < 0:
            return jsonify({'error': f'Row {row_number}: article number, name, and a non-negative quantity are required'}), 400
        parsed_rows.append((part_number, name, description, quantity, unit))

    conn = get_db()
    c = conn.cursor()
    created = 0
    updated = 0
    try:
        for part_number, name, description, quantity, unit in parsed_rows:
            c.execute("SELECT id, quantity FROM parts WHERE part_number = ?", (part_number,))
            existing_part = c.fetchone()
            if existing_part:
                c.execute("UPDATE parts SET name = ?, description = ?, quantity = ?, unit = ? WHERE id = ?",
                          (name, description, quantity, unit, existing_part['id']))
                quantity_change = quantity - existing_part['quantity']
                c.execute("""INSERT INTO audit_log (part_id, operation, quantity_change, reason, device_id)
                             VALUES (?, 'csv_import', ?, 'CSV inventory import', 'dashboard')""",
                          (existing_part['id'], quantity_change))
                updated += 1
            else:
                c.execute("""INSERT INTO parts (part_number, name, description, quantity, unit)
                             VALUES (?, ?, ?, ?, ?)""",
                          (part_number, name, description, quantity, unit))
                c.execute("""INSERT INTO audit_log (part_id, operation, quantity_change, reason, device_id)
                             VALUES (?, 'csv_import', ?, 'CSV inventory import', 'dashboard')""",
                          (c.lastrowid, quantity))
                created += 1
        conn.commit()
    except Exception as error:
        conn.rollback()
        conn.close()
        return jsonify({'error': str(error)}), 400
    conn.close()
    return jsonify({'status': 'imported', 'created': created, 'updated': updated})

def csv_response(rows, header, filename):
    """Render rows as a semicolon separated CSV download."""
    output = io.StringIO()
    writer = csv.writer(output, delimiter=';')
    writer.writerow(header)
    writer.writerows(rows)
    return Response('\ufeff' + output.getvalue(), content_type='text/csv; charset=utf-8', headers={
        'Content-Disposition': f'attachment; filename={filename}'
    })

PRODUCTION_TIME_HEADER = [
    'job_id', 'batch_number', 'product_code', 'product_name', 'operation', 'quantity',
    'status', 'device', 'estimated_minutes_per_unit', 'estimated_minutes_total',
    'actual_minutes', 'actual_minutes_per_unit', 'actual_seconds',
    'created_at', 'started_at', 'completed_at',
]

@app.route('/api/production-times/export.csv', methods=['GET'])
def export_production_times_csv():
    """Export estimated and actual production time per job."""
    status = request.args.get('status')

    conn = get_db()
    c = conn.cursor()
    query = """SELECT j.id, j.batch_number, j.quantity, j.status, j.assigned_device_id,
                      j.elapsed_seconds, j.created_at, j.started_at, j.completed_at,
                      p.product_code, p.name AS product_name,
                      o.name AS operation_name, o.estimated_time_minutes,
                      CASE WHEN j.status = 'in_progress' AND j.started_at IS NOT NULL
                           THEN CAST((julianday('now') - julianday(j.started_at)) * 86400 AS INTEGER)
                           ELSE 0 END AS running_seconds
               FROM job_queue j
               JOIN products p ON j.product_id = p.id
               JOIN operations o ON j.operation_id = o.id
               WHERE 1=1"""
    params = []
    if status == 'available':
        query += " AND j.status IN ('pending', 'paused')"
    elif status:
        query += " AND j.status = ?"
        params.append(status)
    query += " ORDER BY j.created_at, j.id"
    c.execute(query, params)
    jobs = c.fetchall()
    conn.close()

    rows = []
    for job in jobs:
        actual_seconds = (job['elapsed_seconds'] or 0) + (job['running_seconds'] or 0)
        actual_minutes = round(actual_seconds / 60, 2)
        quantity = job['quantity'] or 1
        rows.append([
            job['id'], job['batch_number'], job['product_code'], job['product_name'],
            job['operation_name'], quantity, job['status'], job['assigned_device_id'] or '',
            job['estimated_time_minutes'], job['estimated_time_minutes'] * quantity,
            actual_minutes, round(actual_minutes / quantity, 2), actual_seconds,
            job['created_at'] or '', job['started_at'] or '', job['completed_at'] or '',
        ])
    return csv_response(rows, PRODUCTION_TIME_HEADER, 'aronet-production-times.csv')

PRODUCT_CSV_HEADER = [
    'product_code', 'product_name', 'quantity_to_build', 'operations',
    'part_number', 'part_name', 'quantity_per_unit',
]

@app.route('/api/products/export.csv', methods=['GET'])
def export_products_csv():
    """Export every product with its workflow and one row per required part."""
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT * FROM products ORDER BY product_code")
    products = c.fetchall()

    rows = []
    for product in products:
        c.execute("""SELECT o.name FROM operations o
                     JOIN product_operations po ON o.id = po.operation_id
                     WHERE po.product_id = ? ORDER BY po.sequence_order""", (product['id'],))
        operations = ' | '.join(row['name'] for row in c.fetchall())
        c.execute("""SELECT p.part_number, p.name, pp.quantity_per_unit
                     FROM parts p JOIN product_parts pp ON pp.part_id = p.id
                     WHERE pp.product_id = ? ORDER BY p.part_number""", (product['id'],))
        parts = c.fetchall()
        if not parts:
            rows.append([product['product_code'], product['name'],
                         product['quantity_to_build'], operations, '', '', ''])
            continue
        for part in parts:
            rows.append([product['product_code'], product['name'], product['quantity_to_build'],
                         operations, part['part_number'], part['name'], part['quantity_per_unit']])
    conn.close()
    return csv_response(rows, PRODUCT_CSV_HEADER, 'aronet-products.csv')

@app.route('/api/products/import.csv', methods=['POST'])
def import_products_csv():
    """Import products, workflows, and bills of materials from an AroNet product CSV."""
    uploaded_file = request.files.get('file')
    if not uploaded_file or not uploaded_file.filename:
        return jsonify({'error': 'Choose a CSV file to import'}), 400

    try:
        content = uploaded_file.read().decode('utf-8-sig')
        rows = list(csv.DictReader(io.StringIO(content), delimiter=';'))
    except UnicodeDecodeError:
        return jsonify({'error': 'CSV must be UTF-8 encoded'}), 400

    required_fields = {'product_code', 'product_name', 'part_number', 'quantity_per_unit'}
    if not rows or not required_fields.issubset(rows[0].keys()):
        return jsonify({'error': 'CSV must use the exported AroNet product headers'}), 400

    # Collect the rows per product so each product is written in one pass.
    products = {}
    order = []
    for row_number, row in enumerate(rows, start=2):
        product_code = (row.get('product_code') or '').strip()
        product_name = (row.get('product_name') or '').strip()
        if not product_code:
            return jsonify({'error': f'Row {row_number}: product_code is required'}), 400

        quantity_to_build = (row.get('quantity_to_build') or '').strip() or '1'
        try:
            quantity_to_build = int(quantity_to_build)
        except ValueError:
            return jsonify({'error': f'Row {row_number}: quantity_to_build must be a whole number'}), 400
        if quantity_to_build < 1:
            return jsonify({'error': f'Row {row_number}: quantity_to_build must be at least 1'}), 400

        operations = [name.strip() for name in (row.get('operations') or '').split('|') if name.strip()]

        if product_code not in products:
            products[product_code] = {'name': product_name, 'quantity_to_build': quantity_to_build,
                                      'operations': operations, 'parts': []}
            order.append(product_code)
        product = products[product_code]
        if product_name:
            product['name'] = product_name
        if operations:
            product['operations'] = operations

        part_number = (row.get('part_number') or '').strip()
        if not part_number:
            continue
        try:
            quantity_per_unit = int((row.get('quantity_per_unit') or '').strip() or '1')
        except ValueError:
            return jsonify({'error': f'Row {row_number}: quantity_per_unit must be a whole number'}), 400
        if quantity_per_unit < 1:
            return jsonify({'error': f'Row {row_number}: quantity_per_unit must be at least 1'}), 400
        if any(existing['part_number'] == part_number for existing in product['parts']):
            return jsonify({'error': f'Row {row_number}: {part_number} is listed twice for {product_code}'}), 400
        product['parts'].append({'part_number': part_number, 'quantity_per_unit': quantity_per_unit,
                                 'row_number': row_number})

    conn = get_db()
    c = conn.cursor()
    created = 0
    updated = 0
    try:
        for product_code in order:
            product = products[product_code]
            if not product['name']:
                conn.rollback()
                conn.close()
                return jsonify({'error': f'{product_code}: product_name is required'}), 400

            part_specs = []
            for part in product['parts']:
                c.execute("SELECT id FROM parts WHERE part_number = ?", (part['part_number'],))
                existing_part = c.fetchone()
                if not existing_part:
                    conn.rollback()
                    conn.close()
                    return jsonify({'error': f"Row {part['row_number']}: unknown part {part['part_number']}."
                                             ' Import the inventory CSV or add the part first'}), 400
                part_specs.append({'part_id': existing_part['id'],
                                   'quantity_per_unit': part['quantity_per_unit']})

            operation_ids = []
            for operation_name in product['operations']:
                c.execute("SELECT id FROM operations WHERE name = ?", (operation_name,))
                existing_operation = c.fetchone()
                if not existing_operation:
                    conn.rollback()
                    conn.close()
                    return jsonify({'error': f'{product_code}: unknown operation "{operation_name}".'
                                             ' Add the operation first'}), 400
                operation_ids.append(existing_operation['id'])

            c.execute("SELECT id FROM products WHERE product_code = ?", (product_code,))
            existing_product = c.fetchone()
            if existing_product:
                product_id = existing_product['id']
                c.execute("UPDATE products SET name = ?, quantity_to_build = ? WHERE id = ?",
                          (product['name'], product['quantity_to_build'], product_id))
                updated += 1
            else:
                c.execute("""INSERT INTO products (product_code, name, quantity_to_build)
                             VALUES (?, ?, ?)""",
                          (product_code, product['name'], product['quantity_to_build']))
                product_id = c.lastrowid
                created += 1

            if product['operations']:
                replace_product_operations(c, product_id, operation_ids)
            replace_product_parts(c, product_id, part_specs)
        conn.commit()
    except Exception as error:
        conn.rollback()
        conn.close()
        return jsonify({'error': str(error)}), 400
    conn.close()
    return jsonify({'status': 'imported', 'created': created, 'updated': updated})

@app.route('/api/dashboard/stats', methods=['GET'])
def get_dashboard_stats():
    """Get dashboard statistics."""
    conn = get_db()
    c = conn.cursor()
    
    c.execute("SELECT COUNT(*) FROM parts")
    parts_count = c.fetchone()[0]

    c.execute("SELECT COUNT(*) FROM job_queue WHERE status = 'pending'")
    pending_jobs = c.fetchone()[0]
    
    c.execute("SELECT COUNT(*) FROM job_queue WHERE status = 'in_progress'")
    in_progress_jobs = c.fetchone()[0]
    
    c.execute("SELECT COUNT(*) FROM job_queue WHERE status = 'completed'")
    completed_jobs = c.fetchone()[0]
    
    c.execute("SELECT COUNT(*) FROM device_status")
    devices_connected = c.fetchone()[0]
    
    conn.close()
    
    return jsonify({
        'parts_count': parts_count,
        'pending_jobs': pending_jobs,
        'in_progress_jobs': in_progress_jobs,
        'completed_jobs': completed_jobs,
        'devices_connected': devices_connected
    })

# ============ ERROR HANDLERS ============
@app.errorhandler(404)
def not_found(e):
    return jsonify({'error': 'Not found'}), 404

@app.errorhandler(500)
def internal_error(e):
    return jsonify({'error': 'Internal server error'}), 500

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)
