/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  11_FactoryTest - ชุดทดสอบโรงงานสำหรับบอร์ด Massmore SHT4X (SKU-1022)

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย (ค่าเริ่มต้นของเฟิร์มแวร์ .bin ที่แจกไว้)
    VIN -> 3.3V หรือ 5V
    GND -> GND
    SDA -> GPIO 21
    SCL -> GPIO 22

  เฟิร์มแวร์เริ่มทดสอบเองทันทีที่บูต ไม่ต้องกดอะไร
  พิมพ์ r แล้วกด Enter ใน Serial Monitor เพื่อทดสอบซ้ำ (สะดวกตอนตรวจทีละหลายบอร์ด)

  ลำดับการทำงาน
    GATE 1  สแกนบัส I2C หาที่อยู่ของเซ็นเซอร์
    GATE 2  ตรวจตัวตนของชิปด้วย verifyChip() 10 ข้อ
    RUN TEST  24 หัวข้อ ครอบคลุมทุกคำสั่งที่ชิปมี
    สรุปผล  ผ่าน / ไม่ผ่าน

  ถ้าด่านคัดกรองไม่ผ่าน เฟิร์มแวร์จะหยุดแล้วบอกสาเหตุที่เป็นไปได้
  แทนที่จะรัน 24 หัวข้อให้ FAIL ทั้งหมดโดยไม่ได้ข้อมูลอะไรเพิ่ม

  บรรทัดที่ขึ้นต้นด้วย # ออกแบบให้โปรแกรมอื่นแยกวิเคราะห์ได้
    #RESULT,<ลำดับ>,<ชื่อหัวข้อ>,<PASS|WARN|FAIL>,<รายละเอียด>
    #DEVICE,<address>,<variant>,<serial>,<ผลตรวจของแท้>,<ผ่านกี่ข้อจาก10>
    #VERDICT,<PASS|FAIL>,<จำนวนผ่าน>,<จำนวนไม่ผ่าน>,<จำนวนเตือน>

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

/* ------------------------------------------------------------------ */
/* ตั้งค่าให้ตรงกับบอร์ดที่ใช้ทดสอบ                                       */
/* ------------------------------------------------------------------ */

#define PIN_SDA 21
#define PIN_SCL 22

/* รุ่นที่ติ๊กไว้บนซิลค์สกรีน ใช้กำหนดเกณฑ์ความแม่นยำที่รายงาน */
#define BOARD_VARIANT MASSMORE_SHT4X_VARIANT_SHT40

/* เกณฑ์ผ่านของแต่ละหัวข้อ ปรับได้ตามสภาพห้องทดสอบ */
#define LIMIT_T_MIN 5.0f
#define LIMIT_T_MAX 55.0f
#define LIMIT_RH_MIN 5.0f
#define LIMIT_RH_MAX 98.0f
#define LIMIT_STABILITY_T 0.5f
#define LIMIT_STABILITY_RH 3.0f
#define LIMIT_HEATER_RISE 0.3f
#define LIMIT_CROSS_PRECISION_T 1.0f
#define LIMIT_CROSS_PRECISION_RH 5.0f

MassmoreSHT4x sensor;

uint8_t g_index = 0;
uint16_t g_pass = 0;
uint16_t g_warn = 0;
uint16_t g_fail = 0;

uint8_t g_foundAddress = 0;
uint8_t g_busDevices = 0;
uint32_t g_serial = 0;
massmore_sht4x_genuine_t g_genuine = MASSMORE_SHT4X_GENUINE_UNKNOWN;
uint8_t g_genuinePass = 0;

/* ------------------------------------------------------------------ */
/* ตัวช่วยจัดรูปแบบ                                                     */
/*   เลี่ยง %f ใน snprintf เพราะ newlib บน ESP32 บางรุ่นไม่รองรับ         */
/* ------------------------------------------------------------------ */

/* แปลงทศนิยมเป็นข้อความ 2 ตำแหน่ง */
void ftF2(char *out, size_t size, float value) {
  if (isnan(value)) {
    snprintf(out, size, "nan");
    return;
  }
  bool negative = value < 0.0f;
  if (negative) {
    value = -value;
  }
  long whole = (long)value;
  long frac = (long)((value - (float)whole) * 100.0f + 0.5f);
  if (frac >= 100) {
    whole++;
    frac -= 100;
  }
  snprintf(out, size, "%s%ld.%02ld", negative ? "-" : "", whole, frac);
}

