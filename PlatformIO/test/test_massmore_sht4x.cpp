/*!
 * @file test_massmore_sht4x.cpp
 * @brief ชุดทดสอบไลบรารี Massmore_SHT4x ที่รันบนเครื่อง PC ได้เลย
 *
 *     cd PlatformIO/test && make
 *
 * ทดสอบไฟล์ Massmore_SHT4x.cpp ตัวเดียวกับที่คอมไพล์ลงบอร์ดจริง
 * โดยแทนที่ Wire ด้วยชิป SHT4x จำลองที่ทำตาม datasheet
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "massmore_sht4x_host_shim.h"

#include "Massmore_SHT4x.h"

#include <stdio.h>
#include <stdlib.h>

static int g_pass = 0;
static int g_fail = 0;
static const char *g_group = "";

static void group(const char *name) {
  g_group = name;
  printf("\n--- %s ---\n", name);
}

static void check(bool condition, const char *what) {
  if (condition) {
    g_pass++;
  } else {
    g_fail++;
    printf("  [FAIL] %s :: %s\n", g_group, what);
  }
}

static void checkNear(float actual, float expected, float tolerance,
                      const char *what) {
  float diff = actual - expected;
  if (diff < 0) {
    diff = -diff;
  }
  if (diff <= tolerance) {
    g_pass++;
  } else {
    g_fail++;
    printf("  [FAIL] %s :: %s (ได้ %.4f คาดว่า %.4f)\n", g_group, what,
           (double)actual, (double)expected);
  }
}

/* ========================================================================= */
/* 1. CRC-8                                                                  */
/* ========================================================================= */

static void testCrc() {
  group("CRC-8");

  /* เวกเตอร์ทดสอบจาก datasheet: CRC(0xBEEF) = 0x92 */
  uint8_t beef[2] = {0xBE, 0xEF};
  check(MassmoreSHT4x::crc8(beef, 2) == 0x92, "CRC(0xBEEF) = 0x92");

  uint8_t zero[2] = {0x00, 0x00};
  check(MassmoreSHT4x::crc8(zero, 2) == 0x81, "CRC(0x0000) = 0x81");

  uint8_t ff[2] = {0xFF, 0xFF};
  check(MassmoreSHT4x::crc8(ff, 2) == 0xAC, "CRC(0xFFFF) = 0xAC");

  /* CRC ต้องเปลี่ยนเมื่อข้อมูลเปลี่ยนแม้แต่บิตเดียว */
  uint8_t a[2] = {0x12, 0x34};
  uint8_t b[2] = {0x12, 0x35};
  check(MassmoreSHT4x::crc8(a, 2) != MassmoreSHT4x::crc8(b, 2),
        "ข้อมูลต่างกัน 1 บิต ได้ CRC ต่างกัน");

  /* ค่า CRC ต้องตรงกับที่ชิปจำลองคำนวณ */
  for (int i = 0; i < 256; i++) {
    uint8_t data[2] = {(uint8_t)i, (uint8_t)(255 - i)};
    if (MassmoreSHT4x::crc8(data, 2) != mockCrc8(data, 2)) {
      check(false, "CRC ตรงกับฝั่งชิปจำลองทุกค่า");
      return;
    }
  }
  check(true, "CRC ตรงกับฝั่งชิปจำลองทั้ง 256 ชุด");
}

/* ========================================================================= */
/* 2. สูตรแปลงค่า                                                             */
/* ========================================================================= */

static void testConversion() {
  group("สูตรแปลงค่า");

  /* ขอบล่างและขอบบนตาม datasheet */
  checkNear(MassmoreSHT4x::rawToCelsius(0), -45.0f, 0.001f, "raw 0 = -45 C");
  checkNear(MassmoreSHT4x::rawToCelsius(65535), 130.0f, 0.001f, "raw 65535 = 130 C");
  checkNear(MassmoreSHT4x::rawToFahrenheit(0), -49.0f, 0.001f, "raw 0 = -49 F");
  checkNear(MassmoreSHT4x::rawToFahrenheit(65535), 266.0f, 0.001f, "raw 65535 = 266 F");
  checkNear(MassmoreSHT4x::rawToHumidity(0), -6.0f, 0.001f, "raw 0 = -6 %RH");
  checkNear(MassmoreSHT4x::rawToHumidity(65535), 119.0f, 0.001f, "raw 65535 = 119 %RH");

  /* ต้องหารด้วย 65535 ไม่ใช่ 65536 */
  checkNear(MassmoreSHT4x::rawToCelsius(32768), 42.5013f, 0.002f, "กึ่งกลางสเกล = 42.5 C");
  checkNear(MassmoreSHT4x::rawToHumidity(32768), 56.5009f, 0.002f,
            "กึ่งกลางสเกล = 56.5 %RH");

  /* เซลเซียสกับฟาเรนไฮต์ต้องสอดคล้องกัน */
  for (uint32_t raw = 0; raw <= 65535; raw += 4095) {
    float c = MassmoreSHT4x::rawToCelsius((uint16_t)raw);
    float f = MassmoreSHT4x::rawToFahrenheit((uint16_t)raw);
    checkNear(MassmoreSHT4x::celsiusToFahrenheit(c), f, 0.01f,
              "C กับ F ของ raw เดียวกันตรงกัน");
  }

  /* แปลงกลับไปกลับมาต้องได้ค่าเดิม */
  for (uint32_t raw = 0; raw <= 65535; raw += 8191) {
    float c = MassmoreSHT4x::rawToCelsius((uint16_t)raw);
    check(MassmoreSHT4x::celsiusToRaw(c) == (uint16_t)raw, "แปลง C กลับเป็น raw ได้เท่าเดิม");
    float h = MassmoreSHT4x::rawToHumidity((uint16_t)raw);
    check(MassmoreSHT4x::humidityToRaw(h) == (uint16_t)raw, "แปลง RH กลับเป็น raw ได้เท่าเดิม");
  }

  /* ค่าที่เกินขอบต้องถูกหนีบไว้ ไม่วน */
  check(MassmoreSHT4x::celsiusToRaw(-100.0f) == 0, "อุณหภูมิต่ำกว่าช่วง หนีบเป็น 0");
  check(MassmoreSHT4x::celsiusToRaw(500.0f) == 65535, "อุณหภูมิสูงกว่าช่วง หนีบเป็น 65535");
  check(MassmoreSHT4x::humidityToRaw(-50.0f) == 0, "ความชื้นต่ำกว่าช่วง หนีบเป็น 0");
  check(MassmoreSHT4x::humidityToRaw(200.0f) == 65535, "ความชื้นสูงกว่าช่วง หนีบเป็น 65535");

  checkNear(MassmoreSHT4x::fahrenheitToCelsius(32.0f), 0.0f, 0.001f, "32 F = 0 C");
  checkNear(MassmoreSHT4x::fahrenheitToCelsius(212.0f), 100.0f, 0.001f, "212 F = 100 C");
}

