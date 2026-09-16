/*
  05_Factory_Test — Outgoing QA/QC สำหรับบอร์ด Massmore SHT4X (SKU-1022)

  ใช้ตรวจบอร์ดก่อนส่งลูกค้า และให้ Massmore Web Serial Monitor อ่านผลอัตโนมัติ
  Serial 115200 baud — บรรทัดที่ขึ้นต้นด้วย # เป็น machine-parsable

  Default wiring (Primary test MCU = ESP32 Classic)
    VIN -> 3V3, GND -> GND, SDA -> GPIO 21, SCL -> GPIO 22
  ESP32-S3 : SDA -> GPIO 8, SCL -> GPIO 9
  AVR Nano : SDA -> A4, SCL -> A5 (VIN -> 5V)

  ลำดับการทดสอบ
    1. BUS_SCAN      หา SHT4x ที่ 0x44 / 0x45 / 0x46
    2. CHIP_ID       SHT4x ไม่มี CHIP_ID register ใช้ Serial Number + CRC แทน
    3. SERIAL        Serial Number 32-bit จากโรงงาน
    4. AUTHENTICITY  heuristic 9 ข้อ (isGenuine) -> GENUINE / SUSPECT
    5. RANGE_TEMP / RANGE_HUMI  ค่าอยู่ในช่วงกายภาพของชิป
    6. CONTINUOUS    อ่าน 20 ครั้ง ไม่มี NAN / TIMEOUT และ noise สมเหตุสมผล
    7. HEATER        ยิง 200 mW 1 s อุณหภูมิต้องขึ้น (พิสูจน์ว่า sensing element ทำงาน)
    8. VERDICT

  พิมพ์ r แล้ว Enter ใน Serial Monitor เพื่อทดสอบซ้ำ (ไม่ต้อง reset)

  by Massmore | MIT License
*/

#include <Massmore_SHT4x.h>

/* ---------------- ขาสำหรับ Factory Test (hardcode เฉพาะ sketch นี้) ---------------- */
#if defined(ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define FT_SDA 8
#define FT_SCL 9
#define FT_MCU "ESP32-S3"
#else
#define FT_SDA 21
#define FT_SCL 22
#define FT_MCU "ESP32"
#endif
#elif defined(__AVR_ATmega328P__)
#define FT_MCU "AVR_ATMEGA328P"
#else
#define FT_MCU "UNKNOWN"
#endif

#define FT_VERSION "v1.0"
#define FT_PRODUCT "Massmore_SHT4x"
#define FT_EXPECTED_ADDR Massmore_SHT4x::I2C_ADDR_DEFAULT

/* เกณฑ์ผ่าน */
#define FT_SAMPLES 20
#define FT_SAMPLE_GAP_MS 50
#define FT_NOISE_T_MAX 1.0f    /* peak-to-peak °C ใน 20 ตัวอย่าง */
#define FT_NOISE_RH_MAX 3.0f   /* peak-to-peak %RH */
#define FT_HEATER_RISE_MIN 0.3f /* °C */

Massmore_SHT4x sensor;
char failReason[24];
bool anyFail = false;

/* ---------------- helpers ---------------- */

void resultLine(const char *name, bool pass, const char *value) {
  Serial.print(F("#RESULT "));
  Serial.print(name);
  Serial.print(pass ? F(" PASS ") : F(" FAIL "));
  Serial.println(value);
}

void resultHex(const char *name, bool pass, uint32_t value) {
  char buf[12];
  snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)value);
  resultLine(name, pass, buf);
}

void resultFloat(const char *name, bool pass, float value) {
  char buf[16];
  dtostrf(value, 1, 2, buf);
  resultLine(name, pass, buf);
}

void fail(const char *name, const char *value, const char *reason) {
  resultLine(name, false, value);
  if (!anyFail) {
    strncpy(failReason, reason, sizeof(failReason) - 1);
    failReason[sizeof(failReason) - 1] = '\0';
  }
  anyFail = true;
}

void verdict() {
  Serial.print(F("#VERDICT "));
  if (anyFail) {
    Serial.print(F("FAIL "));
    Serial.println(failReason);
    Serial.print(F("[FAIL] QA CHECK FAILED: "));
    Serial.println(failReason);
  } else {
    Serial.println(F("PASS"));
    Serial.println(F("[PASS] SENSOR QA PASSED - READY TO SHIP"));
  }
  Serial.println();
  Serial.println(F("Type r + Enter to run again"));
}

/* ---------------- test sequence ---------------- */