/* คัดลอกข้อความโดยไม่ตัดกลางตัวอักษรไทย (UTF-8 ตัวละ 3 ไบต์) */
void ftCopyDetail(char *dst, size_t dstSize, const char *src) {
  if (dstSize == 0) {
    return;
  }
  size_t length = strlen(src);
  if (length < dstSize) {
    memcpy(dst, src, length + 1);
    return;
  }
  size_t cut = dstSize - 1;
  while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) {
    cut--; /* ถอยกลับพ้นไบต์ต่อเนื่องของตัวอักษรเดียวกัน */
  }
  memcpy(dst, src, cut);
  dst[cut] = '\0';
}

/* พิมพ์ผลหนึ่งหัวข้อ ทั้งบรรทัดสำหรับคนอ่านและบรรทัดสำหรับเครื่องอ่าน */
void report(const char *name, const char *status, const char *detail) {
  g_index++;
  if (strcmp(status, "PASS") == 0) {
    g_pass++;
  } else if (strcmp(status, "WARN") == 0) {
    g_warn++;
  } else {
    g_fail++;
  }

  const char *tag = "[FAIL]";
  if (strcmp(status, "PASS") == 0) {
    tag = "[ OK ]";
  } else if (strcmp(status, "WARN") == 0) {
    tag = "[WARN]";
  }

  char safe[288];
  ftCopyDetail(safe, sizeof(safe), detail);

  char line[400];
  snprintf(line, sizeof(line), "%s %02u %-18s %s", tag, (unsigned)g_index, name, safe);
  Serial.println(line);

  snprintf(line, sizeof(line), "#RESULT,%u,%s,%s,%s", (unsigned)g_index, name, status,
           safe);
  Serial.println(line);
}

void pass(const char *name, const char *detail) { report(name, "PASS", detail); }
void warn(const char *name, const char *detail) { report(name, "WARN", detail); }
void fail(const char *name, const char *detail) { report(name, "FAIL", detail); }

/* ------------------------------------------------------------------ */
/* GATE 1 : สแกนบัส I2C                                                */
/* ------------------------------------------------------------------ */

bool gateScan() {
  Serial.println();
  Serial.println("--- GATE 1 : สแกนบัส I2C ---");

  g_busDevices = 0;
  g_foundAddress = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      g_busDevices++;
      char line[96];
      snprintf(line, sizeof(line), "      พบอุปกรณ์ที่ 0x%02X", address);
      Serial.println(line);
      if (g_foundAddress == 0 &&
          (address == MASSMORE_SHT4X_I2C_ADDR_A || address == MASSMORE_SHT4X_I2C_ADDR_B ||
           address == MASSMORE_SHT4X_I2C_ADDR_C)) {
        g_foundAddress = address;
      }
    }
    delay(2);
  }

  char detail[288];
  if (g_foundAddress != 0) {
    snprintf(detail, sizeof(detail), "พบ SHT4x ที่ 0x%02X (อุปกรณ์บนบัสทั้งหมด %u ตัว)",
             g_foundAddress, (unsigned)g_busDevices);
    pass("I2C_SCAN", detail);
    return true;
  }

  if (g_busDevices == 0) {
    fail("I2C_SCAN", "ไม่พบอุปกรณ์บนบัสเลย ตรวจสาย VIN GND SDA SCL");
  } else {
    snprintf(detail, sizeof(detail),
             "พบ %u อุปกรณ์ แต่ไม่มีตัวไหนอยู่ที่ 0x44 0x45 หรือ 0x46",
             (unsigned)g_busDevices);
    fail("I2C_SCAN", detail);
  }
  return false;
}

/* ------------------------------------------------------------------ */
/* GATE 2 : ตรวจตัวตนของชิป                                            */
/* ------------------------------------------------------------------ */

bool gateIdentity() {
  Serial.println();
  Serial.println("--- GATE 2 : ตรวจตัวตนของชิป ---");

  if (!sensor.beginWithExistingBus(g_foundAddress, BOARD_VARIANT)) {
    char detail[288];
    snprintf(detail, sizeof(detail), "begin() ไม่สำเร็จ : %s", sensor.lastErrorString());
    fail("IDENTITY", detail);
    return false;
  }

  g_genuine = sensor.verifyChip();
  g_genuinePass = sensor.getVerifyPassCount();
  g_serial = sensor.getSerialNumber();

  uint16_t mask = sensor.getVerifyMask();
  for (uint8_t i = 0; i < MASSMORE_SHT4X_CHK_COUNT; i++) {
    char line[288];
    snprintf(line, sizeof(line), "      [%s] %u. %s", (mask & (1u << i)) ? "ok" : "--",
             (unsigned)(i + 1), MassmoreSHT4x::getVerifyCheckName(i));
    Serial.println(line);
  }

  char detail[288];
  snprintf(detail, sizeof(detail), "ผ่าน %u/10 ข้อ : %s", (unsigned)g_genuinePass,
           MassmoreSHT4x::genuineToString(g_genuine));

  if (g_genuine == MASSMORE_SHT4X_GENUINE_PASS) {
    pass("IDENTITY", detail);
    return true;
  }
  if (g_genuine == MASSMORE_SHT4X_GENUINE_PARTIAL) {
    warn("IDENTITY", detail);
    return true;
  }
  fail("IDENTITY", detail);
  return false;
}