/* ========================================================================= */
/* 3. ตารางคำสั่งและเวลา                                                       */
/* ========================================================================= */

static void testCommandTable() {
  group("ตารางคำสั่งและเวลา");

  check(MassmoreSHT4x::precisionCommand(MASSMORE_SHT4X_PRECISION_HIGH) == 0xFD,
        "ความละเอียดสูง = 0xFD");
  check(MassmoreSHT4x::precisionCommand(MASSMORE_SHT4X_PRECISION_MEDIUM) == 0xF6,
        "ความละเอียดกลาง = 0xF6");
  check(MassmoreSHT4x::precisionCommand(MASSMORE_SHT4X_PRECISION_LOW) == 0xE0,
        "ความละเอียดต่ำ = 0xE0");

  check(MASSMORE_SHT4X_CMD_READ_SERIAL == 0x89, "อ่านซีเรียล = 0x89");
  check(MASSMORE_SHT4X_CMD_SOFT_RESET == 0x94, "soft reset = 0x94");

  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_200MW_1S) == 0x39,
        "ฮีตเตอร์ 200mW 1s = 0x39");
  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_200MW_0S1) == 0x32,
        "ฮีตเตอร์ 200mW 0.1s = 0x32");
  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_110MW_1S) == 0x2F,
        "ฮีตเตอร์ 110mW 1s = 0x2F");
  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_110MW_0S1) == 0x24,
        "ฮีตเตอร์ 110mW 0.1s = 0x24");
  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_20MW_1S) == 0x1E,
        "ฮีตเตอร์ 20mW 1s = 0x1E");
  check(MassmoreSHT4x::heaterCommand(MASSMORE_SHT4X_HEATER_20MW_0S1) == 0x15,
        "ฮีตเตอร์ 20mW 0.1s = 0x15");

  check(MassmoreSHT4x::heaterPowerMilliwatt(MASSMORE_SHT4X_HEATER_200MW_1S) == 200,
        "กำลังฮีตเตอร์ 200 mW");
  check(MassmoreSHT4x::heaterPowerMilliwatt(MASSMORE_SHT4X_HEATER_110MW_0S1) == 110,
        "กำลังฮีตเตอร์ 110 mW");
  check(MassmoreSHT4x::heaterPowerMilliwatt(MASSMORE_SHT4X_HEATER_20MW_1S) == 20,
        "กำลังฮีตเตอร์ 20 mW");

  check(MassmoreSHT4x::heaterDurationMs(MASSMORE_SHT4X_HEATER_200MW_1S) == 1100,
        "พัลส์ยาวรอ 1100 ms");
  check(MassmoreSHT4x::heaterDurationMs(MASSMORE_SHT4X_HEATER_200MW_0S1) == 120,
        "พัลส์สั้นรอ 120 ms");

  /* เวลารอต้องมากกว่าค่าสูงสุดใน datasheet */
  check(MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_HIGH) >= 9,
        "รอความละเอียดสูงมากกว่า 8.3 ms");
  check(MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_MEDIUM) >= 5,
        "รอความละเอียดกลางมากกว่า 4.5 ms");
  check(MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_LOW) >= 2,
        "รอความละเอียดต่ำมากกว่า 1.6 ms");

  /* ยิ่งละเอียดยิ่งใช้เวลานาน */
  check(MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_LOW) <
            MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_MEDIUM),
        "ต่ำเร็วกว่ากลาง");
  check(MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_MEDIUM) <
            MassmoreSHT4x::precisionDurationMs(MASSMORE_SHT4X_PRECISION_HIGH),
        "กลางเร็วกว่าสูง");

  check(MASSMORE_SHT4X_I2C_ADDR_A == 0x44, "address รุ่น A = 0x44");
  check(MASSMORE_SHT4X_I2C_ADDR_B == 0x45, "address รุ่น B = 0x45");
  check(MASSMORE_SHT4X_I2C_ADDR_C == 0x46, "address รุ่น C = 0x46");
}

/* ========================================================================= */
/* 4. begin() และการตรวจพารามิเตอร์                                            */
/* ========================================================================= */

static void testBegin() {
  group("begin()");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);

  check(sensor.getMode() == MASSMORE_SHT4X_MODE_IDLE, "ก่อน begin() อยู่โหมด IDLE");
  check(!sensor.measure(nullptr, nullptr), "วัดก่อน begin() ต้องไม่สำเร็จ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_NOT_BEGUN, "ได้รหัส ERR_NOT_BEGUN");

  check(sensor.begin(0x44, MASSMORE_SHT4X_VARIANT_SHT45, 21, 22), "begin() สำเร็จ");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_READY, "หลัง begin() อยู่โหมด READY");
  check(sensor.getAddress() == 0x44, "จำ address ไว้ถูก");
  check(sensor.getVariant() == MASSMORE_SHT4X_VARIANT_SHT45, "จำรุ่นไว้ถูก");
  check(sensor.getSerialNumber() == 0x0A1B2C3D, "begin() อ่านซีเรียลมาแล้ว");
  /*
   * host test คอมไพล์โดยไม่มี ARDUINO_ARCH_ESP32 ไลบรารีจึงเรียก Wire.begin()
   * แบบไม่ระบุขา ซึ่งถูกต้องแล้วสำหรับบอร์ดที่ขา I2C ตายตัว
   */
  check(Wire.begun(), "เรียก Wire.begin() ให้แล้ว");
  check(Wire.frequency() == MASSMORE_SHT4X_I2C_FREQ_DEFAULT, "ตั้งความถี่บัสถูกต้อง");
  check(sensor.isConnected(), "isConnected() เจอชิป");

  /* address นอกช่วงต้องถูกปฏิเสธ */
  MassmoreSHT4x bad(&Wire);
  check(!bad.begin(0x50), "address 0x50 ถูกปฏิเสธ");
  check(bad.lastError() == MASSMORE_SHT4X_ERR_BAD_ARG, "ได้รหัส ERR_BAD_ARG");
  check(!bad.begin(0x47), "address 0x47 ถูกปฏิเสธ");

  /* address 0x45 และ 0x46 ต้องรับได้ */
  mockResetChip();
  g_mockChip.address = 0x45;
  MassmoreSHT4x b(&Wire);
  check(b.begin(0x45), "begin() ที่ 0x45 สำเร็จ");
  mockResetChip();
  g_mockChip.address = 0x46;
  MassmoreSHT4x c(&Wire);
  check(c.begin(0x46), "begin() ที่ 0x46 สำเร็จ");

  /* ไม่มีชิปบนบัส */
  mockResetChip();
  g_mockChip.present = false;
  MassmoreSHT4x missing(&Wire);
  check(!missing.begin(), "ไม่มีชิปแล้ว begin() ต้องไม่สำเร็จ");
  check(!missing.isConnected(), "isConnected() ตอบ false");
  check(missing.lastError() == MASSMORE_SHT4X_ERR_NO_DEVICE, "ได้รหัส ERR_NO_DEVICE");

  /* beginWithExistingBus */
  mockResetChip();
  MassmoreSHT4x existing(&Wire);
  check(existing.beginWithExistingBus(0x44, MASSMORE_SHT4X_VARIANT_SHT41),
        "beginWithExistingBus() สำเร็จ");
  check(existing.getVariant() == MASSMORE_SHT4X_VARIANT_SHT41, "รุ่นถูกตั้งให้ถูกต้อง");
}

