/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  01_BasicReading - อ่านอุณหภูมิและความชื้นแบบง่ายที่สุด

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย (ESP32 + สาย Qwiic หรือสายจัมเปอร์)
    VIN  -> 3.3V หรือ 5V   (บอร์ดมี regulator กับ level shifter ในตัว)
    GND  -> GND
    SDA  -> GPIO 21
    SCL  -> GPIO 22
    3Vo  -> ขา OUTPUT 3.3V ห้ามจ่ายไฟเข้า

  I2C address ของบอร์ด Massmore คือ 0x44 เสมอ (SHT4x ไม่มีขา ADDR ให้เปลี่ยน)

  ตัวอย่างนี้แสดงสิ่งที่ต้องใช้อย่างน้อยที่สุดสามบรรทัด
    1. ประกาศอ็อบเจกต์
    2. begin()
    3. measure()

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    /* รอ USB CDC บนบอร์ดที่ใช้ USB ในตัว ไม่เกิน 3 วินาที */
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 01 Basic Reading");

  /*
    begin() พารามิเตอร์ทั้งหมดมีค่าเริ่มต้นให้แล้ว เรียกเปล่า ๆ ก็ได้
      begin(address, variant, sdaPin, sclPin, frequency)

    ระบุรุ่นตามช่องที่ติ๊กไว้บนบอร์ด (SHT 40 / 41 / 45)
    ค่านี้ใช้แค่แสดงผลกับกำหนดเกณฑ์ความแม่นยำ ไม่มีผลต่อการสื่อสาร
  */
  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    Serial.println("ตรวจสาย VIN / GND / SDA / SCL แล้วกดปุ่ม reset");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("พบเซ็นเซอร์ที่ 0x");
  Serial.print(sensor.getAddress(), HEX);
  Serial.print("  ซีเรียลจากโรงงาน 0x");
  Serial.println(sensor.getSerialNumber(), HEX);
  Serial.println();
}

void loop() {
  float temperature = 0.0f;
  float humidity = 0.0f;

  if (sensor.measure(&temperature, &humidity)) {
    Serial.print("อุณหภูมิ ");
    Serial.print(temperature, 2);
    Serial.print(" C   ความชื้น ");
    Serial.print(humidity, 2);
    Serial.println(" %RH");
  } else {
    Serial.print("อ่านค่าไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
  }

  delay(1000);
}