/* ------------------------------------------------------------------ */
/* RUN TEST                                                            */
/* ------------------------------------------------------------------ */

void testSoftReset() {
  char detail[288];
  if (!sensor.softReset()) {
    snprintf(detail, sizeof(detail), "ส่งคำสั่ง 0x94 ไม่สำเร็จ : %s",
             sensor.lastErrorString());
    fail("SOFT_RESET", detail);
    return;
  }
  delay(5);
  float t = 0.0f;
  if (!sensor.measure(&t, nullptr)) {
    snprintf(detail, sizeof(detail), "รีเซ็ตแล้ววัดต่อไม่ได้ : %s",
             sensor.lastErrorString());
    fail("SOFT_RESET", detail);
    return;
  }
  char buf[16];
  ftF2(buf, sizeof(buf), t);
  snprintf(detail, sizeof(detail), "0x94 สำเร็จ วัดต่อได้ %s C", buf);
  pass("SOFT_RESET", detail);
}

void testSerialNumber() {
  char detail[288];
  uint32_t first = 0;
  uint32_t second = 0;

  if (!sensor.readSerialNumber(&first)) {
    snprintf(detail, sizeof(detail), "อ่านคำสั่ง 0x89 ไม่สำเร็จ : %s",
             sensor.lastErrorString());
    fail("SERIAL_NUMBER", detail);
    return;
  }
  delay(5);
  if (!sensor.readSerialNumber(&second)) {
    fail("SERIAL_NUMBER", "อ่านซีเรียลครั้งที่สองไม่สำเร็จ");
    return;
  }
  g_serial = first;

  if (first == 0x00000000UL || first == 0xFFFFFFFFUL) {
    snprintf(detail, sizeof(detail), "ซีเรียลเป็นค่าว่าง 0x%08lX", (unsigned long)first);
    fail("SERIAL_NUMBER", detail);
    return;
  }
  if (first != second) {
    snprintf(detail, sizeof(detail), "อ่านสองครั้งได้คนละค่า 0x%08lX กับ 0x%08lX",
             (unsigned long)first, (unsigned long)second);
    fail("SERIAL_NUMBER", detail);
    return;
  }
  snprintf(detail, sizeof(detail), "0x%08lX (อ่านซ้ำได้ค่าเดิม)", (unsigned long)first);
  pass("SERIAL_NUMBER", detail);
}

void testCrcIntegrity() {
  const uint16_t rounds = 100;
  uint16_t crcErrors = 0;
  uint16_t otherErrors = 0;

  for (uint16_t i = 0; i < rounds; i++) {
    if (!sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, nullptr, nullptr)) {
      if (sensor.lastError() == MASSMORE_SHT4X_ERR_CRC) {
        crcErrors++;
      } else {
        otherErrors++;
      }
    }
    delay(5);
  }

  char detail[288];
  snprintf(detail, sizeof(detail), "อ่าน %u ครั้ง CRC ผิด %u ครั้ง ผิดอื่น %u ครั้ง",
           (unsigned)rounds, (unsigned)crcErrors, (unsigned)otherErrors);
  if (crcErrors == 0 && otherErrors == 0) {
    pass("CRC_INTEGRITY", detail);
  } else if (crcErrors + otherErrors <= 2) {
    warn("CRC_INTEGRITY", detail);
  } else {
    fail("CRC_INTEGRITY", detail);
  }
}

/* วัดหนึ่งระดับความละเอียด แล้วรายงานค่าและเวลาที่ใช้จริง */
uint32_t measureOne(massmore_sht4x_precision_t precision, const char *name) {
  float t = 0.0f;
  float h = 0.0f;
  uint32_t start = micros();
  bool ok = sensor.measureWith(precision, &t, &h);
  uint32_t elapsed = micros() - start;

  char detail[288];
  if (!ok) {
    snprintf(detail, sizeof(detail), "วัดไม่สำเร็จ : %s", sensor.lastErrorString());
    fail(name, detail);
    return 0;
  }

  char bufT[16];
  char bufH[16];
  ftF2(bufT, sizeof(bufT), t);
  ftF2(bufH, sizeof(bufH), h);
  snprintf(detail, sizeof(detail), "%s C  %s %%RH  ใช้เวลา %lu us", bufT, bufH,
           (unsigned long)elapsed);

  if (t < LIMIT_T_MIN || t > LIMIT_T_MAX || h < LIMIT_RH_MIN || h > LIMIT_RH_MAX) {
    warn(name, detail);
  } else {
    pass(name, detail);
  }
  return elapsed;
}