void runFactoryTest() {
  anyFail = false;
  failReason[0] = '\0';

  Serial.println();
  Serial.print(F("#MASSMORE_FACTORY_TEST "));
  Serial.println(F(FT_VERSION));
  Serial.print(F("#PRODUCT "));
  Serial.println(F(FT_PRODUCT));
  Serial.print(F("#MCU "));
  Serial.println(F(FT_MCU));
  Serial.print(F("Library "));
  Serial.println(Massmore_SHT4x::version());

  /* 1. BUS_SCAN */
  uint8_t found[3];
  uint8_t n = Massmore_SHT4x::scan(Wire, found);
  if (n == 0) {
    fail("BUS_SCAN", "NONE", "NO_DEVICE");
    Serial.println(F("  -> no ACK at 0x44/0x45/0x46: check VIN, GND, SDA, SCL, pull-ups"));
    verdict();
    return;
  }
  {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", found[0]);
    bool ok = (found[0] == FT_EXPECTED_ADDR);
    if (ok) {
      resultLine("BUS_SCAN", true, buf);
    } else {
      fail("BUS_SCAN", buf, "UNEXPECTED_ADDR");
    }
  }

  /* 2-3. CHIP_ID (Serial Number) + SERIAL */
  bool begun = sensor.begin(found[0]);
  if (!begun) {
    fail("CHIP_ID", sensor.lastErrorString(), "CHIP_ID_MISMATCH");
    verdict();
    return;
  }
  resultHex("CHIP_ID", true, sensor.getSerialNumber());
  resultHex("SERIAL", true, sensor.getSerialNumber());

  /* 4. AUTHENTICITY */
  bool genuine = sensor.isGenuine();
  for (uint8_t i = 0; i < Massmore_SHT4x::CHK_COUNT; i++) {
    Serial.print(F("  check "));
    Serial.print(i + 1);
    Serial.print(F(" "));
    Serial.print(Massmore_SHT4x::genuineCheckName(i));
    Serial.println((sensor.getGenuineMask() & (1u << i)) ? F(" ok") : F(" FAIL"));
  }
  if (genuine) {
    resultLine("AUTHENTICITY", true, "GENUINE");
  } else {
    fail("AUTHENTICITY", "SUSPECT", "AUTHENTICITY_SUSPECT");
  }

  /* 5-6. RANGE + CONTINUOUS */
  float tMin = 1e9f, tMax = -1e9f, hMin = 1e9f, hMax = -1e9f;
  float tFirst = NAN, hFirst = NAN;
  uint8_t good = 0;
  for (uint8_t i = 0; i < FT_SAMPLES; i++) {
    float t, h;
    if (sensor.readAll(t, h) && !isnan(t) && !isnan(h)) {
      good++;
      if (isnan(tFirst)) {
        tFirst = t;
        hFirst = h;
      }
      if (t < tMin) tMin = t;
      if (t > tMax) tMax = t;
      if (h < hMin) hMin = h;
      if (h > hMax) hMax = h;
    }
    delay(FT_SAMPLE_GAP_MS);
  }

  bool rangeT = !isnan(tFirst) && tFirst > Massmore_SHT4x::TEMP_MIN_C &&
                tFirst < Massmore_SHT4x::TEMP_MAX_C;
  bool rangeH = !isnan(hFirst) && hFirst >= Massmore_SHT4x::HUMI_MIN_PERCENT &&
                hFirst <= Massmore_SHT4x::HUMI_MAX_PERCENT;
  if (rangeT) {
    resultFloat("RANGE_TEMP", true, tFirst);
  } else {
    fail("RANGE_TEMP", "NAN", "TEMP_OUT_OF_RANGE");
  }
  if (rangeH) {
    resultFloat("RANGE_HUMI", true, hFirst);
  } else {
    fail("RANGE_HUMI", "NAN", "HUMI_OUT_OF_RANGE");
  }

  {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)good, (unsigned)FT_SAMPLES);
    float noiseT = tMax - tMin;
    float noiseH = hMax - hMin;
    Serial.print(F("  noise p-p  T="));
    Serial.print(noiseT, 2);
    Serial.print(F(" C  RH="));
    Serial.print(noiseH, 2);
    Serial.println(F(" %"));
    if (good != FT_SAMPLES) {
      fail("CONTINUOUS", buf, "READ_DROPOUT");
    } else if (noiseT > FT_NOISE_T_MAX || noiseH > FT_NOISE_RH_MAX) {
      fail("CONTINUOUS", buf, "NOISE_TOO_HIGH");
    } else {
      resultLine("CONTINUOUS", true, buf);
    }
  }

  /* 7. HEATER — พิสูจน์ว่า sensing element ตอบสนองจริง */
  {
    float before = sensor.readTemperature();
    Massmore_SHT4x::Readings hot;
    bool heaterOk = sensor.runHeater(Massmore_SHT4x::HeaterMode::MW200_1S, &hot);
    float rise = heaterOk ? (hot.temperature - before) : NAN;
    if (heaterOk && rise >= FT_HEATER_RISE_MIN) {
      resultFloat("HEATER", true, rise);
    } else {
      char buf[16];
      if (isnan(rise)) {
        strncpy(buf, sensor.lastErrorString(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      } else {
        dtostrf(rise, 1, 2, buf);
      }
      fail("HEATER", buf, "HEATER_NO_RESPONSE");
    }
  }

  verdict();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
#if defined(ESP32)
  Wire.begin(FT_SDA, FT_SCL);
#else
  Wire.begin();
#endif
  Wire.setClock(100000);
  runFactoryTest();
}

void loop() {
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'r' || c == 'R') {
      /* พักให้ Heater ครบ duty cycle ก่อนรอบใหม่ (ถ้าจำเป็น) */
      uint32_t wait = sensor.heaterCooldownRemainingMs();
      if (wait > 0) {
        Serial.print(F("Heater cooldown "));
        Serial.print(wait);
        Serial.println(F(" ms ..."));
        delay(wait);
      }
      runFactoryTest();
    }
  }
}
