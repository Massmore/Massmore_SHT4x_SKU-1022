/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  02_Precision_Timing - เปรียบเทียบความละเอียดทั้งสามระดับ

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  SHT4x มีคำสั่งวัดสามตัว ต่างกันที่เวลาและ noise
    0xFD  ความละเอียดสูง   ประมาณ 6.9 ms   noise ต่ำสุด
    0xF6  ความละเอียดกลาง  ประมาณ 3.7 ms
    0xE0  ความละเอียดต่ำ   ประมาณ 1.3 ms   กินไฟน้อยสุด

  ตัวอย่างนี้วัดซ้ำแต่ละระดับหลายครั้ง แล้วรายงาน
    - เวลาที่ใช้จริง (ไมโครวินาที)
    - ส่วนเบี่ยงเบนมาตรฐาน เพื่อให้เห็นว่า noise ต่างกันจริง

  เลือกใช้อย่างไร
    - งานทั่วไปที่วัดวินาทีละครั้ง ใช้ HIGH ไปเลย ต่างกันไม่กี่มิลลิวินาที
    - งานใช้แบตเตอรี่ที่ตื่นมาวัดแล้วหลับต่อ ใช้ LOW ประหยัดพลังงานได้จริง
    - งานที่ต้องการค่าเนียนสำหรับกราฟ ใช้ HIGH แล้วเฉลี่ยหลายครั้ง

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

#define SAMPLES 20

/* วัดซ้ำ SAMPLES ครั้งด้วยความละเอียดที่กำหนด แล้วสรุปผล */
void benchmark(massmore_sht4x_precision_t precision) {
  float sumT = 0.0f;
  float sumRH = 0.0f;
  float sumT2 = 0.0f;
  uint32_t totalMicros = 0;
  uint16_t ok = 0;

  for (uint16_t i = 0; i < SAMPLES; i++) {
    float t = 0.0f;
    float h = 0.0f;
    uint32_t start = micros();
    bool good = sensor.measureWith(precision, &t, &h);
    uint32_t elapsed = micros() - start;

    if (good) {
      ok++;
      sumT += t;
      sumT2 += t * t;
      sumRH += h;
      totalMicros += elapsed;
    }
    delay(20);
  }

  Serial.print("  ");
  Serial.print(MassmoreSHT4x::precisionToString(precision));
  if (ok == 0) {
    Serial.println("  อ่านไม่สำเร็จเลย");
    return;
  }

  float meanT = sumT / ok;
  float variance = (sumT2 / ok) - (meanT * meanT);
  if (variance < 0.0f) {
    variance = 0.0f;
  }

  Serial.print("\tเฉลี่ย ");
  Serial.print(meanT, 3);
  Serial.print(" C  ");
  Serial.print(sumRH / ok, 2);
  Serial.print(" %RH\tsd ");
  Serial.print(sqrtf(variance), 4);
  Serial.print(" C\tเวลาเฉลี่ย ");
  Serial.print(totalMicros / ok);
  Serial.println(" us");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 02 Precision & Timing");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }
  Serial.println();
}

void loop() {
  Serial.print("วัดระดับละ ");
  Serial.print(SAMPLES);
  Serial.println(" ครั้ง");

  benchmark(MASSMORE_SHT4X_PRECISION_LOW);
  benchmark(MASSMORE_SHT4X_PRECISION_MEDIUM);
  benchmark(MASSMORE_SHT4X_PRECISION_HIGH);

  Serial.println();
  Serial.println("sd ยิ่งน้อยแปลว่าค่ายิ่งนิ่ง ลองเป่าลมใส่แล้วดูว่าค่าไหนไวกว่ากัน");
  Serial.println();
  delay(5000);
}