void testTimingOrder(uint32_t low, uint32_t medium, uint32_t high) {
  char detail[288];
  snprintf(detail, sizeof(detail), "ต่ำ %lu us  กลาง %lu us  สูง %lu us",
           (unsigned long)low, (unsigned long)medium, (unsigned long)high);

  if (low == 0 || medium == 0 || high == 0) {
    fail("TIMING_ORDER", "มีระดับที่วัดไม่สำเร็จ");
    return;
  }
  if (low < medium && medium < high) {
    pass("TIMING_ORDER", detail);
  } else {
    warn("TIMING_ORDER", detail);
  }
}

/* SHT4x แท้ต้อง NACK เมื่อถูกอ่านตอนยังวัดไม่เสร็จ (ไม่มี clock stretching) */
void testNackBehavior() {
  char detail[288];
  if (!sensor.sendCommand(MASSMORE_SHT4X_CMD_MEAS_HIGH)) {
    fail("NACK_BEHAVIOR", "ส่งคำสั่งวัดไม่สำเร็จ");
    return;
  }
  delay(2); /* ความละเอียดสูงต้องใช้ 6.9 ms ตอนนี้จึงยังไม่เสร็จแน่นอน */

  uint8_t buffer[MASSMORE_SHT4X_MEAS_FRAME_LEN];
  bool early = sensor.readBytes(buffer, MASSMORE_SHT4X_MEAS_FRAME_LEN);

  /* รอให้ครบเวลาแล้วเก็บผลทิ้ง ไม่ให้ค้างคาบัส */
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
  sensor.readBytes(buffer, MASSMORE_SHT4X_MEAS_FRAME_LEN);

  if (early) {
    fail("NACK_BEHAVIOR", "อ่านที่ 2 ms แล้วได้ข้อมูล ทั้งที่ควร NACK");
  } else {
    snprintf(detail, sizeof(detail), "อ่านที่ 2 ms แล้วชิป NACK ถูกต้องตาม datasheet");
    pass("NACK_BEHAVIOR", detail);
  }
}

void testNoiseCompare() {
  float sumLow = 0.0f;
  float sumLow2 = 0.0f;
  float sumHigh = 0.0f;
  float sumHigh2 = 0.0f;
  const uint8_t rounds = 20;
  uint8_t okLow = 0;
  uint8_t okHigh = 0;

  for (uint8_t i = 0; i < rounds; i++) {
    float t = 0.0f;
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_LOW, &t, nullptr)) {
      sumLow += t;
      sumLow2 += t * t;
      okLow++;
    }
    delay(10);
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, nullptr)) {
      sumHigh += t;
      sumHigh2 += t * t;
      okHigh++;
    }
    delay(10);
  }

  if (okLow < 5 || okHigh < 5) {
    fail("NOISE_COMPARE", "อ่านค่าได้ไม่พอสำหรับคำนวณ");
    return;
  }

  float meanLow = sumLow / okLow;
  float meanHigh = sumHigh / okHigh;
  float varLow = (sumLow2 / okLow) - meanLow * meanLow;
  float varHigh = (sumHigh2 / okHigh) - meanHigh * meanHigh;
  if (varLow < 0.0f) {
    varLow = 0.0f;
  }
  if (varHigh < 0.0f) {
    varHigh = 0.0f;
  }

  char bufLow[16];
  char bufHigh[16];
  ftF2(bufLow, sizeof(bufLow), sqrtf(varLow));
  ftF2(bufHigh, sizeof(bufHigh), sqrtf(varHigh));

  char detail[288];
  snprintf(detail, sizeof(detail), "sd ต่ำ=%s C  sd สูง=%s C", bufLow, bufHigh);
  pass("NOISE_COMPARE", detail);
}

void testCrossPrecision() {
  float lowT = 0.0f;
  float lowH = 0.0f;
  float highT = 0.0f;
  float highH = 0.0f;

  if (!sensor.measureWith(MASSMORE_SHT4X_PRECISION_LOW, &lowT, &lowH) ||
      !sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &highT, &highH)) {
    fail("CROSS_PRECISION", "วัดเปรียบเทียบไม่สำเร็จ");
    return;
  }

  float diffT = fabsf(highT - lowT);
  float diffH = fabsf(highH - lowH);

  char bufT[16];
  char bufH[16];
  ftF2(bufT, sizeof(bufT), diffT);
  ftF2(bufH, sizeof(bufH), diffH);

  char detail[288];
  snprintf(detail, sizeof(detail), "ต่างกัน %s C และ %s %%RH ระหว่างความละเอียดต่ำกับสูง",
           bufT, bufH);

  if (diffT <= LIMIT_CROSS_PRECISION_T && diffH <= LIMIT_CROSS_PRECISION_RH) {
    pass("CROSS_PRECISION", detail);
  } else {
    warn("CROSS_PRECISION", detail);
  }
}

