import sqlite3
import json
from datetime import datetime
from pathlib import Path

DB_PATH = Path(__file__).parent / "inventory.db"

def get_db():
    """Get database connection."""
    conn = sqlite3.connect(str(DB_PATH))
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    """Initialize database schema."""
    conn = get_db()
    c = conn.cursor()
    
    # Parts/Components table
    c.execute('''CREATE TABLE IF NOT EXISTS parts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        part_number TEXT UNIQUE NOT NULL,
        name TEXT NOT NULL,
        description TEXT,
        quantity INTEGER DEFAULT 0,
        reorder_level INTEGER DEFAULT 10,
        unit TEXT DEFAULT 'pcs',
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )''')
    
    # Operations/Jobs table (definitions)
    c.execute('''CREATE TABLE IF NOT EXISTS operations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT UNIQUE NOT NULL,
        description TEXT,
        estimated_time_minutes INTEGER DEFAULT 30,
        requires_part TEXT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )''')
    
    # Products table
    c.execute('''CREATE TABLE IF NOT EXISTS products (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        product_code TEXT UNIQUE NOT NULL,
        name TEXT NOT NULL,
        quantity_to_build INTEGER DEFAULT 1,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )''')
    
    # Product-Operation mapping (which jobs make up this product)
    c.execute('''CREATE TABLE IF NOT EXISTS product_operations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        product_id INTEGER NOT NULL,
        operation_id INTEGER NOT NULL,
        sequence_order INTEGER,
        FOREIGN KEY (product_id) REFERENCES products(id),
        FOREIGN KEY (operation_id) REFERENCES operations(id),
        UNIQUE(product_id, operation_id)
    )''')
    
    # Product-Part requirements
    c.execute('''CREATE TABLE IF NOT EXISTS product_parts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        product_id INTEGER NOT NULL,
        part_id INTEGER NOT NULL,
        quantity_per_unit INTEGER DEFAULT 1,
        FOREIGN KEY (product_id) REFERENCES products(id),
        FOREIGN KEY (part_id) REFERENCES parts(id),
        UNIQUE(product_id, part_id)
    )''')
    
    # Job Queue (production jobs)
    c.execute('''CREATE TABLE IF NOT EXISTS job_queue (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        product_id INTEGER NOT NULL,
        operation_id INTEGER NOT NULL,
        batch_number TEXT NOT NULL,
        status TEXT DEFAULT 'pending',
        assigned_device_id TEXT,
        started_at TIMESTAMP,
        completed_at TIMESTAMP,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (product_id) REFERENCES products(id),
        FOREIGN KEY (operation_id) REFERENCES operations(id)
    )''')
    
    # Device Status
    c.execute('''CREATE TABLE IF NOT EXISTS device_status (
        device_id TEXT PRIMARY KEY,
        last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        wifi_signal INTEGER,
        current_job_id INTEGER,
        status TEXT DEFAULT 'idle',
        FOREIGN KEY (current_job_id) REFERENCES job_queue(id)
    )''')
    
    # Inventory Audit Log
    c.execute('''CREATE TABLE IF NOT EXISTS audit_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        part_id INTEGER,
        operation TEXT,
        quantity_change INTEGER,
        reason TEXT,
        device_id TEXT,
        timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (part_id) REFERENCES parts(id)
    )''')
    
    conn.commit()
    conn.close()

def seed_demo_data():
    """Seed database with demo data."""
    # Ensure tables exist first
    init_db()
    
    conn = get_db()
    c = conn.cursor()
    
    try:
        # Demo parts
        c.execute("INSERT INTO parts (part_number, name, description, quantity, reorder_level) VALUES (?, ?, ?, ?, ?)",
                  ("PN-MOTOR-01", "Motor 3HP", "3 Horsepower electric motor", 50, 5))
        c.execute("INSERT INTO parts (part_number, name, description, quantity, reorder_level) VALUES (?, ?, ?, ?, ?)",
                  ("PN-COMPRESSOR-01", "Compressor", "Industrial compressor unit", 20, 3))
        c.execute("INSERT INTO parts (part_number, name, description, quantity, reorder_level) VALUES (?, ?, ?, ?, ?)",
                  ("PN-STEEL-FRAME", "Steel Frame", "Main structural frame", 100, 10))
        c.execute("INSERT INTO parts (part_number, name, description, quantity, reorder_level) VALUES (?, ?, ?, ?, ?)",
                  ("PN-WIRING-KIT", "Wiring Kit", "Complete wiring harness", 80, 5))
        
        # Demo operations
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Laser Cutting", "Cut metal components", 15))
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Bending", "Bend steel components", 20))
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Wiring", "Install electrical wiring", 30))
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Assembly", "Assemble sub-assemblies", 45))
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Final Assembly", "Final product assembly", 60))
        c.execute("INSERT INTO operations (name, description, estimated_time_minutes) VALUES (?, ?, ?)",
                  ("Inspection", "Quality inspection", 20))
        
        # Demo product: CS20
        c.execute("INSERT INTO products (product_code, name, quantity_to_build) VALUES (?, ?, ?)",
                  ("CS20", "CS20 Industrial Dehumidifier", 15))
        
        conn.commit()
        print("✓ Demo data seeded successfully")
    except sqlite3.IntegrityError:
        print("✓ Demo data already exists")
    except Exception as e:
        print(f"Note: {e}")
    finally:
        conn.close()