/* ========================================================================= */
/* 5. การวัด                                                                  */
/* ========================================================================= */

static void testMeasure() {
  group("การวัด");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  float t = NAN;
  float h = NAN;
  check(sensor.measure(&t, &h), "measure() สำเร็จ");
  checkNear(t, MassmoreSHT4x::rawToCelsius(0x6666), 0.01f, "อุณหภูมิตรงกับค่าดิบ");
  checkNear(h, MassmoreSHT4x::rawToHumidity(0x8000), 0.01f, "ความชื้นตรงกับค่าดิบ");
  check(g_mockChip.lastCommand == 0xFD, "ค่าเริ่มต้นใช้ความละเอียดสูง");

  /* รับ nullptr ได้ */
  check(sensor.measure(&t, nullptr), "ส่ง nullptr ให้ความชื้นได้");
  check(sensor.measure(nullptr, &h), "ส่ง nullptr ให้อุณหภูมิได้");
  check(sensor.measure(nullptr, nullptr), "ส่ง nullptr ทั้งคู่ได้");

  /* โครงสร้าง reading */
  massmore_sht4x_reading_t reading;
  check(sensor.measure(reading), "measure(reading) สำเร็จ");
  check(reading.rawTemperature == 0x6666, "ค่าดิบอุณหภูมิถูกเก็บไว้");
  check(reading.rawHumidity == 0x8000, "ค่าดิบความชื้นถูกเก็บไว้");
  check(!reading.heated, "ค่านี้ไม่ได้มาจากฮีตเตอร์");
  check(reading.timestampMs == millis(), "timestamp ถูกบันทึก");

  /* ค่าล่าสุดที่เก็บไว้ภายใน */
  checkNear(sensor.getTemperature(), reading.temperature, 0.001f, "getTemperature() ตรงกัน");
  checkNear(sensor.getHumidity(), reading.humidity, 0.001f, "getHumidity() ตรงกัน");
  checkNear(sensor.getTemperatureF(),
            MassmoreSHT4x::celsiusToFahrenheit(reading.temperature), 0.01f,
            "getTemperatureF() ตรงกัน");
  check(sensor.getRawTemperature() == 0x6666, "getRawTemperature() ตรงกัน");
  check(sensor.getRawHumidity() == 0x8000, "getRawHumidity() ตรงกัน");
  check(sensor.getLastUpdateMs() == reading.timestampMs, "getLastUpdateMs() ตรงกัน");
  check(sensor.getLastReading().rawTemperature == 0x6666, "getLastReading() ตรงกัน");

  /* เลือกความละเอียดได้ */
  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_LOW);
  check(sensor.getPrecision() == MASSMORE_SHT4X_PRECISION_LOW, "ตั้งความละเอียดต่ำได้");
  sensor.measure(&t, &h);
  check(g_mockChip.lastCommand == 0xE0, "ส่งคำสั่ง 0xE0 ตามที่ตั้ง");

  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_MEDIUM);
  sensor.measure(&t, &h);
  check(g_mockChip.lastCommand == 0xF6, "ส่งคำสั่ง 0xF6 ตามที่ตั้ง");

  /* measureWith() ไม่เปลี่ยนค่าที่ตั้งไว้ */
  sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, &h);
  check(g_mockChip.lastCommand == 0xFD, "measureWith() ใช้ความละเอียดที่ระบุ");
  check(sensor.getPrecision() == MASSMORE_SHT4X_PRECISION_MEDIUM,
        "measureWith() ไม่ไปแก้ค่าที่ตั้งไว้");

  /* ฟังก์ชันอ่านค่าเดี่ยว */
  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_HIGH);
  check(!isnan(sensor.readTemperature()), "readTemperature() ได้ค่า");
  check(!isnan(sensor.readTemperatureF()), "readTemperatureF() ได้ค่า");
  check(!isnan(sensor.readHumidity()), "readHumidity() ได้ค่า");

  /* CRC ผิดต้องจับได้ */
  g_mockChip.corruptCrc = true;
  check(!sensor.measure(&t, &h), "CRC ผิดแล้ว measure() ต้องไม่สำเร็จ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_CRC, "ได้รหัส ERR_CRC");
  check(isnan(sensor.readTemperature()), "CRC ผิดแล้ว readTemperature() คืน NAN");
  g_mockChip.corruptCrc = false;

  /* ชิปหายไปกลางคัน */
  g_mockChip.present = false;
  check(!sensor.measure(&t, &h), "ชิปหลุดแล้ว measure() ต้องไม่สำเร็จ");
  g_mockChip.present = true;
  check(sensor.measure(&t, &h), "เสียบกลับแล้ววัดได้อีก");
}

/* ========================================================================= */
/* 6. offset และการตัดขอบความชื้น                                              */
/* ========================================================================= */