void testHeater(massmore_sht4x_heater_t mode, const char *name, bool requireRise) {
  char detail[288];

  float cold = 0.0f;
  if (!sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &cold, nullptr)) {
    fail(name, "วัดค่าก่อนยิงฮีตเตอร์ไม่สำเร็จ");
    return;
  }

  sensor.setHeaterDutyGuard(false); /* ระหว่างทดสอบเราคุมจังหวะพักเอง */
  massmore_sht4x_reading_t hot;
  bool ok = sensor.runHeater(mode, &hot);
  sensor.setHeaterDutyGuard(true);

  if (!ok) {
    snprintf(detail, sizeof(detail), "ยิงฮีตเตอร์ไม่สำเร็จ : %s", sensor.lastErrorString());
    fail(name, detail);
    return;
  }

  float rise = hot.temperature - cold;
  char bufCold[16];
  char bufHot[16];
  char bufRise[16];
  ftF2(bufCold, sizeof(bufCold), cold);
  ftF2(bufHot, sizeof(bufHot), hot.temperature);
  ftF2(bufRise, sizeof(bufRise), rise);

  snprintf(detail, sizeof(detail), "%u mW  %s C -> %s C  ขึ้น %s C",
           (unsigned)MassmoreSHT4x::heaterPowerMilliwatt(mode), bufCold, bufHot,
           bufRise);

  if (!requireRise) {
    pass(name, detail);
  } else if (rise >= LIMIT_HEATER_RISE) {
    pass(name, detail);
  } else {
    warn(name, detail);
  }

  /* พักให้เย็นและรักษา duty cycle */
  delay(3000);
}

void testHeaterDutyGuard() {
  char detail[288];

  sensor.resetHeaterStats();
  sensor.setHeaterDutyGuard(true);
  delay(2100); /* ให้พ้นช่วงผ่อนผันตอนเพิ่งเริ่มนับ */

  bool firstOk = sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr);
  bool secondBlocked = !sensor.runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr);
  bool rightError = sensor.lastError() == MASSMORE_SHT4X_ERR_HEATER_DUTY;

  char buf[16];
  ftF2(buf, sizeof(buf), sensor.getHeaterDutyPercent());

  if (firstOk && secondBlocked && rightError) {
    snprintf(detail, sizeof(detail), "ยิงซ้ำทันทีถูกกันไว้ถูกต้อง duty ตอนนี้ %s %%", buf);
    pass("HEATER_DUTY", detail);
  } else {
    snprintf(detail, sizeof(detail), "พฤติกรรมตัวกันเผลอไม่ตรงที่คาด duty %s %%", buf);
    fail("HEATER_DUTY", detail);
  }
  sensor.resetHeaterStats();
  delay(2000);
}

void testCooldown() {
  char detail[288];
  massmore_sht4x_reading_t settled;
  if (!sensor.measureAfterHeating(4000, settled)) {
    fail("COOLDOWN", "วัดค่าหลังพักไม่สำเร็จ");
    return;
  }

  char bufT[16];
  char bufH[16];
  ftF2(bufT, sizeof(bufT), settled.temperature);
  ftF2(bufH, sizeof(bufH), settled.humidity);
  snprintf(detail, sizeof(detail), "หลังพัก 4 วินาที %s C  %s %%RH", bufT, bufH);

  if (settled.temperature < LIMIT_T_MIN || settled.temperature > LIMIT_T_MAX) {
    warn("COOLDOWN", detail);
  } else {
    pass("COOLDOWN", detail);
  }
}

void testBogusCommand() {
  char detail[288];
  sensor.sendCommand(MASSMORE_SHT4X_CMD_BOGUS);
  delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);

  uint8_t buffer[MASSMORE_SHT4X_MEAS_FRAME_LEN];
  bool answered = sensor.readBytes(buffer, MASSMORE_SHT4X_MEAS_FRAME_LEN);

  sensor.softReset();
  delay(5);

  if (answered) {
    snprintf(detail, sizeof(detail), "คำสั่ง 0x%02X ถูกตอบด้วยข้อมูล ทั้งที่ไม่มีในตาราง",
             MASSMORE_SHT4X_CMD_BOGUS);
    fail("BOGUS_CMD", detail);
  } else {
    snprintf(detail, sizeof(detail), "คำสั่ง 0x%02X ไม่ถูกตอบด้วยข้อมูล ถูกต้อง",
             MASSMORE_SHT4X_CMD_BOGUS);
    pass("BOGUS_CMD", detail);
  }
}

