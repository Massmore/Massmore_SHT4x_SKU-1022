/*
  01_BasicRead — อ่านอุณหภูมิและความชื้นแบบง่ายที่สุด (Simple Blocking API)

  บอร์ด : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F (Outdoor)
  ร้าน  : https://www.massmore.shop

  Wiring (ESP32 Classic — ใช้สาย Qwiic ได้เลย)
    VIN -> 3V3 (หรือ 5V)      บอร์ดมี 3.3 V LDO ในตัว
    GND -> GND
    SDA -> GPIO 21
    SCL -> GPIO 22
    3Vo -> ขา OUTPUT 3.3 V ห้ามจ่ายไฟเข้า

  Wiring (Arduino Nano / ATmega328P)
    VIN -> 5V, GND -> GND, SDA -> A4, SCL -> A5   (บอร์ดรับ 5 V ได้)

  I2C address ของบอร์ด Massmore คือ 0x44 เสมอ (SHT4x ไม่มีขา ADDR)

  หลักการ: sketch เป็นเจ้าของ I2C Bus (เรียก Wire.begin() เอง)
  ไลบรารีไม่ hardcode GPIO และไม่แตะ Bus lifecycle

  by Massmore | MIT License
*/

#include <Massmore_SHT4x.h>

Massmore_SHT4x sensor;  // ใช้ Wire (I2C Bus หลัก)

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // รอ USB CDC (ESP32-S3 / Nano ที่มี USB ในตัว) ไม่เกิน 3 วินาที
  }

  // sketch เป็นผู้เริ่ม Bus: ESP32 เลือกขาได้ ส่วน AVR ใช้ขา hardware (A4/A5)
#if defined(ESP32)
  Wire.begin(21, 22);  // SDA, SCL
#else
  Wire.begin();
#endif
  Wire.setClock(100000);  // 100 kHz เหมาะกับสาย Qwiic ยาว; ชิปรองรับถึง 1 MHz

  Serial.println();
  Serial.println(F("Massmore_SHT4x - 01_BasicRead"));

  // ระบุรุ่นตาม silkscreen (SHT 40 / 41 / 45) ใช้แสดง accuracy spec เท่านั้น
  if (!sensor.begin(Massmore_SHT4x::I2C_ADDR_DEFAULT, Massmore_SHT4x::Variant::SHT40)) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    Serial.println(F("Check VIN / GND / SDA / SCL then press RESET"));
    while (true) {
      delay(1000);
    }
  }

  Serial.print(F("Found "));
  Serial.print(sensor.getVariantName());
  Serial.print(F(" at 0x"));
  Serial.print(sensor.getAddress(), HEX);
  Serial.print(F("  Serial Number: 0x"));
  Serial.println(sensor.getSerialNumber(), HEX);
  Serial.println();
}

void loop() {
  float temperature = 0.0f;
  float humidity = 0.0f;

  // วัดครั้งเดียวได้ทั้งสองค่า (ประหยัดกว่าเรียก readTemperature() + readHumidity())
  if (sensor.readAll(temperature, humidity)) {
    Serial.print(F("Temperature: "));
    Serial.print(temperature, 2);
    Serial.print(F(" C   Humidity: "));
    Serial.print(humidity, 2);
    Serial.println(F(" %RH"));
  } else {
    Serial.print(F("Read failed: "));
    Serial.println(sensor.lastErrorString());
  }

  delay(1000);
}