static void testOffsetAndClipping() {
  group("offset และการตัดขอบ");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  float base = NAN;
  sensor.measure(&base, nullptr);

  sensor.setTemperatureOffset(-3.5f);
  check(sensor.getTemperatureOffset() == -3.5f, "จำ offset อุณหภูมิไว้");
  float shifted = NAN;
  sensor.measure(&shifted, nullptr);
  checkNear(shifted, base - 3.5f, 0.01f, "offset อุณหภูมิถูกนำไปบวกจริง");

  sensor.setTemperatureOffset(0.0f);

  float baseH = NAN;
  sensor.measure(nullptr, &baseH);
  sensor.setHumidityOffset(2.5f);
  check(sensor.getHumidityOffset() == 2.5f, "จำ offset ความชื้นไว้");
  float shiftedH = NAN;
  sensor.measure(nullptr, &shiftedH);
  checkNear(shiftedH, baseH + 2.5f, 0.01f, "offset ความชื้นถูกนำไปบวกจริง");
  sensor.setHumidityOffset(0.0f);

  /* การตัดขอบเปิดไว้เป็นค่าเริ่มต้น */
  check(sensor.getHumidityClipping(), "การตัดขอบความชื้นเปิดไว้ตั้งแต่แรก");

  g_mockChip.rawRH = 0xFFFF; /* = 119 %RH ตามสูตร */
  float high = NAN;
  sensor.measure(nullptr, &high);
  checkNear(high, 100.0f, 0.001f, "ค่าเกิน 100 ถูกตัดเหลือ 100");

  g_mockChip.rawRH = 0x0000; /* = -6 %RH ตามสูตร */
  float low = NAN;
  sensor.measure(nullptr, &low);
  checkNear(low, 0.0f, 0.001f, "ค่าต่ำกว่า 0 ถูกตัดเป็น 0");

  /* ปิดการตัดขอบแล้วต้องได้ค่าดิบตามสูตร */
  sensor.setHumidityClipping(false);
  check(!sensor.getHumidityClipping(), "ปิดการตัดขอบได้");
  sensor.measure(nullptr, &low);
  checkNear(low, -6.0f, 0.01f, "ปิดการตัดขอบแล้วได้ -6 %RH ตามสูตร");
  g_mockChip.rawRH = 0xFFFF;
  sensor.measure(nullptr, &high);
  checkNear(high, 119.0f, 0.01f, "ปิดการตัดขอบแล้วได้ 119 %RH ตามสูตร");

  g_mockChip.rawRH = 0x8000;
  sensor.setHumidityClipping(true);
}

/* ========================================================================= */
/* 7. โหมดไม่บล็อก                                                            */
/* ========================================================================= */

static void testNonBlocking() {
  group("โหมดไม่บล็อก");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();
  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_HIGH);

  check(sensor.startMeasurement(), "startMeasurement() สำเร็จ");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_MEASURING, "เข้าโหมด MEASURING");
  check(!sensor.isMeasurementReady(), "เพิ่งสั่ง ยังไม่พร้อม");

  float t = NAN;
  float h = NAN;
  check(!sensor.readMeasurement(&t, &h), "อ่านก่อนเวลาไม่สำเร็จ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_NOT_READY, "ได้รหัส ERR_NOT_READY");

  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  check(sensor.isMeasurementReady(), "ครบเวลาแล้วพร้อมอ่าน");
  check(sensor.readMeasurement(&t, &h), "readMeasurement() สำเร็จ");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_READY, "กลับสู่โหมด READY");
  checkNear(t, MassmoreSHT4x::rawToCelsius(0x6666), 0.01f, "ค่าที่ได้ถูกต้อง");

  /* อ่านซ้ำโดยไม่สั่งวัดใหม่ต้องไม่ผ่าน */
  check(!sensor.readMeasurement(&t, &h), "อ่านซ้ำโดยไม่สั่งวัดใหม่ไม่สำเร็จ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_WRONG_MODE, "ได้รหัส ERR_WRONG_MODE");

  /* แบบรับเป็นโครงสร้าง */
  sensor.startMeasurement();
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  massmore_sht4x_reading_t reading;
  check(sensor.readMeasurement(reading), "readMeasurement(reading) สำเร็จ");
  check(reading.rawTemperature == 0x6666, "โครงสร้างมีค่าดิบครบ");

  /* ความละเอียดต่ำใช้เวลาน้อยกว่า */
  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_LOW);
  sensor.startMeasurement();
  delay(MASSMORE_SHT4X_MEAS_DURATION_LOW_MS);
  check(sensor.isMeasurementReady(), "ความละเอียดต่ำพร้อมเร็วกว่า");
  check(sensor.readMeasurement(&t, &h), "อ่านผลความละเอียดต่ำได้");
}

/* ========================================================================= */
/* 8. update() และ callback                                                   */
/* ========================================================================= */

static int g_callbackCount = 0;
static float g_callbackTemperature = NAN;

static void onReading(const massmore_sht4x_reading_t &reading) {
  g_callbackCount++;
  g_callbackTemperature = reading.temperature;
}

static void testUpdate() {
  group("update() และ callback");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();
  sensor.setPrecision(MASSMORE_SHT4X_PRECISION_HIGH);
  sensor.setCallback(onReading);
  sensor.setUpdateInterval(1000);
  check(sensor.getUpdateInterval() == 1000, "จำช่วงเวลาที่ตั้งไว้");

  g_callbackCount = 0;

  /* รอบแรก: update() สั่งวัด แล้วรอบถัดมาเก็บผล */
  check(!sensor.update(), "รอบแรกยังไม่มีค่าใหม่ (แค่สั่งวัด)");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_MEASURING, "update() สั่งวัดให้แล้ว");
  check(!sensor.update(), "ยังไม่ครบเวลาก็ยังไม่มีค่า");
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  check(sensor.update(), "ครบเวลาแล้ว update() คืน true");
  check(g_callbackCount == 1, "callback ถูกเรียกหนึ่งครั้ง");
  check(!isnan(g_callbackTemperature), "callback ได้ค่าอุณหภูมิ");

  /* ยังไม่ถึงรอบถัดไป ต้องไม่สั่งวัดซ้ำ */
  check(!sensor.update(), "ยังไม่ถึงรอบถัดไป");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_READY, "ยังอยู่โหมด READY");

  /* ผ่านไปครบ 1 วินาที ต้องสั่งวัดใหม่ */
  delay(1000);
  check(!sensor.update(), "ถึงรอบใหม่ update() สั่งวัด");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_MEASURING, "เข้าโหมด MEASURING อีกครั้ง");
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  check(sensor.update(), "ได้ค่าที่สอง");
  check(g_callbackCount == 2, "callback ถูกเรียกครั้งที่สอง");

  /* ถอด callback ออกแล้วต้องไม่ถูกเรียกอีก */
  sensor.setCallback(nullptr);
  delay(1000);
  sensor.update();
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  sensor.update();
  check(g_callbackCount == 2, "ถอด callback แล้วไม่ถูกเรียกอีก");
}

/* ========================================================================= */
/* 9. ฮีตเตอร์                                                                */
/* ========================================================================= */