void testGeneralCall() {
  char detail[288];
  if (!sensor.generalCallReset()) {
    snprintf(detail, sizeof(detail), "ส่ง general call ไม่สำเร็จ : %s",
             sensor.lastErrorString());
    warn("GENERAL_CALL", detail);
    return;
  }
  delay(10);
  float t = 0.0f;
  if (!sensor.measure(&t, nullptr)) {
    fail("GENERAL_CALL", "หลัง general call แล้ววัดต่อไม่ได้");
    return;
  }
  char buf[16];
  ftF2(buf, sizeof(buf), t);
  snprintf(detail, sizeof(detail), "รีเซ็ตทั้งบัสแล้ววัดต่อได้ %s C", buf);
  pass("GENERAL_CALL", detail);
}

void testPlausibility() {
  char detail[288];
  float t = 0.0f;
  float h = 0.0f;
  if (!sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, &h)) {
    fail("PLAUSIBILITY", "วัดค่าไม่สำเร็จ");
    return;
  }

  float dp = MassmoreSHT4x::dewPoint(t, h);
  char bufT[16];
  char bufH[16];
  char bufD[16];
  ftF2(bufT, sizeof(bufT), t);
  ftF2(bufH, sizeof(bufH), h);
  ftF2(bufD, sizeof(bufD), dp);
  snprintf(detail, sizeof(detail), "%s C  %s %%RH  จุดน้ำค้าง %s C", bufT, bufH, bufD);

  if (dp > t + 0.5f) {
    fail("PLAUSIBILITY", "จุดน้ำค้างสูงกว่าอุณหภูมิอากาศ เป็นไปไม่ได้");
    return;
  }
  if (t < LIMIT_T_MIN || t > LIMIT_T_MAX || h < LIMIT_RH_MIN || h > LIMIT_RH_MAX) {
    warn("PLAUSIBILITY", detail);
    return;
  }
  pass("PLAUSIBILITY", detail);
}

void testStability() {
  const uint8_t rounds = 20;
  float minT = 1000.0f;
  float maxT = -1000.0f;
  float minH = 1000.0f;
  float maxH = -1000.0f;
  uint8_t ok = 0;

  for (uint8_t i = 0; i < rounds; i++) {
    float t = 0.0f;
    float h = 0.0f;
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, &h)) {
      ok++;
      if (t < minT) {
        minT = t;
      }
      if (t > maxT) {
        maxT = t;
      }
      if (h < minH) {
        minH = h;
      }
      if (h > maxH) {
        maxH = h;
      }
    }
    delay(20);
  }

  if (ok < rounds / 2) {
    fail("STABILITY", "อ่านค่าสำเร็จน้อยเกินไป");
    return;
  }

  float rangeT = maxT - minT;
  float rangeH = maxH - minH;
  char bufT[16];
  char bufH[16];
  ftF2(bufT, sizeof(bufT), rangeT);
  ftF2(bufH, sizeof(bufH), rangeH);

  char detail[288];
  snprintf(detail, sizeof(detail), "%u ครั้ง ช่วง T %s C  ช่วง RH %s %%", (unsigned)ok,
           bufT, bufH);

  if (rangeT <= LIMIT_STABILITY_T && rangeH <= LIMIT_STABILITY_RH) {
    pass("STABILITY", detail);
  } else {
    warn("STABILITY", detail);
  }
}

void testBusSpeed() {
  uint8_t ok400 = 0;
  uint8_t ok100 = 0;

  Wire.setClock(400000UL);
  for (uint8_t i = 0; i < 10; i++) {
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, nullptr, nullptr)) {
      ok400++;
    }
    delay(10);
  }

  Wire.setClock(100000UL);
  for (uint8_t i = 0; i < 10; i++) {
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, nullptr, nullptr)) {
      ok100++;
    }
    delay(10);
  }

  char detail[288];
  snprintf(detail, sizeof(detail), "400 kHz ผ่าน %u/10, 100 kHz ผ่าน %u/10",
           (unsigned)ok400, (unsigned)ok100);

  if (ok100 < 10) {
    fail("BUS_SPEED", detail);
  } else if (ok400 < 10) {
    warn("BUS_SPEED", detail);
  } else {
    pass("BUS_SPEED", detail);
  }
}

