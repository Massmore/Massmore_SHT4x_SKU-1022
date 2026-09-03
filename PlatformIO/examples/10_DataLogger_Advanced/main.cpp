/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  10_DataLogger_Advanced - ตัวบันทึกข้อมูลพร้อมตัวกรอง สถิติ และการสอบเทียบ

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  การต่อสาย
    VIN -> 3.3V หรือ 5V   GND -> GND   SDA -> GPIO 21   SCL -> GPIO 22

  ตัวอย่างนี้รวมเทคนิคระดับใช้งานจริงไว้ในไฟล์เดียว

    1. ตัวกรองค่าเฉลี่ยเคลื่อนที่ (moving average) ขนาดปรับได้
       ทำให้กราฟเนียนโดยไม่เสียการตอบสนอง

    2. สถิติสะสม  ต่ำสุด / สูงสุด / เฉลี่ย ทั้งอุณหภูมิและความชื้น

    3. การสอบเทียบด้วย offset
       วางบอร์ดคู่กับเทอร์โมมิเตอร์อ้างอิง แล้วพิมพ์ค่าต่างเข้ามา
       เหมาะกับกรณีติดตั้งใกล้ ESP32 ที่ตัวมันเองปล่อยความร้อน

    4. เอาต์พุตแบบ CSV พร้อมคัดลอกไปเปิดใน Excel ได้เลย

  คำสั่งที่พิมพ์ได้ใน Serial Monitor (พิมพ์แล้วกด Enter)
      s        แสดงสถิติสะสม
      r        ล้างสถิติ
      c        เข้าโหมดสอบเทียบ (จะถามค่าอ้างอิงต่อ)
      f        สลับขนาดตัวกรอง 1 / 5 / 20 ค่า
      h        ยิงฮีตเตอร์ 20 mW หนึ่งครั้ง (กันค่าเลื่อนในที่ชื้น)
      ?        แสดงเมนูนี้

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

MassmoreSHT4x sensor;

#define FILTER_MAX 20

float bufferT[FILTER_MAX];
float bufferRH[FILTER_MAX];
uint8_t filterSize = 5;
uint8_t filterIndex = 0;
uint8_t filterCount = 0;

uint32_t samples = 0;
float minT = 1000.0f;
float maxT = -1000.0f;
float sumT = 0.0f;
float minRH = 1000.0f;
float maxRH = -1000.0f;
float sumRH = 0.0f;

uint32_t lastSampleMs = 0;
#define SAMPLE_INTERVAL_MS 2000

void resetStats() {
  samples = 0;
  minT = 1000.0f;
  maxT = -1000.0f;
  sumT = 0.0f;
  minRH = 1000.0f;
  maxRH = -1000.0f;
  sumRH = 0.0f;
  filterIndex = 0;
  filterCount = 0;
  Serial.println("# ล้างสถิติแล้ว");
}

void printMenu() {
  Serial.println("# คำสั่ง: s=สถิติ  r=ล้าง  c=สอบเทียบ  f=ขนาดตัวกรอง  h=ฮีตเตอร์  ?=เมนู");
}

void printStats() {
  Serial.println("# ---------------- สถิติสะสม ----------------");
  Serial.print("# จำนวนตัวอย่าง : ");
  Serial.println(samples);
  if (samples == 0) {
    Serial.println("# ยังไม่มีข้อมูล");
    return;
  }
  Serial.print("# อุณหภูมิ  ต่ำสุด ");
  Serial.print(minT, 2);
  Serial.print("  สูงสุด ");
  Serial.print(maxT, 2);
  Serial.print("  เฉลี่ย ");
  Serial.print(sumT / samples, 2);
  Serial.println(" C");
  Serial.print("# ความชื้น  ต่ำสุด ");
  Serial.print(minRH, 2);
  Serial.print("  สูงสุด ");
  Serial.print(maxRH, 2);
  Serial.print("  เฉลี่ย ");
  Serial.print(sumRH / samples, 2);
  Serial.println(" %RH");
  Serial.print("# offset ที่ตั้งไว้ : ");
  Serial.print(sensor.getTemperatureOffset(), 2);
  Serial.print(" C / ");
  Serial.print(sensor.getHumidityOffset(), 2);
  Serial.println(" %RH");
  Serial.print("# ขนาดตัวกรอง : ");
  Serial.println(filterSize);
  Serial.println("# -------------------------------------------");
}