static void testHeater() {
  group("ฮีตเตอร์");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();
  sensor.setHeaterDutyGuard(false); /* ทดสอบตัวกันเผลอแยกต่างหาก */

  float cold = NAN;
  sensor.measure(&cold, nullptr);

  massmore_sht4x_reading_t hot;
  check(sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, &hot), "runHeater() สำเร็จ");
  check(g_mockChip.lastCommand == 0x39, "ส่งคำสั่ง 0x39");
  check(hot.heated, "ผลที่ได้ถูกทำเครื่องหมายว่ามาจากฮีตเตอร์");
  check(hot.temperature > cold, "อุณหภูมิตอนร้อนสูงกว่าตอนปกติ");
  check(sensor.getHeaterOnTimeMs() == 1100, "นับเวลาฮีตเตอร์ไว้ 1100 ms");

  /* ทุกโหมดต้องส่งคำสั่งถูกตัว */
  sensor.runHeater(MASSMORE_SHT4X_HEATER_110MW_0S1, nullptr);
  check(g_mockChip.lastCommand == 0x24, "โหมด 110mW 0.1s ส่ง 0x24");
  sensor.runHeater(MASSMORE_SHT4X_HEATER_20MW_1S, nullptr);
  check(g_mockChip.lastCommand == 0x1E, "โหมด 20mW 1s ส่ง 0x1E");

  /* แบบไม่บล็อก */
  check(sensor.startHeater(MASSMORE_SHT4X_HEATER_200MW_0S1), "startHeater() สำเร็จ");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_HEATING, "เข้าโหมด HEATING");
  check(!sensor.isHeaterDone(), "ฮีตเตอร์ยังทำงานอยู่");
  check(!sensor.measure(nullptr, nullptr), "สั่งวัดตอนฮีตเตอร์ทำงานไม่ได้");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_WRONG_MODE, "ได้รหัส ERR_WRONG_MODE");
  delay(MASSMORE_SHT4X_HEATER_SHORT_MS);
  check(sensor.isHeaterDone(), "ครบเวลาแล้วฮีตเตอร์จบ");
  massmore_sht4x_reading_t afterPulse;
  check(sensor.readMeasurement(afterPulse), "อ่านผลหลังยิงฮีตเตอร์ได้");
  check(afterPulse.heated, "ผลถูกทำเครื่องหมายว่ามาจากฮีตเตอร์");

  /* self test */
  float rise = NAN;
  check(sensor.runHeaterSelfTest(MASSMORE_SHT4X_HEATER_200MW_1S, 0.5f, &rise),
        "runHeaterSelfTest() ผ่าน");
  check(rise > 0.5f, "อุณหภูมิขึ้นเกินเกณฑ์");

  /* เซ็นเซอร์ที่ฮีตเตอร์ไม่ทำงาน ต้องตกการทดสอบ */
  g_mockChip.heatBoostRaw = 0;
  check(!sensor.runHeaterSelfTest(MASSMORE_SHT4X_HEATER_200MW_1S, 0.5f, &rise),
        "ฮีตเตอร์ไม่ทำให้ร้อน ต้องไม่ผ่าน");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_OUT_OF_RANGE, "ได้รหัส ERR_OUT_OF_RANGE");
  g_mockChip.heatBoostRaw = 800;

  /* วัดค่าจริงหลังฮีตเตอร์ต้องไม่ติดธง heated */
  massmore_sht4x_reading_t coolReading;
  check(sensor.measureAfterHeating(100, coolReading), "measureAfterHeating() สำเร็จ");
  check(!coolReading.heated, "ค่าหลังเย็นแล้วไม่ติดธงฮีตเตอร์");

  /* removeCondensation ยิงครบจำนวนพัลส์ */
  uint32_t before = g_mockChip.heaterPulses;
  check(sensor.removeCondensation(3, 10), "removeCondensation() สำเร็จ");
  check(g_mockChip.heaterPulses - before == 3, "ยิงครบ 3 พัลส์");
  check(!sensor.removeCondensation(0, 10), "จำนวนพัลส์ 0 ถูกปฏิเสธ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_BAD_ARG, "ได้รหัส ERR_BAD_ARG");
}

/* ========================================================================= */
/* 10. ตัวกันเผลอ duty cycle                                                   */
/* ========================================================================= */

static void testHeaterDutyGuard() {
  group("ตัวกันเผลอ duty cycle");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  check(sensor.getHeaterDutyGuard(), "ตัวกันเผลอเปิดไว้ตั้งแต่แรก");
  check(sensor.getHeaterOnTimeMs() == 0, "ยังไม่เคยใช้ฮีตเตอร์");

  /* พัลส์แรกยิงได้เสมอ */
  check(sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr), "พัลส์แรกยิงได้");

  /* ยิงซ้ำทันทีต้องถูกปฏิเสธ เพราะ duty จะเกิน 10% */
  check(!sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr),
        "ยิงซ้ำทันทีถูกปฏิเสธ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_HEATER_DUTY, "ได้รหัส ERR_HEATER_DUTY");
  check(!sensor.startHeater(MASSMORE_SHT4X_HEATER_200MW_1S),
        "startHeater() ก็ถูกกันเช่นกัน");
  check(sensor.getMode() != MASSMORE_SHT4X_MODE_HEATING, "ไม่ได้เข้าโหมด HEATING");

  /* รอให้ผ่านไปนานพอ duty จะกลับมาต่ำกว่า 10% */
  delay(30000);
  check(sensor.getHeaterDutyPercent() < 10.0f, "duty ลดลงต่ำกว่า 10% หลังรอ");
  check(sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr),
        "รอนานพอแล้วยิงได้อีก");

  /* ปิดตัวกันเผลอแล้วต้องยิงได้ทันที */
  sensor.setHeaterDutyGuard(false);
  check(!sensor.getHeaterDutyGuard(), "ปิดตัวกันเผลอได้");
  check(sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr),
        "ปิดตัวกันเผลอแล้วยิงได้ทันที");

  /* ล้างสถิติ */
  sensor.resetHeaterStats();
  check(sensor.getHeaterOnTimeMs() == 0, "resetHeaterStats() ล้างเวลาสะสม");
  checkNear(sensor.getHeaterDutyPercent(), 0.0f, 0.001f, "duty กลับเป็น 0");
}

/* ========================================================================= */
/* 11. ซีเรียลและ verifyChip()                                                */
/* ========================================================================= */

