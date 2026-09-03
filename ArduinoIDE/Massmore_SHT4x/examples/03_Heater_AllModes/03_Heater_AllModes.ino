/*
  03_Heater_AllModes - ฮีตเตอร์ในตัวชิปครบทั้ง 6 โหมด

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  SHT4x มีฮีตเตอร์ในตัว 3 ระดับกำลัง คูณ 2 ระยะเวลา รวมเป็น 6 โหมด
    200 mW  1 s / 0.1 s      แรงสุด ใช้ไล่หยดน้ำที่เกาะหน้าเซ็นเซอร์
    110 mW  1 s / 0.1 s      กลาง ๆ
     20 mW  1 s / 0.1 s      เบาสุด ใช้กันค่าเลื่อนตอนอยู่ในที่ชื้นจัดนาน ๆ

  เรื่องที่ต้องรู้
    - ทุกคำสั่งฮีตเตอร์จะ "วัดหนึ่งครั้งก่อนดับฮีตเตอร์" ให้อัตโนมัติ
      ค่าที่ได้ตอนนั้นร้อนกว่าความจริง ห้ามเอาไปรายงานเป็นอุณหภูมิห้อง
    - datasheet กำหนดว่าฮีตเตอร์ใช้ได้ที่ duty cycle ต่ำกว่า 10%
      ไลบรารีมีตัวกันเผลอให้แล้ว ถ้ายิงถี่เกินจะคืน ERR_HEATER_DUTY
    - หลังยิงฮีตเตอร์ต้องรอให้เย็นก่อนวัดค่าจริง ใช้ measureAfterHeating()

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

const massmore_sht4x_heater_t MODES[6] = {
    MASSMORE_SHT4X_HEATER_20MW_0S1,  MASSMORE_SHT4X_HEATER_20MW_1S,
    MASSMORE_SHT4X_HEATER_110MW_0S1, MASSMORE_SHT4X_HEATER_110MW_1S,
    MASSMORE_SHT4X_HEATER_200MW_0S1, MASSMORE_SHT4X_HEATER_200MW_1S};

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 03 Heater ครบทุกโหมด");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT40,
                    21, 22)) {
    Serial.print("ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  Serial.println("จะไล่ยิงทีละโหมด พร้อมพักระหว่างโหมดเพื่อรักษา duty cycle");
  Serial.println();
}

void loop() {
  for (uint8_t i = 0; i < 6; i++) {
    massmore_sht4x_heater_t mode = MODES[i];

    /* วัดค่าตอนเย็นไว้ก่อนเป็นค่าอ้างอิง */
    float coldT = 0.0f;
    float coldRH = 0.0f;
    if (!sensor.measure(&coldT, &coldRH)) {
      Serial.print("วัดค่าก่อนยิงไม่สำเร็จ: ");
      Serial.println(sensor.lastErrorString());
      continue;
    }

    Serial.print(MassmoreSHT4x::heaterToString(mode));
    Serial.print("\t(");
    Serial.print(MassmoreSHT4x::heaterPowerMilliwatt(mode));
    Serial.print(" mW)\tก่อน ");
    Serial.print(coldT, 2);
    Serial.print(" C ");
    Serial.print(coldRH, 1);
    Serial.print(" %RH");

    /*
      ตัวอย่างนี้ยิงติดกันเพื่อการสาธิต จึงปิดตัวกันเผลอชั่วคราว
      แล้วพักเองระหว่างโหมด งานจริงควรเปิดตัวกันเผลอทิ้งไว้
    */
    sensor.setHeaterDutyGuard(false);

    massmore_sht4x_reading_t hot;
    if (sensor.runHeater(mode, &hot)) {
      Serial.print("\t->  ตอนร้อน ");
      Serial.print(hot.temperature, 2);
      Serial.print(" C ");
      Serial.print(hot.humidity, 1);
      Serial.print(" %RH\tขึ้น ");
      Serial.print(hot.temperature - coldT, 2);
      Serial.println(" C");
    } else {
      Serial.print("\t->  ยิงไม่สำเร็จ: ");
      Serial.println(sensor.lastErrorString());
    }

    sensor.setHeaterDutyGuard(true);

    /* พักให้เซ็นเซอร์เย็นและรักษา duty cycle ให้ต่ำกว่า 10% */
    delay(12000);
  }

  /* วัดค่าจริงหลังเย็นสนิท */
  massmore_sht4x_reading_t settled;
  if (sensor.measureAfterHeating(3000, settled)) {
    Serial.print("ค่าหลังเย็นสนิท ");
    Serial.print(settled.temperature, 2);
    Serial.print(" C ");
    Serial.print(settled.humidity, 1);
    Serial.println(" %RH");
  }

  Serial.print("duty cycle ที่ใช้ไปทั้งหมด ");
  Serial.print(sensor.getHeaterDutyPercent(), 2);
  Serial.print(" %  (ฮีตเตอร์ทำงานรวม ");
  Serial.print(sensor.getHeaterOnTimeMs());
  Serial.println(" ms)");
  Serial.println();

  delay(20000);
}