void testNonBlocking() {
  char detail[288];
  if (!sensor.startMeasurement()) {
    fail("NON_BLOCKING", "startMeasurement() ไม่สำเร็จ");
    return;
  }
  uint32_t spins = 0;
  uint32_t guard = millis() + 200;
  while (!sensor.isMeasurementReady()) {
    spins++;
    if (millis() > guard) {
      fail("NON_BLOCKING", "รอเกินเวลาที่ควรเป็น");
      return;
    }
  }
  float t = 0.0f;
  if (!sensor.readMeasurement(&t, nullptr)) {
    snprintf(detail, sizeof(detail), "readMeasurement() ไม่สำเร็จ : %s",
             sensor.lastErrorString());
    fail("NON_BLOCKING", detail);
    return;
  }
  char buf[16];
  ftF2(buf, sizeof(buf), t);
  snprintf(detail, sizeof(detail), "วนรอได้ %lu รอบระหว่างวัด แล้วได้ %s C",
           (unsigned long)spins, buf);
  pass("NON_BLOCKING", detail);
}

void testOffsetApi() {
  char detail[288];
  float before = 0.0f;
  if (!sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &before, nullptr)) {
    fail("OFFSET_API", "วัดค่าอ้างอิงไม่สำเร็จ");
    return;
  }

  sensor.setTemperatureOffset(-3.0f);
  float after = 0.0f;
  bool ok = sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &after, nullptr);
  sensor.setTemperatureOffset(0.0f);

  if (!ok) {
    fail("OFFSET_API", "วัดค่าหลังใส่ offset ไม่สำเร็จ");
    return;
  }

  float delta = before - after;
  char buf[16];
  ftF2(buf, sizeof(buf), delta);
  snprintf(detail, sizeof(detail), "ใส่ offset -3.00 C แล้วค่าลดลง %s C", buf);

  if (delta > 2.5f && delta < 3.5f) {
    pass("OFFSET_API", detail);
  } else {
    fail("OFFSET_API", detail);
  }
}

void testClippingApi() {
  char detail[288];
  sensor.setHumidityClipping(false);
  bool offOk = !sensor.getHumidityClipping();
  sensor.setHumidityClipping(true);
  bool onOk = sensor.getHumidityClipping();

  float h = 0.0f;
  bool measured = sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, nullptr, &h);

  if (offOk && onOk && measured && h >= 0.0f && h <= 100.0f) {
    char buf[16];
    ftF2(buf, sizeof(buf), h);
    snprintf(detail, sizeof(detail), "เปิดปิดได้ และค่าอยู่ในช่วง 0-100 (%s %%RH)", buf);
    pass("CLIPPING_API", detail);
  } else {
    fail("CLIPPING_API", "การตัดขอบความชื้นทำงานไม่ถูกต้อง");
  }
}

void testVariantSpec() {
  char bufT[16];
  char bufH[16];
  ftF2(bufT, sizeof(bufT), sensor.getTemperatureAccuracy());
  ftF2(bufH, sizeof(bufH), sensor.getHumidityAccuracy());

  char detail[288];
  snprintf(detail, sizeof(detail), "%s สเปก +/-%s C, +/-%s %%RH", sensor.getVariantName(),
           bufT, bufH);
  pass("VARIANT_SPEC", detail);
}

/* ------------------------------------------------------------------ */
/* รายงานสรุป                                                          */
/* ------------------------------------------------------------------ */

void printSummary(bool gatesPassed) {
  Serial.println();
  Serial.println("==========================================================");
  Serial.println("  รายงานสรุป Massmore SHT4X (SKU-1022)");
  Serial.println("==========================================================");
  Serial.println();

  char line[288];
  Serial.println("[ อุปกรณ์ที่ตรวจพบ ]");
  snprintf(line, sizeof(line), "  I2C address     : 0x%02X", g_foundAddress);
  Serial.println(line);
  snprintf(line, sizeof(line), "  รุ่นที่ตั้งไว้     : %s", sensor.getVariantName());
  Serial.println(line);
  snprintf(line, sizeof(line), "  ซีเรียล         : 0x%08lX", (unsigned long)g_serial);
  Serial.println(line);
  snprintf(line, sizeof(line), "  ผลตรวจของแท้     : %s  (%u/10)",
           MassmoreSHT4x::genuineToString(g_genuine), (unsigned)g_genuinePass);
  Serial.println(line);
  snprintf(line, sizeof(line), "  ไลบรารีเวอร์ชัน   : %s",
           MassmoreSHT4x::getLibraryVersion());
  Serial.println(line);

  Serial.println();
  Serial.println("[ สรุป ]");
  snprintf(line, sizeof(line), "  ผ่าน %u   เตือน %u   ไม่ผ่าน %u", (unsigned)g_pass,
           (unsigned)g_warn, (unsigned)g_fail);
  Serial.println(line);
  Serial.println();

  bool verdict = gatesPassed && (g_fail == 0);
  if (verdict) {
    Serial.println("  >>> ผลรวม: ผ่าน  บอร์ดนี้ใช้งานได้ปกติ <<<");
  } else {
    Serial.println("  >>> ผลรวม: ไม่ผ่าน  ดูหัวข้อที่ขึ้น FAIL ด้านบน <<<");
  }
  Serial.println("==========================================================");

  snprintf(line, sizeof(line), "#DEVICE,0x%02X,%s,0x%08lX,%s,%u", g_foundAddress,
           sensor.getVariantName(), (unsigned long)g_serial,
           MassmoreSHT4x::genuineToString(g_genuine), (unsigned)g_genuinePass);
  Serial.println(line);

  snprintf(line, sizeof(line), "#VERDICT,%s,%u,%u,%u", verdict ? "PASS" : "FAIL",
           (unsigned)g_pass, (unsigned)g_fail, (unsigned)g_warn);
  Serial.println(line);

  Serial.println();
  Serial.println("พิมพ์ r แล้วกด Enter เพื่อทดสอบซ้ำ");
}

