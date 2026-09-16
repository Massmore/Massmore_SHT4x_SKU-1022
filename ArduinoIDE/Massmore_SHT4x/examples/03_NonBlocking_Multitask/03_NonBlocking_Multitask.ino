/*
  03_NonBlocking_Multitask — Non-blocking FSM API + งานอื่นทำงานพร้อมกัน

  บอร์ด : Massmore SHT4X (SKU-1022)
  ร้าน  : https://www.massmore.shop

  Blocking API (readAll) จะหยุดรอ ~7 ms ต่อครั้ง ซึ่งพอสำหรับงานทั่วไป
  แต่ถ้า loop() ต้องตอบสนองเร็ว (ขับ LED, อ่านปุ่ม, รับ Serial, RTOS task)
  ให้ใช้ FSM:
      requestConversion()  ->  update() ทุกรอบ loop  ->  isDataReady()  ->  getReadings()

  งานที่สอง: กะพริบ LED ทุก 100 ms และนับรอบ loop เพื่อพิสูจน์ว่าไม่ถูก Block

  Wiring: ESP32 SDA=21 SCL=22 | Nano SDA=A4 SCL=A5

  by Massmore | MIT License
*/

#include <Massmore_SHT4x.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // ESP32 DevKit ส่วนใหญ่มี LED ที่ GPIO 2
#endif

#define MEASURE_INTERVAL_MS 1000
#define BLINK_INTERVAL_MS 100

Massmore_SHT4x sensor;

uint32_t lastRequestMs = 0;
uint32_t lastBlinkMs = 0;
uint32_t loopCounter = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
  pinMode(LED_BUILTIN, OUTPUT);

#if defined(ESP32)
  Wire.begin(21, 22);
#else
  Wire.begin();
#endif

  Serial.println();
  Serial.println(F("Massmore_SHT4x - 03_NonBlocking_Multitask"));
  if (!sensor.begin()) {
    Serial.print(F("Sensor not found: "));
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  uint32_t now = millis();
  loopCounter++;

  /* ---- งานที่ 1: เซ็นเซอร์ (Non-blocking) ---- */
  if (sensor.getState() == Massmore_SHT4x::State::IDLE &&
      (uint32_t)(now - lastRequestMs) >= MEASURE_INTERVAL_MS) {
    lastRequestMs = now;
    if (!sensor.requestConversion(Massmore_SHT4x::Precision::HIGH_RES)) {
      Serial.print(F("request failed: "));
      Serial.println(sensor.lastErrorString());
    }
  }

  sensor.update();  // เรียกทุกรอบ ไม่ Block (คืนทันทีถ้ายังไม่ถึงเวลา)

  if (sensor.isDataReady()) {
    Massmore_SHT4x::Readings r;
    sensor.getReadings(r);
    Serial.print(F("T="));
    Serial.print(r.temperature, 2);
    Serial.print(F(" C  RH="));
    Serial.print(r.humidity, 2);
    Serial.print(F(" %   loops/sec="));
    Serial.println(loopCounter);
    loopCounter = 0;
  } else if (sensor.getState() == Massmore_SHT4x::State::IDLE &&
             sensor.lastError() != Massmore_SHT4x::ErrorCode::OK) {
    Serial.print(F("measurement error: "));
    Serial.println(sensor.lastErrorString());
    sensor.softReset();  // ล้าง error แล้วเริ่มใหม่รอบถัดไป
  }

  /* ---- งานที่ 2: กะพริบ LED (rollover-safe millis) ---- */
  if ((uint32_t)(now - lastBlinkMs) >= BLINK_INTERVAL_MS) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }
}
