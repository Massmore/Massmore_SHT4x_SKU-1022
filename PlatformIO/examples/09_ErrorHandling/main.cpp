/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  09_ErrorHandling - จับข้อผิดพลาดและกู้คืนอัตโนมัติ

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  งานที่ต้องเดินยาว ๆ ต้องรับมือกับ
    - สายหลุดหรือหลวมชั่วขณะ
    - สัญญาณรบกวนทำให้ CRC ไม่ตรง
    - ไฟตกจนชิปรีบูตเอง

  ไลบรารีแยกรหัสข้อผิดพลาดไว้ 12 แบบ พร้อมคำอธิบายภาษาไทย
  ตัวอย่างนี้แสดง
    1. การอ่านรหัสจริงจาก lastError() ไม่ใช่แค่ดูว่า false
    2. การนับสถิติข้อผิดพลาดสะสม
    3. การกู้คืนแบบไล่ระดับ  ลองใหม่ -> soft reset -> begin() ใหม่ทั้งหมด
    4. การจงใจสร้างข้อผิดพลาดเพื่อดูว่าโค้ดตอบสนองถูกไหม

  ลองทดสอบจริงด้วยการดึงสาย SDA ออกตอนโปรแกรมกำลังรัน แล้วเสียบกลับ

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

uint32_t totalReads = 0;
uint32_t totalErrors = 0;
uint32_t recoveries = 0;
uint8_t consecutiveErrors = 0;
bool online = false;

void printError(const char *stage) {
  Serial.print("  [");
  Serial.print(stage);
  Serial.print("] รหัส ");
  Serial.print((int)sensor.lastError());
  Serial.print(" : ");
  Serial.println(sensor.lastErrorString());
}

/* กู้คืนแบบไล่ระดับ เริ่มจากเบาที่สุดก่อน */
bool recover() {
  Serial.println("  เริ่มขั้นตอนกู้คืน");

  /* ขั้นที่ 1 - ชิปยังตอบบนบัสไหม */
  if (sensor.isConnected()) {
    Serial.println("  ขั้นที่ 1 ชิปยังตอบอยู่ ลอง soft reset");
    if (sensor.softReset() && sensor.measure(nullptr, nullptr)) {
      Serial.println("  กู้คืนสำเร็จด้วย soft reset");
      recoveries++;
      return true;
    }
  } else {
    printError("isConnected");
  }

  /* ขั้นที่ 2 - เริ่มต้นบัสใหม่ทั้งหมด */
  Serial.println("  ขั้นที่ 2 เริ่มต้น begin() ใหม่ทั้งหมด");
  if (sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40, 21,
                   22)) {
    Serial.println("  กู้คืนสำเร็จด้วยการ begin() ใหม่");
    recoveries++;
    return true;
  }
  printError("begin");

  Serial.println("  กู้คืนไม่สำเร็จ ตรวจสายไฟและสาย I2C");
  return false;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 09 การจัดการข้อผิดพลาด");
  Serial.println();

  /* --- สาธิตข้อผิดพลาดที่ตรวจได้ทันทีโดยไม่ต้องมีฮาร์ดแวร์ผิดปกติ --- */
  Serial.println("ทดสอบการตรวจพารามิเตอร์");

  MassmoreSHT4x probe;
  if (!probe.measure(nullptr, nullptr)) {
    Serial.print("  วัดก่อน begin() -> ");
    Serial.println(probe.lastErrorString());
  }
  if (!probe.begin(0x50)) {
    Serial.print("  address 0x50 -> ");
    Serial.println(probe.lastErrorString());
  }
  if (!probe.readSerialNumber(nullptr)) {
    Serial.print("  ส่ง nullptr -> ");
    Serial.println(probe.lastErrorString());
  }

  Serial.println();
  Serial.println("ตารางรหัสข้อผิดพลาดทั้งหมด");
  for (int i = 0; i <= (int)MASSMORE_SHT4X_ERR_HEATER_DUTY; i++) {
    Serial.print("  ");
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(MassmoreSHT4x::errorToString((massmore_sht4x_error_t)i));
  }
  Serial.println();

  online = sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                        21, 22);
  if (!online) {
    printError("begin");
    Serial.println("จะพยายามกู้คืนเองใน loop()");
  } else {
    Serial.println("เริ่มทำงานปกติ ลองดึงสาย SDA ออกดูได้เลย");
  }
  Serial.println();
}

void loop() {
  totalReads++;

  float t = 0.0f;
  float h = 0.0f;

  if (online && sensor.measure(&t, &h)) {
    consecutiveErrors = 0;
    Serial.print("ครั้งที่ ");
    Serial.print(totalReads);
    Serial.print("  ");
    Serial.print(t, 2);
    Serial.print(" C  ");
    Serial.print(h, 2);
    Serial.print(" %RH   (ผิดพลาดสะสม ");
    Serial.print(totalErrors);
    Serial.print(" ครั้ง  กู้คืน ");
    Serial.print(recoveries);
    Serial.println(" ครั้ง)");
  } else {
    totalErrors++;
    consecutiveErrors++;
    Serial.print("ครั้งที่ ");
    Serial.print(totalReads);
    Serial.println("  อ่านไม่สำเร็จ");
    printError("measure");

    /* CRC ผิดครั้งเดียวมักเป็นสัญญาณรบกวนชั่วคราว ลองใหม่ก่อน */
    if (sensor.lastError() == MASSMORE_SHT4X_ERR_CRC && consecutiveErrors < 3) {
      Serial.println("  CRC ผิดชั่วคราว ลองอ่านใหม่ในรอบถัดไป");
    } else if (consecutiveErrors >= 3) {
      online = recover();
      consecutiveErrors = 0;
    }
  }

  delay(1000);
}
