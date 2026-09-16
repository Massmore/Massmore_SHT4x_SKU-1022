/*
  02_CustomPins_BusRemap — ย้ายขา I2C และใช้ Bus ที่สอง (Wire1) บน ESP32 Core 3.x

  บอร์ด : Massmore SHT4X (SKU-1022)
  ร้าน  : https://www.massmore.shop

  ESP32 / ESP32-S3 มี GPIO Matrix จึงกำหนด SDA/SCL เป็นขาไหนก็ได้
  และมี I2C Controller 2 ชุด (Wire, Wire1) ใช้แยก Bus ได้ เช่น
  เอา SHT4x ไว้บน Wire1 ส่วน OLED / RTC ไว้บน Wire

  ตัวอย่างนี้
    ESP32 Classic : SHT4x บน Wire1  SDA = GPIO 25, SCL = GPIO 26
    ESP32-S3      : SHT4x บน Wire1  SDA = GPIO 17, SCL = GPIO 18
    AVR Nano      : ไม่มี GPIO Matrix ใช้ Wire ขา hardware A4/A5 เท่านั้น

  ESP32 Core 3.x API ที่ใช้: Wire1.begin(sda, scl) / Wire1.setClock(hz)

  by Massmore | MIT License
*/

#include <Massmore_SHT4x.h>

#if defined(ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define PIN_SDA 17
#define PIN_SCL 18
#else
#define PIN_SDA 25
#define PIN_SCL 26
#endif
TwoWire &sensorBus = Wire1;  // Bus ที่สอง
#else
// AVR (ATmega328P) มี I2C Controller ชุดเดียว ขาตายตัว A4 (SDA) / A5 (SCL)
TwoWire &sensorBus = Wire;
#endif

// Inject Bus ผ่าน constructor — ไลบรารีไม่เรียก begin() ของ Bus เอง
Massmore_SHT4x sensor(sensorBus);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
  Serial.println();
  Serial.println(F("Massmore_SHT4x - 02_CustomPins_BusRemap"));

#if defined(ESP32)
  sensorBus.begin(PIN_SDA, PIN_SCL);
  Serial.print(F("ESP32 Wire1  SDA=GPIO"));
  Serial.print(PIN_SDA);
  Serial.print(F("  SCL=GPIO"));
  Serial.println(PIN_SCL);
#else
  sensorBus.begin();
  Serial.println(F("AVR Wire  SDA=A4  SCL=A5 (fixed hardware pins)"));
#endif
  sensorBus.setClock(400000);  // Fast-mode 400 kHz (ชิปรองรับถึง 1 MHz)

  if (!sensor.begin()) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }
  Serial.print(F("Serial Number: 0x"));
  Serial.println(sensor.getSerialNumber(), HEX);
}

void loop() {
  Massmore_SHT4x::Readings r;
  if (sensor.readAll(r, Massmore_SHT4x::Precision::MEDIUM_RES)) {
    Serial.print(F("T="));
    Serial.print(r.temperature, 2);
    Serial.print(F(" C  RH="));
    Serial.print(r.humidity, 2);
    Serial.print(F(" %  raw=0x"));
    Serial.print(r.rawTemperature, HEX);
    Serial.print(F("/0x"));
    Serial.println(r.rawHumidity, HEX);
  } else {
    Serial.print(F("Read failed: "));
    Serial.println(sensor.lastErrorString());
  }
  delay(1000);
}
