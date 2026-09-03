# ติดตั้งบน Arduino IDE

โฟลเดอร์นี้คือไลบรารีในรูปแบบที่ Arduino IDE ใช้ได้โดยตรง

```
ArduinoIDE/
├── Massmore_SHT4x.zip        <- ไฟล์สำหรับ "Add .ZIP Library" (ง่ายที่สุด)
└── Massmore_SHT4x/           <- โฟลเดอร์ไลบรารีตัวจริง
    ├── library.properties
    ├── keywords.txt
    ├── CHANGELOG.md
    ├── LICENSE
    ├── src/
    │   ├── Massmore_SHT4x.h
    │   ├── Massmore_SHT4x.cpp
    │   └── Massmore_SHT4x_Registers.h
    └── examples/             <- 11 ตัวอย่าง
```

---

## สิ่งที่ต้องมีก่อน

| รายการ | เวอร์ชัน |
|---|---|
| Arduino IDE | 2.x (หรือ 1.8.19 ก็ใช้ได้) |
| ESP32 board package | **3.x** ขึ้นไป (ทดสอบกับ 3.3.x) |

ติดตั้ง board package ESP32 ถ้ายังไม่มี

1. **File → Preferences → Additional boards manager URLs** ใส่
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. **Tools → Board → Boards Manager** ค้นคำว่า `esp32` แล้วติดตั้งของ Espressif Systems
   เลือกเวอร์ชัน 3.x

---

## วิธีที่ 1 — ติดตั้งจากไฟล์ .zip (แนะนำ)

1. ดาวน์โหลด `Massmore_SHT4x.zip` จากโฟลเดอร์นี้
2. เปิด Arduino IDE แล้วไปที่ **Sketch → Include Library → Add .ZIP Library...**
3. เลือกไฟล์ที่ดาวน์โหลดมา
4. ปิดแล้วเปิด Arduino IDE ใหม่หนึ่งครั้ง

## วิธีที่ 2 — คัดลอกโฟลเดอร์เอง

คัดลอกโฟลเดอร์ `Massmore_SHT4x/` ทั้งโฟลเดอร์ไปวางที่

| ระบบ | ตำแหน่ง |
|---|---|
| macOS | `~/Documents/Arduino/libraries/` |
| Windows | `Documents\Arduino\libraries\` |
| Linux | `~/Arduino/libraries/` |

แล้วเปิด Arduino IDE ใหม่

---

## เปิดตัวอย่าง

**File → Examples → Massmore_SHT4x → 01_BasicReading**

ตั้งค่าก่อนกด Upload

| หัวข้อ | ค่า |
|---|---|
| Board | ESP32 Dev Module (หรือบอร์ดที่ใช้จริง) |
| Upload Speed | 512000 (ถ้าไม่ผ่านให้ลด 460800 หรือ 115200) |
| Port | พอร์ตของบอร์ด |
| Serial Monitor | 115200 baud |

---

## ตัวอย่างทั้งหมด

| ตัวอย่าง | เนื้อหา |
|---|---|
| `01_BasicReading` | อ่านอุณหภูมิและความชื้นแบบสั้นที่สุด |
| `02_Precision_Timing` | เทียบความละเอียดสามระดับ ทั้งเวลาและ noise |
| `03_Heater_AllModes` | ฮีตเตอร์ครบทั้ง 6 โหมด พร้อมเรื่อง duty cycle |
| `04_SerialNumber_Genuine` | อ่านซีเรียลจากโรงงาน และตรวจว่าเป็นชิปแท้ |
| `05_DewPoint_Comfort` | จุดน้ำค้าง ความชื้นสัมบูรณ์ ดัชนีความร้อน |
| `06_NonBlocking` | อ่านค่าโดยไม่หยุดโปรแกรม |
| `07_MultipleSensors` | ใช้หลายตัวพร้อมกัน ต่างบัส I2C |
| `08_LowPower_DeepSleep` | วัดแล้วหลับ สำหรับงานแบตเตอรี่ |
| `09_ErrorHandling` | จับข้อผิดพลาดและกู้คืนอัตโนมัติ |
| `10_DataLogger_Advanced` | ตัวกรอง สถิติ สอบเทียบ และเอาต์พุต CSV |
| `11_FactoryTest` | ชุดทดสอบโรงงาน 2 ด่าน + 24 หัวข้อ |

---

## แก้ปัญหาที่พบบ่อย

<details>
<summary><b>คอมไพล์แล้วขึ้น 'Wire' was not declared</b></summary>

เลือกบอร์ดผิดตระกูล ตรวจว่าเลือก ESP32 หรือบอร์ดที่มี I2C ในตัวจริง ๆ

</details>

<details>
<summary><b>ตัวอักษรไทยใน Serial Monitor เป็นตัวมั่ว</b></summary>

Arduino IDE 2.x รองรับ UTF-8 อยู่แล้ว ให้ตรวจว่าตั้ง baud rate เป็น 115200 ให้ถูก
ถ้าใช้โปรแกรมอื่นเช่น PuTTY ให้ตั้ง encoding เป็น UTF-8

</details>

<details>
<summary><b>อัปโหลดไม่ผ่าน Timed out waiting for packet header</b></summary>

ลด Upload Speed ลงเป็น 460800 หรือ 115200 แล้วลองใหม่
ถ้ายังไม่ได้ให้กดปุ่ม BOOT ค้างตอนขึ้นข้อความ Connecting

</details>

<details>
<summary><b>ไลบรารีชนกับ Adafruit_SHT4X หรือ SensirionI2CSht4x</b></summary>

ไม่ชนกัน ทุกอย่างในไลบรารีนี้ขึ้นต้นด้วย `Massmore_` หรือ `MASSMORE_SHT4X_`
ติดตั้งพร้อมกันได้ และใช้ในสเก็ตช์เดียวกันก็ได้

</details>

---

← [กลับไปหน้าหลักของไลบรารี](../README.md)