/* ------------------------------------------------------------------ */
/* ลำดับการทดสอบทั้งหมด                                                 */
/* ------------------------------------------------------------------ */

void runAllTests() {
  g_index = 0;
  g_pass = 0;
  g_warn = 0;
  g_fail = 0;
  g_serial = 0;
  g_genuine = MASSMORE_SHT4X_GENUINE_UNKNOWN;
  g_genuinePass = 0;

  Serial.println();
  Serial.println("==========================================================");
  Serial.println("  Massmore SHT4X (SKU-1022) - Factory Test");
  Serial.println("  Temperature & Humidity Sensor  |  Sensirion SHT4x");
  Serial.println("==========================================================");
  char line[128];
  snprintf(line, sizeof(line), "  I2C  SDA=GPIO%u  SCL=GPIO%u", (unsigned)PIN_SDA,
           (unsigned)PIN_SCL);
  Serial.println(line);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000UL);

  if (!gateScan()) {
    Serial.println();
    Serial.println("หยุดที่ด่านที่ 1 สาเหตุที่เป็นไปได้");
    Serial.println("  1. VIN หรือ GND ยังไม่ได้ต่อ");
    Serial.println("  2. SDA กับ SCL สลับกัน");
    Serial.println("  3. สาย Qwiic หลวมหรือขาดใน");
    Serial.println("  4. บอร์ด MCU ใช้ขา I2C คนละเบอร์กับที่เฟิร์มแวร์ตั้งไว้");
    printSummary(false);
    return;
  }

  if (!gateIdentity()) {
    Serial.println();
    Serial.println("หยุดที่ด่านที่ 2 สาเหตุที่เป็นไปได้");
    Serial.println("  1. อุปกรณ์ที่เจอเป็นชิปคนละตระกูลที่ใช้ address เดียวกัน");
    Serial.println("     เช่น SHT3x หรือ AHT2x ซึ่งใช้คำสั่งแบบ 2 ไบต์");
    Serial.println("  2. สัญญาณไม่นิ่ง ลองใช้สายสั้นลงแล้วพิมพ์ r ทดสอบซ้ำ");
    Serial.println("  3. ชิปเป็นของเลียนแบบ");
    printSummary(false);
    return;
  }

  Serial.println();
  Serial.println("--- RUN TEST ---");

  testSoftReset();
  testSerialNumber();
  testCrcIntegrity();

  uint32_t timeHigh = measureOne(MASSMORE_SHT4X_PRECISION_HIGH, "MEAS_HIGH");
  uint32_t timeMedium = measureOne(MASSMORE_SHT4X_PRECISION_MEDIUM, "MEAS_MEDIUM");
  uint32_t timeLow = measureOne(MASSMORE_SHT4X_PRECISION_LOW, "MEAS_LOW");
  testTimingOrder(timeLow, timeMedium, timeHigh);

  testNackBehavior();
  testNoiseCompare();
  testCrossPrecision();

  testHeater(MASSMORE_SHT4X_HEATER_20MW_0S1, "HEATER_20MW", false);
  testHeater(MASSMORE_SHT4X_HEATER_110MW_1S, "HEATER_110MW", false);
  testHeater(MASSMORE_SHT4X_HEATER_200MW_1S, "HEATER_200MW", true);
  testHeaterDutyGuard();
  testCooldown();

  testBogusCommand();
  testGeneralCall();
  testPlausibility();
  testStability();
  testBusSpeed();
  testNonBlocking();
  testOffsetApi();
  testClippingApi();
  testVariantSpec();

  printSummary(true);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
  delay(300);
  runAllTests();
}

void loop() {
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'r' || c == 'R') {
      runAllTests();
    }
  }
  delay(20);
}