static void testSerialAndVerify() {
  group("ซีเรียลและ verifyChip()");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  uint32_t serial = 0;
  check(sensor.readSerialNumber(&serial), "readSerialNumber() สำเร็จ");
  check(serial == 0x0A1B2C3D, "ซีเรียลตรงกับที่ชิปจำลองเก็บไว้");
  check(sensor.getSerialNumber() == serial, "getSerialNumber() ตรงกัน");
  check(!sensor.readSerialNumber(nullptr), "ส่ง nullptr ต้องถูกปฏิเสธ");
  check(sensor.lastError() == MASSMORE_SHT4X_ERR_BAD_ARG, "ได้รหัส ERR_BAD_ARG");

  /* ชิปแท้ต้องผ่านครบ 10 ข้อ */
  mockResetChip();
  MassmoreSHT4x genuine(&Wire);
  genuine.begin();
  massmore_sht4x_genuine_t result = genuine.verifyChip();
  if (result != MASSMORE_SHT4X_GENUINE_PASS) {
    printf("      (ผลที่ได้ mask=0x%03X ผ่าน %u ข้อ)\n", genuine.getVerifyMask(),
           (unsigned)genuine.getVerifyPassCount());
  }
  check(result == MASSMORE_SHT4X_GENUINE_PASS, "ชิปแท้ได้ผล PASS");
  check(genuine.getVerifyPassCount() == 10, "ผ่านครบ 10 ข้อ");
  check(genuine.getVerifyMask() == MASSMORE_SHT4X_CHK_ALL, "บิตครบทุกข้อ");
  check(genuine.getSerialNumber() == 0x0A1B2C3D, "verifyChip() เก็บซีเรียลไว้");

  /* ชิปที่ไม่รู้จักคำสั่งอ่านซีเรียล */
  mockResetChip();
  g_mockChip.supportsSerial = false;
  MassmoreSHT4x noSerial(&Wire);
  noSerial.beginWithExistingBus();
  result = noSerial.verifyChip();
  check((noSerial.getVerifyMask() & MASSMORE_SHT4X_CHK_SERIAL_CRC) == 0,
        "ตกข้อ CRC ของซีเรียล");
  check(result == MASSMORE_SHT4X_GENUINE_NOT_SHT4X, "ไม่มีคำสั่งซีเรียล = ไม่ใช่ SHT4x");

  /* ชิปที่สุ่มซีเรียลใหม่ทุกครั้ง */
  mockResetChip();
  g_mockChip.serialDrifts = true;
  MassmoreSHT4x drifting(&Wire);
  drifting.begin();
  result = drifting.verifyChip();
  check((drifting.getVerifyMask() & MASSMORE_SHT4X_CHK_SERIAL_STABLE) == 0,
        "ตกข้อ อ่านซีเรียลซ้ำได้ค่าเดิม");
  check(result == MASSMORE_SHT4X_GENUINE_PARTIAL, "ตกหนึ่งข้อได้ผล PARTIAL");

  /* ชิปที่ตอบทันทีไม่ยอม NACK ตอนยังวัดไม่เสร็จ */
  mockResetChip();
  g_mockChip.nackWhenBusy = false;
  MassmoreSHT4x noNack(&Wire);
  noNack.begin();
  result = noNack.verifyChip();
  check((noNack.getVerifyMask() & MASSMORE_SHT4X_CHK_NACK_EARLY) == 0,
        "ตกข้อ NACK ตอนยังวัดไม่เสร็จ");

  /* ชิปที่ตอบข้อมูลให้คำสั่งนอกตาราง */
  mockResetChip();
  g_mockChip.answersBogusCommand = true;
  MassmoreSHT4x chatty(&Wire);
  chatty.begin();
  result = chatty.verifyChip();
  check((chatty.getVerifyMask() & MASSMORE_SHT4X_CHK_BOGUS_CMD) == 0,
        "ตกข้อ ปฏิเสธคำสั่งนอกตาราง");

  /* ชิปที่ส่ง CRC ผิดตลอด */
  mockResetChip();
  g_mockChip.corruptCrc = true;
  MassmoreSHT4x badCrc(&Wire);
  badCrc.beginWithExistingBus();
  result = badCrc.verifyChip();
  check(result == MASSMORE_SHT4X_GENUINE_NOT_SHT4X, "CRC ผิดตลอด = ไม่ใช่ SHT4x");

  /* ไม่มีอุปกรณ์เลย */
  mockResetChip();
  g_mockChip.present = false;
  MassmoreSHT4x nothing(&Wire);
  result = nothing.verifyChip();
  check(result == MASSMORE_SHT4X_GENUINE_NOT_SHT4X, "ไม่มีอุปกรณ์ = ไม่ใช่ SHT4x");
  check(nothing.getVerifyPassCount() == 0, "ไม่ผ่านสักข้อ");

  /* ชื่อข้อตรวจต้องมีครบและไม่ว่าง */
  for (uint8_t i = 0; i < MASSMORE_SHT4X_CHK_COUNT; i++) {
    const char *name = MassmoreSHT4x::getVerifyCheckName(i);
    if (name == nullptr || name[0] == '\0') {
      check(false, "ชื่อข้อตรวจครบทุกข้อ");
      break;
    }
  }
  check(true, "ชื่อข้อตรวจครบทั้ง 10 ข้อ");
  check(MassmoreSHT4x::getVerifyCheckName(99) != nullptr, "index เกินช่วงยังคืนข้อความได้");
}

/* ========================================================================= */
/* 12. รีเซ็ต                                                                 */
/* ========================================================================= */

static void testReset() {
  group("รีเซ็ต");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  check(sensor.softReset(), "softReset() สำเร็จ");
  check(g_mockChip.lastCommand == 0x94, "ส่งคำสั่ง 0x94");
  check(sensor.getMode() == MASSMORE_SHT4X_MODE_READY, "หลังรีเซ็ตอยู่โหมด READY");
  check(sensor.measure(nullptr, nullptr), "รีเซ็ตแล้ววัดต่อได้");

  check(sensor.generalCallReset(), "generalCallReset() สำเร็จ");
  check(sensor.measure(nullptr, nullptr), "general call แล้ววัดต่อได้");

  g_mockChip.present = false;
  check(!sensor.softReset(), "ไม่มีชิปแล้ว softReset() ไม่สำเร็จ");
  g_mockChip.present = true;
}

/* ========================================================================= */
/* 13. รุ่นและตัวถัง                                                           */
/* ========================================================================= */

