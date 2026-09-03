/*
  08_LowPower_DeepSleep - วัดแล้วหลับ ใช้กับงานแบตเตอรี่

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  SHT4x กินไฟตอนไม่ทำอะไรแค่ 80 nA และเฉลี่ยประมาณ 0.4 uA เมื่อวัด
  วินาทีละครั้งด้วยความละเอียดต่ำ ตัวมันเองแทบไม่ใช้ไฟเลย
  ตัวที่กินไฟจริงคือ ESP32 เพราะฉะนั้นสูตรประหยัดคือ
      ตื่น -> วัดเร็วที่สุด -> หลับต่อ

  เทคนิคที่ใช้ในตัวอย่างนี้
    - ใช้ความละเอียดต่ำ (0xE0) ใช้เวลาแค่ ~1.3 ms
    - ไม่เรียก verifyChip() ตอนตื่น เพราะกินเวลาหลายวินาที
    - เก็บค่าล่าสุดไว้ใน RTC memory ที่รอดจาก deep sleep
    - นับจำนวนครั้งที่ตื่น และเทียบค่ากับครั้งก่อนเพื่อดูแนวโน้ม

  ตัวเลขที่ควรรู้
    ESP32 ตอนตื่นเต็มกำลังกินประมาณ 80 mA ตอน deep sleep ประมาณ 10 uA
    ตื่นวัดครั้งละ 100 ms ทุก 60 วินาที จะเฉลี่ยราว 150 uA
    แบตเตอรี่ 18650 (2500 mAh) จึงอยู่ได้ประมาณหนึ่งปี

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

#define SLEEP_SECONDS 60

MassmoreSHT4x sensor;

#if defined(ARDUINO_ARCH_ESP32)
/* ตัวแปรใน RTC memory รอดจาก deep sleep แต่หายเมื่อถอดไฟ */
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR float lastTemperature = 0.0f;
RTC_DATA_ATTR float lastHumidity = 0.0f;
RTC_DATA_ATTR bool hasPrevious = false;
#else
uint32_t bootCount = 0;
float lastTemperature = 0.0f;
float lastHumidity = 0.0f;
bool hasPrevious = false;
#endif

void setup() {
  uint32_t startMs = millis();

  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
  }

  bootCount++;
  Serial.println();
  Serial.print("Massmore SHT4X - ตื่นครั้งที่ ");
  Serial.println(bootCount);

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
  } else {
    /* ความละเอียดต่ำ เร็วและประหยัดที่สุด */
    sensor.setPrecision(MASSMORE_SHT4X_PRECISION_LOW);

    float t = 0.0f;
    float h = 0.0f;
    if (sensor.measure(&t, &h)) {
      Serial.print("อุณหภูมิ ");
      Serial.print(t, 2);
      Serial.print(" C   ความชื้น ");
      Serial.print(h, 2);
      Serial.println(" %RH");

      if (hasPrevious) {
        Serial.print("เทียบครั้งก่อน  ");
        Serial.print(t - lastTemperature, 2);
        Serial.print(" C   ");
        Serial.print(h - lastHumidity, 2);
        Serial.println(" %RH");
      }

      lastTemperature = t;
      lastHumidity = h;
      hasPrevious = true;

      /*
        ตรงนี้คือจุดที่ควรส่งค่าขึ้น WiFi / LoRa / SD card
        ตัวอย่างนี้แค่พิมพ์ออก Serial เพื่อให้ไม่มี dependency เพิ่ม
      */
    } else {
      Serial.print("อ่านค่าไม่สำเร็จ: ");
      Serial.println(sensor.lastErrorString());
    }
  }

  Serial.print("ใช้เวลาตื่นทั้งหมด ");
  Serial.print(millis() - startMs);
  Serial.println(" ms");

#if defined(ARDUINO_ARCH_ESP32)
  Serial.print("หลับต่ออีก ");
  Serial.print(SLEEP_SECONDS);
  Serial.println(" วินาที");
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
  /* โค้ดหลังบรรทัดนี้จะไม่ถูกรัน ตื่นมาแล้วเริ่มที่ setup() ใหม่ */
#else
  Serial.println("บอร์ดนี้ไม่ใช่ ESP32 จะใช้ delay() แทน deep sleep");
#endif
}

void loop() {
#if !defined(ARDUINO_ARCH_ESP32)
  delay((uint32_t)SLEEP_SECONDS * 1000UL);
  setup();
#endif
}
