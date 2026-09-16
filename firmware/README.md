# Massmore_SHT4x — Factory Test Firmware

เฟิร์มแวร์สำหรับตรวจบอร์ด **Massmore SHT4X (SKU-1022)** ก่อนส่งลูกค้า (Outgoing QA/QC)
Source: [`ArduinoIDE/Massmore_SHT4x/examples/05_Factory_Test`](../ArduinoIDE/Massmore_SHT4x/examples/05_Factory_Test/05_Factory_Test.ino)

## Files

| File | Target | Flash offset |
|---|---|---|
| `bin/Massmore_SHT4x_FactoryTest_ESP32.bin` | ESP32 Classic (esp32dev) — merged (bootloader + partitions + app) | `0x0` |
| `bin/Massmore_SHT4x_FactoryTest_ESP32_app.bin` | ESP32 Classic — app only | `0x10000` |
| `bin/Massmore_SHT4x_FactoryTest_ESP32S3.bin` | ESP32-S3 (esp32-s3-devkitc-1) — merged | `0x0` |
| `bin/Massmore_SHT4x_FactoryTest_ESP32S3_app.bin` | ESP32-S3 — app only | `0x10000` |
| `bin/Massmore_SHT4x_FactoryTest_NANO.hex` | Arduino Nano (ATmega328P) | — (avrdude) |

## Wiring (Primary test MCU: ESP32 Classic)

| SHT4X pin | ESP32 | ESP32-S3 | Arduino Nano |
|---|---|---|---|
| VIN | 3V3 | 3V3 | 5V |
| GND | GND | GND | GND |
| SDA | GPIO 21 | GPIO 8 | A4 |
| SCL | GPIO 22 | GPIO 9 | A5 |
| 3Vo | — (output, ไม่ต้องต่อ) | — | — |

ใช้สาย Qwiic ต่อจาก ESP32 breakout ที่มี Qwiic ได้โดยตรง

## Flashing

**PlatformIO (แนะนำ)**

```bash
cd PlatformIO
pio run -e esp32dev -t upload      # หรือ -e esp32-s3-devkitc-1 / -e nano
pio device monitor -b 115200
```

**esptool (ESP32, merged binary)**

```bash
pip install esptool
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 921600 \
  write_flash 0x0 firmware/bin/Massmore_SHT4x_FactoryTest_ESP32.bin
```

**esptool (ESP32-S3, merged binary)**

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem-XXXX --baud 921600 \
  write_flash 0x0 firmware/bin/Massmore_SHT4x_FactoryTest_ESP32S3.bin
```

**avrdude (Arduino Nano, new bootloader)**

```bash
avrdude -c arduino -p atmega328p -P /dev/cu.usbserial-XXXX -b 115200 \
  -U flash:w:firmware/bin/Massmore_SHT4x_FactoryTest_NANO.hex:i
```

**Web flasher**: เปิด Massmore Web Serial Monitor เลือกไฟล์ merged `.bin` แล้วกด Flash (ESP Web Tools, offset 0x0)

## Test sequence

| # | `#RESULT` name | Pass criteria |
|---|---|---|
| 1 | `BUS_SCAN` | พบ ACK ที่ 0x44 |
| 2 | `CHIP_ID` | SHT4x ไม่มี CHIP_ID register — ใช้ Serial Number (คำสั่ง 0x89) CRC ถูก, ค่าไม่ใช่ 0 / 0xFFFFFFFF |
| 3 | `SERIAL` | Serial Number 32-bit (พิมพ์เพื่อบันทึก lot) |
| 4 | `AUTHENTICITY` | `isGenuine()` ผ่านครบ 9 ข้อ: I2C_ACK, SERIAL_CRC, SERIAL_SANE, SERIAL_STABLE, SOFT_RESET, MEAS_CRC, MEAS_RANGE, NACK_WHILE_BUSY, LOW_RES_FASTER |
| 5 | `RANGE_TEMP` | −40 < T < 125 °C |
| 6 | `RANGE_HUMI` | 0 ≤ RH ≤ 100 %RH |
| 7 | `CONTINUOUS` | 20/20 ตัวอย่างอ่านได้, noise peak-to-peak ≤ 1.0 °C และ ≤ 3.0 %RH |
| 8 | `HEATER` | ยิง 200 mW 1 s แล้วอุณหภูมิขึ้น ≥ 0.3 °C (พิสูจน์ว่า sensing element ทำงานจริง) |

