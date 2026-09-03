/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

/*
  07_MultipleSensors - ใช้หลายตัวพร้อมกัน

  บอร์ด  : Massmore SHT4X (SKU-1022)  SHT40 / SHT41 / SHT45  ทั้งรุ่น -B และ -F
  ร้านค้า : https://www.massmore.shop

  SHT4x กำหนด I2C address มาจากโรงงานตามรหัสรุ่น เปลี่ยนเองไม่ได้
    รุ่นลงท้าย -AD1B / -AD1F  ->  0x44   (บอร์ด Massmore ใช้ตัวนี้)
    รุ่น SHT40-BD1B           ->  0x45
    รุ่น SHT40-CD1B           ->  0x46

  ดังนั้นถ้าอยากใช้บอร์ด Massmore หลายตัวพร้อมกัน มีสองทาง
    ทาง A  ใช้บัส I2C คนละเส้น (ESP32 ส่วนใหญ่มี Wire และ Wire1)
    ทาง B  ใช้ I2C multiplexer เช่น TCA9548A

  ตัวอย่างนี้ทำทาง A และสาธิตการสแกนหา address ทั้งสามตัวบนบัสเดียวด้วย

  การต่อสาย
    เซ็นเซอร์ตัวที่ 1 (บัส Wire)   SDA -> GPIO 21   SCL -> GPIO 22
    เซ็นเซอร์ตัวที่ 2 (บัส Wire1)  SDA -> GPIO 16   SCL -> GPIO 17
    ทั้งสองตัวใช้ VIN และ GND ร่วมกันได้

  หมายเหตุ  ESP32-C3 และ ESP32-S2 บางรุ่นมีบัส I2C แค่เส้นเดียว
            ตัวอย่างจะข้ามส่วน Wire1 ให้อัตโนมัติ

  by Massmore  |  MIT License
*/

#include <Massmore_SHT4x.h>

/* ESP32 รุ่นไหนมีบัสที่สอง ตรวจจากมาโครของ core โดยตรง */
#if defined(ARDUINO_ARCH_ESP32) &&                                                    \
    ((defined(SOC_HP_I2C_NUM) && SOC_HP_I2C_NUM > 1) ||                                \
     (defined(SOC_I2C_NUM) && SOC_I2C_NUM > 1))
#define HAS_SECOND_BUS 1
#else
#define HAS_SECOND_BUS 0
#endif

MassmoreSHT4x sensorA(&Wire);
#if HAS_SECOND_BUS
MassmoreSHT4x sensorB(&Wire1);
#endif

bool okA = false;
#if HAS_SECOND_BUS
bool okB = false;
#endif

void scanBus(TwoWire *bus, const char *name) {
  uint8_t found[3] = {0, 0, 0};
  uint8_t count = MassmoreSHT4x::scan(bus, found);

  Serial.print("สแกนบัส ");
  Serial.print(name);
  Serial.print(" : พบ ");
  Serial.print(count);
  Serial.print(" ตัว");
  for (uint8_t i = 0; i < count; i++) {
    Serial.print("  0x");
    Serial.print(found[i], HEX);
  }
  Serial.println();
}

void report(MassmoreSHT4x &sensor, const char *label) {
  float t = 0.0f;
  float h = 0.0f;
  Serial.print(label);
  if (sensor.measure(&t, &h)) {
    Serial.print("  0x");
    Serial.print(sensor.getAddress(), HEX);
    Serial.print("  ");
    Serial.print(t, 2);
    Serial.print(" C  ");
    Serial.print(h, 2);
    Serial.print(" %RH  จุดน้ำค้าง ");
    Serial.print(MassmoreSHT4x::dewPoint(t, h), 2);
    Serial.println(" C");
  } else {
    Serial.print("  อ่านไม่สำเร็จ: ");
    Serial.println(sensor.lastErrorString());
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Serial.println();
  Serial.println("Massmore SHT4X (SKU-1022) - 07 ใช้หลายตัวพร้อมกัน");
  Serial.println();

  /* --- บัสแรก --- */
  Wire.begin(21, 22);
  Wire.setClock(100000UL);
  scanBus(&Wire, "Wire ");

  /* beginWithExistingBus() ใช้เมื่อเราตั้งค่าบัสเองแล้ว */
  okA = sensorA.beginWithExistingBus(MASSMORE_SHT4X_I2C_ADDR_A,
                                     MASSMORE_SHT4X_VARIANT_SHT40);
  Serial.print("เซ็นเซอร์ A : ");
  Serial.println(okA ? "พร้อมใช้งาน" : sensorA.lastErrorString());

#if HAS_SECOND_BUS
  /* --- บัสที่สอง --- */
  Wire1.begin(16, 17);
  Wire1.setClock(100000UL);
  scanBus(&Wire1, "Wire1");

  okB = sensorB.beginWithExistingBus(MASSMORE_SHT4X_I2C_ADDR_A,
                                     MASSMORE_SHT4X_VARIANT_SHT45);
  Serial.print("เซ็นเซอร์ B : ");
  Serial.println(okB ? "พร้อมใช้งาน" : sensorB.lastErrorString());
#else
  Serial.println("บอร์ดนี้มีบัส I2C เส้นเดียว ข้ามส่วนของเซ็นเซอร์ B");
#endif

  Serial.println();
  Serial.println("ซีเรียลจากโรงงานใช้แยกว่าตัวไหนเป็นตัวไหนได้ แม้ address เท่ากัน");
  if (okA) {
    Serial.print("  A -> 0x");
    Serial.println(sensorA.getSerialNumber(), HEX);
  }
#if HAS_SECOND_BUS
  if (okB) {
    Serial.print("  B -> 0x");
    Serial.println(sensorB.getSerialNumber(), HEX);
  }
#endif
  Serial.println();
}

void loop() {
  if (okA) {
    report(sensorA, "A");
  }
#if HAS_SECOND_BUS
  if (okB) {
    report(sensorB, "B");
  }

  /* เทียบสองจุดวัด ใช้ดูความต่างระหว่างในตู้กับนอกตู้ได้เลย */
  if (okA && okB) {
    Serial.print("ส่วนต่าง : ");
    Serial.print(sensorA.getTemperature() - sensorB.getTemperature(), 2);
    Serial.print(" C   ");
    Serial.print(sensorA.getHumidity() - sensorB.getHumidity(), 2);
    Serial.println(" %RH");
  }
#endif

  Serial.println();
  delay(2000);
}