static void testVariant() {
  group("รุ่นและตัวถัง");

  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.begin();

  sensor.setVariant(MASSMORE_SHT4X_VARIANT_SHT40);
  check(strcmp(sensor.getVariantName(), "SHT40") == 0, "ชื่อรุ่น SHT40");
  checkNear(sensor.getTemperatureAccuracy(), 0.2f, 0.001f, "SHT40 แม่นยำ 0.2 องศา");
  checkNear(sensor.getHumidityAccuracy(), 1.8f, 0.001f, "SHT40 แม่นยำ 1.8 %RH");

  sensor.setVariant(MASSMORE_SHT4X_VARIANT_SHT41);
  check(strcmp(sensor.getVariantName(), "SHT41") == 0, "ชื่อรุ่น SHT41");
  checkNear(sensor.getTemperatureAccuracy(), 0.2f, 0.001f, "SHT41 แม่นยำ 0.2 องศา");

  sensor.setVariant(MASSMORE_SHT4X_VARIANT_SHT45);
  check(strcmp(sensor.getVariantName(), "SHT45") == 0, "ชื่อรุ่น SHT45");
  checkNear(sensor.getTemperatureAccuracy(), 0.1f, 0.001f, "SHT45 แม่นยำ 0.1 องศา");
  checkNear(sensor.getHumidityAccuracy(), 1.0f, 0.001f, "SHT45 แม่นยำ 1.0 %RH");

  sensor.setVariant(MASSMORE_SHT4X_VARIANT_AUTO);
  check(strcmp(sensor.getVariantName(), "SHT4x") == 0, "ไม่ระบุรุ่นได้ชื่อ SHT4x");
  checkNear(sensor.getTemperatureAccuracy(), 0.2f, 0.001f, "AUTO ใช้เกณฑ์กว้างสุด");

  /* SHT45 ต้องแม่นกว่า SHT40 เสมอ */
  MassmoreSHT4x a(&Wire);
  MassmoreSHT4x b(&Wire);
  a.setVariant(MASSMORE_SHT4X_VARIANT_SHT40);
  b.setVariant(MASSMORE_SHT4X_VARIANT_SHT45);
  check(b.getTemperatureAccuracy() < a.getTemperatureAccuracy(),
        "SHT45 แม่นกว่า SHT40 ด้านอุณหภูมิ");
  check(b.getHumidityAccuracy() < a.getHumidityAccuracy(),
        "SHT45 แม่นกว่า SHT40 ด้านความชื้น");

  sensor.setPackage(MASSMORE_SHT4X_PACKAGE_F);
  check(sensor.getPackage() == MASSMORE_SHT4X_PACKAGE_F, "จำแบบตัวถังไว้");
  check(strlen(sensor.getPackageName()) > 0, "ชื่อตัวถังไม่ว่าง");
  sensor.setPackage(MASSMORE_SHT4X_PACKAGE_B);
  check(strlen(sensor.getPackageName()) > 0, "ชื่อตัวถัง B ไม่ว่าง");

  check(strcmp(MassmoreSHT4x::precisionToString(MASSMORE_SHT4X_PRECISION_HIGH),
               "HIGH") == 0,
        "ชื่อความละเอียด HIGH");
  check(strcmp(MassmoreSHT4x::precisionToString(MASSMORE_SHT4X_PRECISION_LOW),
               "LOW") == 0,
        "ชื่อความละเอียด LOW");
  check(strlen(MassmoreSHT4x::heaterToString(MASSMORE_SHT4X_HEATER_200MW_1S)) > 0,
        "ชื่อโหมดฮีตเตอร์ไม่ว่าง");
}

/* ========================================================================= */
/* 14. ค่าที่คำนวณต่อ                                                          */
/* ========================================================================= */

static void testDerived() {
  group("ค่าที่คำนวณต่อ");

  /* ที่ 100 %RH จุดน้ำค้างเท่ากับอุณหภูมิอากาศ */
  checkNear(MassmoreSHT4x::dewPoint(25.0f, 100.0f), 25.0f, 0.05f,
            "100 %RH จุดน้ำค้าง = อุณหภูมิ");
  checkNear(MassmoreSHT4x::dewPoint(20.0f, 100.0f), 20.0f, 0.05f,
            "100 %RH ที่ 20 องศาก็เช่นกัน");

  /* ค่าอ้างอิงที่คำนวณมือได้ */
  checkNear(MassmoreSHT4x::dewPoint(25.0f, 50.0f), 13.86f, 0.1f,
            "25 องศา 50 %RH จุดน้ำค้าง 13.9");
  checkNear(MassmoreSHT4x::dewPoint(30.0f, 60.0f), 21.39f, 0.1f,
            "30 องศา 60 %RH จุดน้ำค้าง 21.4");

  /* จุดน้ำค้างต้องไม่เกินอุณหภูมิอากาศ และเพิ่มตามความชื้น */
  float previous = -1000.0f;
  for (float rh = 10.0f; rh <= 100.0f; rh += 10.0f) {
    float dp = MassmoreSHT4x::dewPoint(28.0f, rh);
    check(dp <= 28.05f, "จุดน้ำค้างไม่เกินอุณหภูมิอากาศ");
    check(dp > previous, "ความชื้นมากขึ้น จุดน้ำค้างสูงขึ้น");
    previous = dp;
  }

  check(isnan(MassmoreSHT4x::dewPoint(25.0f, 0.0f)), "ความชื้น 0 คืน NAN");
  check(isnan(MassmoreSHT4x::dewPoint(NAN, 50.0f)), "อุณหภูมิ NAN คืน NAN");

  /* ความดันไออิ่มตัวที่ค่าอ้างอิงมาตรฐาน */
  checkNear(MassmoreSHT4x::saturationVaporPressure(0.0f), 6.112f, 0.01f,
            "0 องศา = 6.112 hPa");
  checkNear(MassmoreSHT4x::saturationVaporPressure(20.0f), 23.4f, 0.3f,
            "20 องศา ประมาณ 23.4 hPa");
  checkNear(MassmoreSHT4x::saturationVaporPressure(100.0f), 1038.0f, 5.0f,
            "100 องศา ใกล้ความดันบรรยากาศ (สูตร Magnus เพี้ยนเล็กน้อยที่ปลายช่วง)");

  /* ความชื้นสัมบูรณ์ */
  checkNear(MassmoreSHT4x::absoluteHumidity(20.0f, 50.0f), 8.65f, 0.2f,
            "20 องศา 50 %RH ประมาณ 8.65 g/m3");
  check(MassmoreSHT4x::absoluteHumidity(30.0f, 50.0f) >
            MassmoreSHT4x::absoluteHumidity(20.0f, 50.0f),
        "อากาศร้อนกว่าอุ้มน้ำได้มากกว่า");
  checkNear(MassmoreSHT4x::absoluteHumidity(25.0f, 0.0f), 0.0f, 0.001f,
            "ความชื้น 0 ได้ 0 g/m3");
  check(isnan(MassmoreSHT4x::absoluteHumidity(NAN, 50.0f)), "อินพุต NAN คืน NAN");

  /* ดัชนีความร้อน */
  float hi = MassmoreSHT4x::heatIndex(35.0f, 70.0f);
  check(hi > 35.0f, "ร้อนชื้นแล้วรู้สึกร้อนกว่าจริง");
  checkNear(MassmoreSHT4x::heatIndex(20.0f, 40.0f), 20.0f, 2.0f,
            "อากาศสบายดัชนีใกล้อุณหภูมิจริง");
  check(isnan(MassmoreSHT4x::heatIndex(NAN, 50.0f)), "อินพุต NAN คืน NAN");
}