รอบทดสอบใช้เวลาประมาณ 3 วินาที พิมพ์ `r` + Enter เพื่อทดสอบซ้ำโดยไม่ต้อง reset

## Expected report

<!-- TODO: [MASSMORE_INPUT_REQUIRED: real Factory Test report from a passing board — replace the block below after hardware test] -->

```text
#MASSMORE_FACTORY_TEST v1.0
#PRODUCT Massmore_SHT4x
#MCU ESP32
Library 2.0.0
#RESULT BUS_SCAN PASS 0x44
#RESULT CHIP_ID PASS 0x........
#RESULT SERIAL PASS 0x........
  check 1 I2C_ACK ok
  check 2 SERIAL_CRC ok
  check 3 SERIAL_SANE ok
  check 4 SERIAL_STABLE ok
  check 5 SOFT_RESET ok
  check 6 MEAS_CRC ok
  check 7 MEAS_RANGE ok
  check 8 NACK_WHILE_BUSY ok
  check 9 LOW_RES_FASTER ok
#RESULT AUTHENTICITY PASS GENUINE
#RESULT RANGE_TEMP PASS 27.85
#RESULT RANGE_HUMI PASS 58.20
  noise p-p  T=0.05 C  RH=0.30 %
#RESULT CONTINUOUS PASS 20/20
#RESULT HEATER PASS 3.12
#VERDICT PASS
[PASS] SENSOR QA PASSED - READY TO SHIP
```

Failure example:

```text
#RESULT BUS_SCAN FAIL NONE
#VERDICT FAIL NO_DEVICE
[FAIL] QA CHECK FAILED: NO_DEVICE
```

`<REASON>` values: `NO_DEVICE`, `UNEXPECTED_ADDR`, `CHIP_ID_MISMATCH`, `AUTHENTICITY_SUSPECT`, `TEMP_OUT_OF_RANGE`, `HUMI_OUT_OF_RANGE`, `READ_DROPOUT`, `NOISE_TOO_HIGH`, `HEATER_NO_RESPONSE`

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `BUS_SCAN FAIL NONE` | สายหลวม, สลับ SDA/SCL, ไม่มีไฟ VIN, ต่อไฟเข้า 3Vo แทน VIN |
| `CHIP_ID FAIL CRC_FAIL` | สาย I2C ยาวเกิน / noise — ลด clock เป็น 100 kHz, ใช้สายสั้นลง |
| `AUTHENTICITY FAIL SUSPECT` | ดูบรรทัด `check n ... FAIL`: `NACK_WHILE_BUSY` ตก = ชิปตอบทันทีโดยไม่วัด (clone) |
| `CONTINUOUS FAIL READ_DROPOUT` | ไฟตก / contact ไม่ดี |
| `HEATER FAIL` | sensing element ไม่ตอบสนอง หรือมีลมแรงพัดผ่านเซ็นเซอร์ตอนทดสอบ |

## Build info

| Env | Platform | Core |
|---|---|---|
| esp32dev | pioarduino platform-espressif32 55.03.311 | Arduino-ESP32 3.3.11 |
| esp32-s3-devkitc-1 | pioarduino platform-espressif32 55.03.311 | Arduino-ESP32 3.3.11 |
| nano | atmelavr 5.3.0 | Arduino AVR core |

Rebuild:

```bash
cd PlatformIO && pio run -e esp32dev -e esp32-s3-devkitc-1 -e nano
```
