/*
  04_Heater_DewPoint — Heater ในตัวชิป, duty-cycle guard และค่าที่คำนวณต่อ

  บอร์ด : Massmore SHT4X (SKU-1022)
  ร้าน  : https://www.massmore.shop

  SHT4x มี Heater 6 โหมด (200/110/20 mW × 1 s/0.1 s) ใช้เพื่อ
    - ไล่หยดน้ำที่เกาะหน้าเซ็นเซอร์ (ค่าค้าง 100 %RH)
    - ลด creep ของค่าความชื้นเมื่ออยู่ในที่ชื้นจัดนาน ๆ
  datasheet กำหนดให้ใช้ที่ duty cycle < 10 % ไลบรารีมี guard ให้:
  ถ้ายิงถี่เกิน runHeater() จะคืน false พร้อม ErrorCode::HEATER_DUTY

  ค่าที่วัดตอน Heater ทำงาน "ร้อนกว่าจริง" ห้ามนำไปรายงาน
  ตัวอย่างนี้ยิง Heater ทุก 30 วินาที และแสดง dew point / absolute humidity / heat index

  Wiring: ESP32 SDA=21 SCL=22 | Nano SDA=A4 SCL=A5

  by Massmore | MIT License
*/

#include <Massmore_SHT4x.h>

#define HEATER_PERIOD_MS 30000UL

Massmore_SHT4x sensor;
uint32_t lastHeaterMs = 0;

void printReading(const Massmore_SHT4x::Readings &r) {
  Serial.print(F("T="));
  Serial.print(r.temperature, 2);
  Serial.print(F(" C  RH="));
  Serial.print(r.humidity, 2);
  Serial.print(F(" %  DewPoint="));
  Serial.print(Massmore_SHT4x::dewPoint(r.temperature, r.humidity), 2);
  Serial.print(F(" C  AbsHumi="));
  Serial.print(Massmore_SHT4x::absoluteHumidity(r.temperature, r.humidity), 2);
  Serial.print(F(" g/m3  HeatIndex="));
  Serial.print(Massmore_SHT4x::heatIndex(r.temperature, r.humidity), 2);
  Serial.println(F(" C"));
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
#if defined(ESP32)
  Wire.begin(21, 22);
#else
  Wire.begin();
#endif
  Serial.println();
  Serial.println(F("Massmore_SHT4x - 04_Heater_DewPoint"));
  if (!sensor.begin()) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }
  lastHeaterMs = millis() - HEATER_PERIOD_MS;  // ให้ยิงครั้งแรกทันที
}

void loop() {
  Massmore_SHT4x::Readings r;

  if ((uint32_t)(millis() - lastHeaterMs) >= HEATER_PERIOD_MS) {
    lastHeaterMs = millis();
    Serial.println(F("--- Heater 200 mW / 1 s (Blocking ~1.1 s) ---"));
    Massmore_SHT4x::Readings hot;
    if (sensor.runHeater(Massmore_SHT4x::HeaterMode::MW200_1S, &hot)) {
      Serial.print(F("While heating (do not report): "));
      printReading(hot);
      Serial.print(F("Cooldown required before next pulse: "));
      Serial.print(sensor.heaterCooldownRemainingMs());
      Serial.println(F(" ms"));
    } else {
      Serial.print(F("Heater refused: "));
      Serial.println(sensor.lastErrorString());
    }
    delay(3000);  // รอให้เย็นก่อนวัดค่าจริง
  }

  if (sensor.readAll(r)) {
    printReading(r);
  } else {
    Serial.print(F("Read failed: "));
    Serial.println(sensor.lastErrorString());
  }
  delay(2000);
}