/* ========================================================================= */
/* 15. scan() และข้อมูลทั่วไป                                                  */
/* ========================================================================= */

static void testScanAndMisc() {
  group("scan() และข้อมูลทั่วไป");

  mockResetChip();
  uint8_t found[3] = {0, 0, 0};
  check(MassmoreSHT4x::scan(&Wire, found) == 1, "เจอชิปหนึ่งตัว");
  check(found[0] == 0x44, "เจอที่ 0x44");

  g_mockChip.address = 0x46;
  check(MassmoreSHT4x::scan(&Wire, found) == 1, "ย้ายไป 0x46 ก็ยังเจอ");
  check(found[0] == 0x46, "เจอที่ 0x46");

  g_mockChip.present = false;
  check(MassmoreSHT4x::scan(&Wire, found) == 0, "ไม่มีชิปแล้วเจอศูนย์ตัว");
  check(MassmoreSHT4x::scan(nullptr, found) == 0, "ส่ง wire เป็น nullptr คืน 0");
  check(MassmoreSHT4x::scan(&Wire, nullptr) == 0, "ส่ง found เป็น nullptr คืน 0");

  check(strcmp(MassmoreSHT4x::getLibraryVersion(), MASSMORE_SHT4X_VERSION_STRING) == 0,
        "เวอร์ชันไลบรารีตรงกับที่ประกาศไว้");
  check(MASSMORE_SHT4X_VERSION_MAJOR == 1, "เวอร์ชันหลัก 1");

  /* ข้อความของทุกรหัสข้อผิดพลาดต้องมีจริง */
  for (int i = 0; i <= (int)MASSMORE_SHT4X_ERR_HEATER_DUTY; i++) {
    const char *text = MassmoreSHT4x::errorToString((massmore_sht4x_error_t)i);
    if (text == nullptr || text[0] == '\0') {
      check(false, "ข้อความข้อผิดพลาดครบทุกรหัส");
      break;
    }
  }
  check(true, "ข้อความข้อผิดพลาดครบทุกรหัส");
  check(strcmp(MassmoreSHT4x::errorToString(MASSMORE_SHT4X_OK), "ปกติ") == 0,
        "รหัส OK แปลว่าปกติ");
  check(MassmoreSHT4x::errorToString((massmore_sht4x_error_t)999) != nullptr,
        "รหัสที่ไม่รู้จักยังคืนข้อความได้");

  for (int i = 0; i <= (int)MASSMORE_SHT4X_GENUINE_NOT_SHT4X; i++) {
    const char *text = MassmoreSHT4x::genuineToString((massmore_sht4x_genuine_t)i);
    if (text == nullptr || text[0] == '\0') {
      check(false, "ข้อความผลตรวจของแท้ครบทุกค่า");
      break;
    }
  }
  check(true, "ข้อความผลตรวจของแท้ครบทุกค่า");

  /* clearError() */
  mockResetChip();
  MassmoreSHT4x sensor(&Wire);
  sensor.measure(nullptr, nullptr);
  check(sensor.lastError() != MASSMORE_SHT4X_OK, "มีข้อผิดพลาดค้างอยู่");
  sensor.clearError();
  check(sensor.lastError() == MASSMORE_SHT4X_OK, "clearError() ล้างได้");
  check(strcmp(sensor.lastErrorString(), "ปกติ") == 0, "lastErrorString() ตรงกัน");

  /* setTimeout รับค่าได้ */
  sensor.setTimeout(50);
  check(true, "setTimeout() เรียกได้ไม่พัง");

  /* readBytes ตรวจพารามิเตอร์ */
  mockResetChip();
  MassmoreSHT4x low(&Wire);
  low.begin();
  uint8_t buffer[6];
  check(!low.readBytes(nullptr, 6), "buffer เป็น nullptr ถูกปฏิเสธ");
  check(!low.readBytes(buffer, 0), "ความยาว 0 ถูกปฏิเสธ");
  check(!low.readBytes(buffer, 4), "ความยาวที่ไม่หารด้วย 3 ลงตัวถูกปฏิเสธ");
  check(low.sendCommand(MASSMORE_SHT4X_CMD_MEAS_LOW), "sendCommand() ส่งได้");
  delay(MASSMORE_SHT4X_MEAS_DURATION_LOW_MS);
  check(low.readBytes(buffer, 6), "อ่าน 6 ไบต์ได้");

  uint16_t first = 0;
  uint16_t second = 0;
  check(!low.commandAndRead(MASSMORE_SHT4X_CMD_MEAS_LOW, 3, nullptr, &second),
        "commandAndRead() ที่ตัวรับเป็น nullptr ถูกปฏิเสธ");
  check(low.commandAndRead(MASSMORE_SHT4X_CMD_MEAS_LOW, 3, &first, &second),
        "commandAndRead() ทำงานได้");
  check(first == 0x6666 && second == 0x8000, "ได้สอง word ตามที่ชิปส่งมา");
}

/* ========================================================================= */
/* main                                                                      */
/* ========================================================================= */

int main() {
  printf("==========================================================\n");
  printf("  ชุดทดสอบไลบรารี Massmore_SHT4x %s\n", MASSMORE_SHT4X_VERSION_STRING);
  printf("  รันบนเครื่อง PC ด้วยชิป SHT4x จำลอง ไม่ต้องมีบอร์ด\n");
  printf("==========================================================\n");

  testCrc();
  testConversion();
  testCommandTable();
  testBegin();
  testMeasure();
  testOffsetAndClipping();
  testNonBlocking();
  testUpdate();
  testHeater();
  testHeaterDutyGuard();
  testSerialAndVerify();
  testReset();
  testVariant();
  testDerived();
  testScanAndMisc();

  printf("\n==========================================================\n");
  printf("  ผ่าน %d ข้อ   ไม่ผ่าน %d ข้อ\n", g_pass, g_fail);
  printf("==========================================================\n");
  return (g_fail == 0) ? 0 : 1;
}
