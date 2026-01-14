import board
import displayio
import terminalio
from adafruit_display_text import label
from adafruit_ble import BLERadio
from adafruit_ble.advertising.standard import Advertisement
import time

display = board.DISPLAY
ble = BLERadio()

SCAN_TIME = 15
MIN_SEEN_TIME = 8
rssi_razb = 15
skimmer_names = ["HC-05", "HC-06", "HM-10", "DX-BT18", "HC-03"]

def update_display(text, color=0x00FF00, scale=2, x=10, y=70):
    group = displayio.Group()
    text_area = label.Label(
        terminalio.FONT,
        text=text,
        color=color,
        scale=scale,
        x=x,
        y=y
    )
    group.append(text_area)
    display.root_group = group

update_display("СКАНИРОВАНИЕ\nОЖИДАИТЕ", color=0x00FFDE, scale=3, x=20, y=50)
time.sleep(3)

devices = {}
founded = False

for adv in ble.start_scan(Advertisement, timeout=SCAN_TIME):
    address = str(adv.address)
    name = adv.complete_name or "Unknown"
    rssi = adv.rssi
    now = time.monotonic()
    clean_name = name.rstrip('\x00').strip().upper()
    if any(skimmer.upper() in clean_name for skimmer in skimmer_names):
        founded = True
        break 
    
    if address not in devices:
        devices[address] = {
            "name": name,
            "rssi": [],
            "first_seen": now,
            "last_seen": now
        }
    devices[address]["rssi"].append(rssi)
    devices[address]["last_seen"] = now

if not founded:
    for data in devices.values():
        duration = data["last_seen"] - data["first_seen"]
        rssi_values = data["rssi"]
        if len(rssi_values) < 3:
            continue
        rssi_var = max(rssi_values) - min(rssi_values)
        avg_rssi = sum(rssi_values) / len(rssi_values)
        
        if (
            data["name"] == "Unknown"
            and duration >= MIN_SEEN_TIME
            and rssi_var <= rssi_razb
            and avg_rssi > -65
        ):
            founded = True
            break

if founded:
    update_display("ОПАСНОСТЬ!\nВОЗМОЖЕН\nСКИММЕР", color=0xC91616, scale=3, x=10, y=20)
else:
    update_display("ВСЕ ОК!", color=0x22F322, scale=4, x=20, y=40)

print(devices)
print(founded)
while True:
    time.sleep(1)