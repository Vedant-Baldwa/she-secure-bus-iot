import sqlite3
import time
import threading
from flask import Flask, render_template, request, redirect
import paho.mqtt.client as mqtt
from datetime import datetime
# ================== MQTT Settings ==================
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPICS = [
    ("shesecure/keypad_id", 0),
    ("shesecure/panic", 0),
    ("shesecure/alcohol", 0),
    ("shesecure/noise", 0)
]

MQTT_ALERT_TOPIC = "shesecure/alerts"
MQTT_LCD_TOPIC = "shesecure/custom_lcd"
MQTT_NAME_TOPIC = "shesecure/display_name"


# ================== Flask App ======------------------------

app = Flask(__name__)

# ================== Global Counters ===----------------------------------
male_count = 0
female_count = 0

last_events={}
DEDUP_WINDOW=2
de_dup_lock=threading.Lock()

# ================== SQLite Database Setup ===--------------------------
def init_db():
	conn = sqlite3.connect('database.db')
	c = conn.cursor()
	c.execute('''CREATE TABLE IF NOT EXISTS logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT,
                    event_type TEXT,
                    details TEXT
                )''')
	c.execute('''CREATE TABLE IF NOT EXISTS authorized_students (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    uid TEXT UNIQUE,
                    name TEXT,
                    gender TEXT
                )''')
	conn.commit()
	conn.close()

# ================== MQTT Handlers ==----------------

client = mqtt.Client(protocol=mqtt.MQTTv311)

def on_connect(client, userdata, flags, rc):
	print("Connected to MQTT Broker with result code "+str(rc))
	client.subscribe(MQTT_TOPICS)

def on_message(client, userdata, msg):
	global male_count, female_count
	
	payload = msg.payload.decode()
	key=(msg.topic,payload)
	now=time.time()
	
	with de_dup_lock:
		prev=last_events.get(key,0)
		if(now-prev<DEDUP_WINDOW):
			return
		last_events[key]=now
	
	timestamp=datetime.now().strftime('%Y-%m-%d %H:%M:%S')
	print(f"Received on {msg.topic}: at {timestamp}: {msg.payload.decode()}")
	conn = sqlite3.connect('database.db')
	c = conn.cursor()
	
	if msg.topic == "shesecure/keypad_id":
		c.execute("SELECT name, gender FROM authorized_students WHERE uid = ?", (payload,))
		result = c.fetchone()
		if result:
			student_name, gender = result
			print(f"Authorized Student: {student_name}, Gender: {gender}")
			client.publish(MQTT_NAME_TOPIC, f"{student_name}")
			c.execute("INSERT INTO logs (timestamp, event_type, details) VALUES (?, 'RFID', ?)", (timestamp,student_name))
			if gender == "Male":
				male_count += 1
			elif gender == "Female":
				female_count += 1
		else:
			print("Unauthorized Access!")
			client.publish(MQTT_NAME_TOPIC, "UNAUTHORIZED!")
			c.execute("INSERT INTO logs (timestamp, event_type, details) VALUES (?, 'RFID', 'Unauthorized')",(timestamp,))
	
	elif msg.topic == "shesecure/panic":
		c.execute("INSERT INTO logs (timestamp, event_type, details) VALUES (?, 'PANIC', ?)", (timestamp,payload,))
		client.publish(MQTT_ALERT_TOPIC, "PANIC")
	elif msg.topic == "shesecure/alcohol":
		c.execute("INSERT INTO logs (timestamp, event_type, details) VALUES (?, 'ALCOHOL', ?)", (timestamp,payload,))
		client.publish(MQTT_ALERT_TOPIC, "ALCOHOL")
	elif msg.topic == "shesecure/noise":
		c.execute("INSERT INTO logs (timestamp, event_type, details) VALUES (?, 'NOISE', ?)", (timestamp,payload,))
		client.publish(MQTT_ALERT_TOPIC, "NOISE")
	
	conn.commit()
	conn.close()

# ================== MQTT Client Setup ====-------------

def start_mqtt():
	client.on_connect = on_connect
	client.on_message = on_message
	client.connect(MQTT_BROKER, MQTT_PORT, 60)
	client.loop_start()

# ================== Flask Routes ==================

@app.route('/')
def dashboard():
	conn = sqlite3.connect('database.db')
	c = conn.cursor()
	c.execute("SELECT * FROM logs ORDER BY id DESC LIMIT 10")
	logs = c.fetchall()
	conn.close()
	return render_template('dashboard.html', logs=logs, male_count=male_count, female_count=female_count)

@app.route('/send_custom_message', methods=['POST'])
def send_custom_message():
	message = request.form['message']
	if message:
		client.publish(MQTT_LCD_TOPIC, message)
	return redirect('/')

@app.route('/clear_custom_message')
def clear_custom_message():
	client.publish(MQTT_LCD_TOPIC, "CLEAR")
	return redirect('/')

@app.route('/reset_counts')
def reset_counts():
	global male_count, female_count
	male_count = 0
	female_count = 0
	return redirect('/')
	
@app.route('/clear_logs')
def clear_logs():
	conn=sqlite3.connect('database.db')
	c=conn.cursor()
	c.execute("DELETE FROM logs")
	conn.commit()
	conn.close()
	
	with de_duplock:
		last_events.clear()
	print('All logs cleared','info')
	return redirect('/')

# ================== Main ====--------------

if __name__ == "__main__":
	init_db()
	mqtt_thread = threading.Thread(target=start_mqtt)
	mqtt_thread.start()
	app.run(host='0.0.0.0', port=5000, debug=True)