/* โหมดสอบเทียบ: ถามค่าอ้างอิงแล้วคำนวณ offset ให้ */
void calibrate() {
  Serial.println("# โหมดสอบเทียบ");
  Serial.println("# พิมพ์อุณหภูมิจริงจากเครื่องมืออ้างอิง (องศาเซลเซียส) แล้วกด Enter");
  Serial.println("# พิมพ์ x เพื่อยกเลิก");

  /* ล้าง buffer ที่ค้างอยู่ */
  while (Serial.available()) {
    Serial.read();
  }

  uint32_t deadline = millis() + 30000;
  String input = "";
  while (millis() < deadline) {
    if (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length() > 0) {
          break;
        }
      } else {
        input += c;
      }
    }
  }

  input.trim();
  if (input.length() == 0 || input == "x") {
    Serial.println("# ยกเลิกการสอบเทียบ");
    return;
  }

  float reference = input.toFloat();
  if (reference < -40.0f || reference > 125.0f) {
    Serial.println("# ค่าที่ใส่อยู่นอกช่วงที่ชิปวัดได้ ยกเลิก");
    return;
  }

  /* วัดค่าดิบโดยไม่มี offset เพื่อคำนวณส่วนต่างจริง */
  float saved = sensor.getTemperatureOffset();
  sensor.setTemperatureOffset(0.0f);
  float sum = 0.0f;
  uint8_t ok = 0;
  for (uint8_t i = 0; i < 10; i++) {
    float t = 0.0f;
    if (sensor.measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, nullptr)) {
      sum += t;
      ok++;
    }
    delay(100);
  }
  if (ok == 0) {
    Serial.println("# วัดค่าไม่สำเร็จ คืน offset เดิม");
    sensor.setTemperatureOffset(saved);
    return;
  }

  float measured = sum / ok;
  float offset = reference - measured;
  sensor.setTemperatureOffset(offset);

  Serial.print("# ค่าที่วัดได้เฉลี่ย ");
  Serial.print(measured, 3);
  Serial.print(" C  ค่าอ้างอิง ");
  Serial.print(reference, 3);
  Serial.print(" C  ->  offset ใหม่ ");
  Serial.print(offset, 3);
  Serial.println(" C");
  Serial.println("# offset นี้อยู่ในหน่วยความจำเท่านั้น หายเมื่อรีเซ็ต");
  Serial.println("# ถ้าอยากเก็บถาวรให้บันทึกลง Preferences หรือ EEPROM เอง");
}

void handleCommand(char c) {
  switch (c) {
  case 's':
    printStats();
    break;
  case 'r':
    resetStats();
    break;
  case 'c':
    calibrate();
    break;
  case 'f':
    if (filterSize == 1) {
      filterSize = 5;
    } else if (filterSize == 5) {
      filterSize = FILTER_MAX;
    } else {
      filterSize = 1;
    }
    filterIndex = 0;
    filterCount = 0;
    Serial.print("# ขนาดตัวกรองใหม่ = ");
    Serial.println(filterSize);
    break;
  case 'h': {
    Serial.println("# ยิงฮีตเตอร์ 20 mW 1 วินาที");
    massmore_sht4x_reading_t hot;
    if (sensor.runHeater(MASSMORE_SHT4X_HEATER_20MW_1S, &hot)) {
      Serial.print("# ค่าตอนร้อน ");
      Serial.print(hot.temperature, 2);
      Serial.println(" C (ไม่นับเข้าสถิติ)");
    } else {
      Serial.print("# ยิงไม่สำเร็จ: ");
      Serial.println(sensor.lastErrorString());
    }
    break;
  }
  case '?':
    printMenu();
    break;
  default:
    break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("# Massmore SHT4X (SKU-1022) - 10 Data Logger ขั้นสูง");

  if (!sensor.begin(MASSMORE_SHT4X_I2C_ADDR_DEFAULT, MASSMORE_SHT4X_VARIANT_SHT45,
                    21, 22)) {
    Serial.print("# ไม่พบเซ็นเซอร์: ");
    Serial.println(sensor.lastErrorString());
    while (true) {
      delay(1000);
    }
  }

  Serial.print("# ซีเรียล 0x");
  Serial.print(sensor.getSerialNumber(), HEX);
  Serial.print("  รุ่น ");
  Serial.print(sensor.getVariantName());
  Serial.print("  สเปก +/-");
  Serial.print(sensor.getTemperatureAccuracy(), 1);
  Serial.print(" C, +/-");
  Serial.print(sensor.getHumidityAccuracy(), 1);
  Serial.println(" %RH");

  printMenu();
  resetStats();

  /* หัวตาราง CSV */
  Serial.println("millis,temperature_c,humidity_rh,filtered_t,filtered_rh,dewpoint_c,abs_humidity_gm3");
}

void loop() {
  if (Serial.available()) {
    handleCommand((char)Serial.read());
  }

  if (millis() - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs = millis();

  float t = 0.0f;
  float h = 0.0f;
  if (!sensor.measure(&t, &h)) {
    Serial.print("# อ่านค่าไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
    return;
  }

  /* --- ตัวกรองค่าเฉลี่ยเคลื่อนที่ --- */
  bufferT[filterIndex] = t;
  bufferRH[filterIndex] = h;
  filterIndex = (uint8_t)((filterIndex + 1) % filterSize);
  if (filterCount < filterSize) {
    filterCount++;
  }

  float filteredT = 0.0f;
  float filteredRH = 0.0f;
  for (uint8_t i = 0; i < filterCount; i++) {
    filteredT += bufferT[i];
    filteredRH += bufferRH[i];
  }
  filteredT /= filterCount;
  filteredRH /= filterCount;

  /* --- สถิติสะสม --- */
  samples++;
  sumT += t;
  sumRH += h;
  if (t < minT) {
    minT = t;
  }
  if (t > maxT) {
    maxT = t;
  }
  if (h < minRH) {
    minRH = h;
  }
  if (h > maxRH) {
    maxRH = h;
  }

  /* --- บรรทัด CSV --- */
  Serial.print(millis());
  Serial.print(",");
  Serial.print(t, 3);
  Serial.print(",");
  Serial.print(h, 3);
  Serial.print(",");
  Serial.print(filteredT, 3);
  Serial.print(",");
  Serial.print(filteredRH, 3);
  Serial.print(",");
  Serial.print(MassmoreSHT4x::dewPoint(filteredT, filteredRH), 3);
  Serial.print(",");
  Serial.println(MassmoreSHT4x::absoluteHumidity(filteredT, filteredRH), 3);
}
