/*
  06_NonBlocking - อ่านค่าโดยไม่หยุดโปรแกรม

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  measure() แบบธรรมดาจะ delay() รอชิปวัดเสร็จ (สูงสุด 10 ms)
  ถ้างานของเราต้องกะพริบไฟ อ่านปุ่ม หรือขับจอไปด้วย การหยุด 10 ms ก็มากเกินไป

  ไลบรารีมีสองทางเลือกที่ไม่บล็อก
    1. คุมเองสามขั้น  startMeasurement() -> isMeasurementReady() -> readMeasurement()
    2. ให้ไลบรารีคุมให้  setUpdateInterval() แล้วเรียก update() รัว ๆ ใน loop()

  ตัวอย่างนี้ทำทั้งสองอย่างพร้อมกัน โดยมีไฟ LED กะพริบทุก 100 ms เป็นตัวพิสูจน์
  ว่าโปรแกรมไม่เคยหยุด และมีตัวนับรอบ loop() ให้เห็นว่าวิ่งได้กี่หมื่นรอบต่อวินาที

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

uint32_t loopCount = 0;
uint32_t lastBlinkMs = 0;
uint32_t lastReportMs = 0;
bool ledState = false;

/* callback จะถูกเรียกจาก update() ทุกครั้งที่ได้ค่าใหม่ */
void onNewReading(const massmore_sht4x_reading_t &reading) {
  Serial.print("[callback] ");
  Serial.print(reading.temperature, 2);
  Serial.print(" C  ");
  Serial.print(reading.humidity, 2);
  Serial.print(" %RH  ที่เวลา ");
  Serial.print(reading.timestampMs);
  Serial.println(" ms");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 06 โหมดไม่บล็อก");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  /* --- วิธีที่ 1: คุมเองสามขั้น --- */
  Serial.println();
  Serial.println("วิธีที่ 1 คุมเองสามขั้น");
  sensor.startMeasurement();
  uint32_t spins = 0;
  while (!sensor.isMeasurementReady()) {
    spins++; /* ตรงนี้เอาไปทำงานอย่างอื่นได้ */
  }
  float t = 0.0f;
  float h = 0.0f;
  if (sensor.readMeasurement(&t, &h)) {
    Serial.print("  ระหว่างรอ วนได้ ");
    Serial.print(spins);
    Serial.print(" รอบ แล้วได้ ");
    Serial.print(t, 2);
    Serial.print(" C  ");
    Serial.print(h, 2);
    Serial.println(" %RH");
  }

  /* --- วิธีที่ 2: ให้ไลบรารีคุมให้ --- */
  sensor.setUpdateInterval(2000); /* วัดทุก 2 วินาที */
  sensor.setCallback(onNewReading);

  Serial.println();
  Serial.println("วิธีที่ 2 เรียก update() ใน loop() ไฟจะกะพริบตลอดโดยไม่สะดุด");
  Serial.println();
}

void loop() {
  loopCount++;

  /* งานหลักของเรา: กะพริบไฟทุก 100 ms */
  if (millis() - lastBlinkMs >= 100) {
    lastBlinkMs = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  /*
    หนึ่งบรรทัดนี้จัดการทุกอย่างให้ ทั้งสั่งวัด รอ และเก็บผล
    คืน true เฉพาะรอบที่ได้ค่าใหม่ ซึ่งเป็นจังหวะเดียวกับที่ callback ถูกเรียก
  */
  sensor.update();

  /* รายงานจำนวนรอบ loop() ทุก 2 วินาที ให้เห็นว่าไม่มีการหยุดค้าง */
  if (millis() - lastReportMs >= 2000) {
    lastReportMs = millis();
    Serial.print("loop() วิ่งไป ");
    Serial.print(loopCount);
    Serial.print(" รอบ  โหมดปัจจุบัน = ");
    switch (sensor.getMode()) {
    case MASSMORE_SHT4X_MODE_READY:
      Serial.println("READY");
      break;
    case MASSMORE_SHT4X_MODE_MEASURING:
      Serial.println("MEASURING");
      break;
    case MASSMORE_SHT4X_MODE_HEATING:
      Serial.println("HEATING");
      break;
    default:
      Serial.println("IDLE");
      break;
    }
    loopCount = 0;
  }
}
