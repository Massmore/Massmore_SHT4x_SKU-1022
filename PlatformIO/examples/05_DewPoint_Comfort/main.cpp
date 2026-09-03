/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  05_DewPoint_Comfort - จุดน้ำค้าง ความชื้นสัมบูรณ์ ดัชนีความร้อน และระดับความสบาย

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  อุณหภูมิกับความชื้นดิบ ๆ ยังไม่ค่อยบอกอะไร ค่าที่คำนวณต่อนี่แหละที่เอาไปใช้จริง

    จุดน้ำค้าง (dew point)
      อุณหภูมิที่ไอน้ำในอากาศจะเริ่มกลั่นตัว ใช้เตือนเรื่องฝ้าที่กระจก
      หยดน้ำในตู้ควบคุม หรือเชื้อราในห้อง ถ้าจุดน้ำค้างใกล้อุณหภูมิผิววัสดุ
      เมื่อไร แปลว่ากำลังจะมีหยดน้ำ

    ความชื้นสัมบูรณ์ (g/m3)
      ปริมาณไอน้ำจริง ๆ ต่ออากาศหนึ่งลูกบาศก์เมตร ใช้เทียบระหว่างห้องที่
      อุณหภูมิต่างกันได้ ต่างจาก %RH ที่เทียบกันตรง ๆ ไม่ได้

    ดัชนีความร้อน (heat index)
      อุณหภูมิที่ "รู้สึก" เมื่อรวมผลของความชื้นเข้าไปด้วย

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

/* จัดระดับความสบายอย่างง่ายจากจุดน้ำค้าง เกณฑ์ที่นักอุตุนิยมวิทยาใช้กันทั่วไป */
const char *comfortFromDewPoint(float dewPoint) {
  if (dewPoint < 10.0f) {
    return "แห้งมาก";
  }
  if (dewPoint < 13.0f) {
    return "สบายมาก";
  }
  if (dewPoint < 16.0f) {
    return "สบาย";
  }
  if (dewPoint < 18.0f) {
    return "เริ่มชื้น";
  }
  if (dewPoint < 21.0f) {
    return "ชื้น";
  }
  if (dewPoint < 24.0f) {
    return "ชื้นมาก อึดอัด";
  }
  return "ชื้นจัด อันตรายถ้าอยู่นาน";
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 05 จุดน้ำค้างและความสบาย");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT45,
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
  float t = 0.0f;
  float h = 0.0f;

  if (!sensor.measure(&t, &h)) {
    Serial.print("อ่านค่าไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    delay(2000);
    return;
  }

  /* ฟังก์ชันคำนวณเป็น static เรียกได้โดยไม่ต้องมีอ็อบเจกต์ */
  float dp = MassmoreSHT4x::dewPoint(t, h);
  float ah = MassmoreSHT4x::absoluteHumidity(t, h);
  float hi = MassmoreSHT4x::heatIndex(t, h);
  float svp = MassmoreSHT4x::saturationVaporPressure(t);

  Serial.println("----------------------------------------");
  Serial.print("อุณหภูมิ         : ");
  Serial.print(t, 2);
  Serial.print(" C  (");
  Serial.print(MassmoreSHT4x::celsiusToFahrenheit(t), 1);
  Serial.println(" F)");

  Serial.print("ความชื้นสัมพัทธ์  : ");
  Serial.print(h, 2);
  Serial.println(" %RH");

  Serial.print("จุดน้ำค้าง       : ");
  Serial.print(dp, 2);
  Serial.print(" C  -> ");
  Serial.println(comfortFromDewPoint(dp));

  Serial.print("ความชื้นสัมบูรณ์  : ");
  Serial.print(ah, 2);
  Serial.println(" g/m3");

  Serial.print("ดัชนีความร้อน    : ");
  Serial.print(hi, 2);
  Serial.println(" C (อุณหภูมิที่รู้สึก)");

  Serial.print("ความดันไออิ่มตัว : ");
  Serial.print(svp, 2);
  Serial.println(" hPa");

  /* เตือนเรื่องหยดน้ำ: ถ้าจุดน้ำค้างห่างจากอุณหภูมิอากาศไม่ถึง 2 องศา */
  if (t - dp < 2.0f) {
    Serial.println(">> ระวัง อากาศเกือบอิ่มตัว มีโอกาสเกิดหยดน้ำบนผิวที่เย็นกว่า");
  }
  if (h > 80.0f) {
    Serial.println(">> ความชื้นสูงต่อเนื่อง ควรยิงฮีตเตอร์ 20 mW เป็นระยะ (ดูตัวอย่างที่ 03)");
  }

  delay(3000);
}
