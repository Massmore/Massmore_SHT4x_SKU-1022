# ใช้งานบน VS Code + PlatformIO

โฟลเดอร์นี้เป็นโปรเจกต์ PlatformIO ที่พร้อม build ทันที เปิดแล้วกด Upload ได้เลย

```
PlatformIO/
├── platformio.ini            <- pin platform ไว้ให้ได้ ESP32 core 3.x
├── src/
│   ├── main.cpp              <- ไฟล์ที่จะถูก build (เริ่มต้นเป็นตัวอย่างที่ 01)
│   └── idf_component.yml
├── lib/Massmore_SHT4x/       <- ไลบรารี (ซอร์สชุดเดียวกับฝั่ง ArduinoIDE)
├── examples/                 <- 11 ตัวอย่างในรูปแบบ main.cpp
└── test/                     <- ชุดทดสอบที่รันบนเครื่อง PC ได้เลย
```

---

## เริ่มใช้งาน

1. ติดตั้งส่วนขยาย **PlatformIO IDE** ใน VS Code
2. **File → Open Folder** แล้วเลือกโฟลเดอร์ `PlatformIO/` นี้ (ไม่ใช่โฟลเดอร์แม่)
3. รอ PlatformIO ดาวน์โหลด toolchain รอบแรก (ใช้เวลาสองสามนาที)
4. เสียบบอร์ด แล้วกดปุ่ม **Upload** (ลูกศรขวาที่แถบล่าง)
5. กดปุ่ม **Serial Monitor** (ปลั๊กที่แถบล่าง) ตั้งไว้ที่ 115200 อยู่แล้ว

---

## เปลี่ยนไปใช้ตัวอย่างอื่น

คัดลอกไฟล์ทับ `src/main.cpp` แล้วกด Upload

```bash
cp examples/04_SerialNumber_Genuine/main.cpp src/main.cpp
```

หรือใน VS Code เปิดไฟล์ในโฟลเดอร์ `examples/` แล้วคัดลอกเนื้อหาไปวางใน `src/main.cpp`

---

## บอร์ดที่เตรียม env ไว้ให้แล้ว

| env | บอร์ด |
|---|---|
| `esp32dev` | ESP32-WROOM-32 / DevKit v1 (ค่าเริ่มต้น) |
| `esp32-s3-devkitc-1` | ESP32-S3 |
| `esp32-s2-saola-1` | ESP32-S2 |
| `esp32-c3-devkitm-1` | ESP32-C3 |
| `esp32-c6-devkitc-1` | ESP32-C6 |

เลือก env ได้จากแถบล่างของ VS Code หรือสั่งจาก terminal

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

> **ขา I2C ของแต่ละบอร์ดไม่เหมือนกัน** ตัวอย่างทั้งหมดตั้งไว้ที่ SDA=21 SCL=22
> ซึ่งเป็นค่าของ ESP32 DevKit ถ้าใช้ S3 / C3 / C6 ให้แก้เลขขาในไฟล์ตัวอย่าง
> หรือส่ง `-1, -1` ให้ `begin()` เพื่อใช้ขาปริยายของบอร์ดนั้น

---

## เรื่องเวอร์ชัน ESP32 core

`platformio.ini` ชี้ไปที่ **pioarduino** fork แบบ pin เวอร์ชันตายตัว

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
```

- `55.03.311` = Arduino ESP32 core **3.3.11** ซึ่งเป็นสาย 3.x เหมือน Arduino IDE รุ่นล่าสุด
- platform `espressif32` ตัวทางการยังส่ง core 2.0.x อยู่ จึงไม่ใช้
- การ pin ไว้ทำให้ build ซ้ำได้ผลเหมือนเดิมเสมอ ไม่มีวันพังเพราะ upstream เปลี่ยน

อยากได้ core ใหม่กว่านี้ ให้เปลี่ยนเลขเวอร์ชันใน URL เป็น release ล่าสุดของ pioarduino

---

## ชุดทดสอบบนเครื่อง PC

ทดสอบตรรกะของไลบรารีได้โดยไม่ต้องมีบอร์ด ใช้แค่ `g++` กับ `make`

```bash
cd test
make
```

ผลที่ได้

```
==========================================================
  ชุดทดสอบไลบรารี Massmore_SHT4x 1.0.0
  รันบนเครื่อง PC ด้วยชิป SHT4x จำลอง ไม่ต้องมีบอร์ด
==========================================================
...
  ผ่าน 314 ข้อ   ไม่ผ่าน 0 ข้อ
==========================================================
```

ชุดทดสอบคอมไพล์ `Massmore_SHT4x.cpp` ตัวเดียวกับที่ลงบอร์ดจริง โดยแทน `Wire`
ด้วยชิป SHT4x จำลองที่ทำตาม datasheet รวมถึงเรื่องที่สำคัญที่สุดคือ
"ยังวัดไม่เสร็จแล้วถูกอ่าน ต้อง NACK" ชิปจำลองมีธงให้จำลอง "ของปลอม" ได้ด้วย
เพื่อทดสอบว่า `verifyChip()` จับได้จริง

---

## build เฟิร์มแวร์ Factory Test เอง

```bash
cp examples/11_FactoryTest/main.cpp src/main.cpp
pio run -e esp32dev
# ได้ไฟล์ที่ .pio/build/esp32dev/firmware.factory.bin
```

หรือใช้ `.bin` ที่ build ไว้แล้วใน [`../firmware/`](../firmware)

---

← [กลับไปหน้าหลักของไลบรารี](../README.md)
