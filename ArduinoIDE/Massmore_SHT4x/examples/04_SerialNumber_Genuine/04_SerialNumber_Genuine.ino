/*
  04_SerialNumber_Genuine - อ่านซีเรียลจากโรงงาน และตรวจว่าเป็นชิปแท้

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  ชิป SHT4x ทุกตัวมีหมายเลขซีเรียล 32 บิตที่โรงงาน Sensirion เขียนไว้
  อ่านได้ด้วยคำสั่ง 0x89 ใช้เป็นรหัสประจำตัวของบอร์ดได้เลย ไม่ซ้ำกัน

  verifyChip() ตรวจพฤติกรรม 10 ข้อที่ของเลียนแบบมักทำไม่ครบ
  ที่สำคัญที่สุดคือข้อ 8: SHT4x แท้ต้อง "NACK" เมื่อถูกอ่านตอนยังวัดไม่เสร็จ
  เพราะชิปไม่รองรับ clock stretching ของปลอมที่ทำเป็นตารางค่าคงที่จะตอบทันที

  ขอบเขตที่ต้องเข้าใจ
    นี่คือการตรวจเชิงพฤติกรรมระดับโปรโตคอล ไม่ใช่ลายเซ็นดิจิทัล
    ตอบได้แค่ว่า "ชิปตัวนี้ทำตัวเหมือน SHT4x แท้ทุกประการหรือไม่"
    บอร์ด Massmore ใช้ชิปแท้จาก Sensirion ประกอบในประเทศไทย

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

void printVerifyDetail() {
  uint16_t mask = sensor.getVerifyMask();
  for (uint8_t i = 0; i < MASSMORE_SHT4X_CHK_COUNT; i++) {
    Serial.print("  ");
    Serial.print((mask & (1u << i)) ? "[ผ่าน]" : "[ตก ]");
    Serial.print(" ");
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.println(MassmoreSHT4x::getVerifyCheckName(i));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 04 ซีเรียลและการตรวจของแท้");
  Serial.println("==========================================================");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  /* --- ซีเรียลจากโรงงาน --- */
  uint32_t serial = 0;
  if (sensor.readSerialNumber(&serial)) {
    Serial.print("ซีเรียลจากโรงงาน : 0x");
    if (serial < 0x10000000UL) {
      Serial.print("0"); /* เติมศูนย์หน้าให้ครบ 8 หลัก */
    }
    Serial.println(serial, HEX);
    Serial.print("เลขฐานสิบ        : ");
    Serial.println(serial);
  } else {
    Serial.print("อ่านซีเรียลไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
  }

  Serial.print("I2C address      : 0x");
  Serial.println(sensor.getAddress(), HEX);
  Serial.print("ไลบรารีเวอร์ชัน   : ");
  Serial.println(MassmoreSHT4x::getLibraryVersion());
  Serial.println();

  /* --- ตรวจของแท้ --- */
  Serial.println("กำลังตรวจพฤติกรรม 10 ข้อ ใช้เวลาสักครู่...");
  massmore_sht4x_genuine_t result = sensor.verifyChip();
  printVerifyDetail();

  Serial.println();
  Serial.print("ผ่าน ");
  Serial.print(sensor.getVerifyPassCount());
  Serial.print("/10 ข้อ  ->  ");
  Serial.println(MassmoreSHT4x::genuineToString(result));

  if (result == MASSMORE_SHT4X_GENUINE_PASS) {
    Serial.println("ชิปตัวนี้ตอบสนองครบทุกข้อตาม datasheet ของ Sensirion");
  } else if (result == MASSMORE_SHT4X_GENUINE_NOT_SHT4X) {
    Serial.println("มีอุปกรณ์ตอบที่ address นี้ แต่ไม่ใช่ SHT4x");
    Serial.println("ชิปตระกูลอื่นที่ใช้ 0x44 เหมือนกัน เช่น SHT3x หรือ AHT2x");
    Serial.println("จะรับคำสั่งแบบ 1 ไบต์ของ SHT4x ไม่ได้ จึงตกข้อซีเรียลกับ CRC");
  } else {
    Serial.println("ตกบางข้อ ลองใช้สายสั้นลงแล้วรีเซ็ตทดสอบใหม่");
    Serial.println("ถ้ายังตกข้อเดิม และซื้อจาก Massmore รบกวนแจ้งเคลมได้เลย");
  }
  Serial.println("==========================================================");
  Serial.println();
}

void loop() {
  float t = 0.0f;
  float h = 0.0f;
  if (sensor.measure(&t, &h)) {
    Serial.print("ซีเรียล 0x");
    Serial.print(sensor.getSerialNumber(), HEX);
    Serial.print("  ");
    Serial.print(t, 2);
    Serial.print(" C  ");
    Serial.print(h, 2);
    Serial.println(" %RH");
  }
  delay(2000);
}
