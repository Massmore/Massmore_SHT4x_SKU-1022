/*!
 * @file massmore_sht4x_host_shim.h
 * @brief ตัวจำลอง Arduino + TwoWire + ชิป SHT4x สำหรับรัน host test บนเครื่อง PC
 *
 * ไฟล์นี้ไม่ได้ถูกคอมไพล์เข้าเฟิร์มแวร์ ใช้เฉพาะตอนรัน `make` ในโฟลเดอร์ test/
 * เพื่อทดสอบตรรกะของไลบรารีจริง ๆ (ไฟล์ .cpp ตัวเดียวกับที่ลงบอร์ด)
 * โดยไม่ต้องมีฮาร์ดแวร์
 *
 * ชิปจำลองทำตาม datasheet ในเรื่องที่สำคัญที่สุดสามข้อ
 *   1. คำสั่งยาว 1 ไบต์
 *   2. ยังวัดไม่เสร็จแล้วถูกอ่าน จะ NACK (ไม่มี clock stretching)
 *   3. เวลาที่ใช้วัดต่างกันตามระดับความละเอียด
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_SHT4X_HOST_SHIM_H
#define MASSMORE_SHT4X_HOST_SHIM_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------------- */
/* ค่าคงที่แบบ Arduino                                                     */
/* --------------------------------------------------------------------- */

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

/* --------------------------------------------------------------------- */
/* เวลาจำลอง เดินหน้าเองเมื่อเรียก delay()                                  */
/* --------------------------------------------------------------------- */

extern uint32_t g_mockMillis;

inline uint32_t millis() { return g_mockMillis; }
inline void delay(uint32_t ms) { g_mockMillis += ms; }
inline void delayMicroseconds(uint32_t us) { g_mockMillis += (us + 999) / 1000; }

/* --------------------------------------------------------------------- */
/* GPIO จำลอง                                                             */
/* --------------------------------------------------------------------- */

extern uint8_t g_mockPinMode[64];
extern uint8_t g_mockPinLevel[64];

inline void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 64) {
    g_mockPinMode[pin] = mode;
  }
}
inline void digitalWrite(uint8_t pin, uint8_t level) {
  if (pin < 64) {
    g_mockPinLevel[pin] = level;
  }
}
inline int digitalRead(uint8_t pin) { return (pin < 64) ? g_mockPinLevel[pin] : 0; }

/* --------------------------------------------------------------------- */
/* ชิป SHT4x จำลอง                                                        */
/* --------------------------------------------------------------------- */

/*!
 * @brief แบบจำลองพฤติกรรมของชิปตาม datasheet
 *
 * ตั้งค่าธงต่าง ๆ เพื่อจำลอง "ชิปปลอม" ที่ทำบางอย่างไม่ได้ แล้วดูว่า
 * verifyChip() จับได้หรือไม่
 */
struct MockSht4x {
  bool present = true;         /*!< มีชิปอยู่บนบัสไหม */
  uint8_t address = 0x44;      /*!< address ที่ชิปตอบ */
  bool supportsSerial = true;  /*!< รู้จักคำสั่ง 0x89 ไหม */
  bool serialDrifts = false;   /*!< อ่านซีเรียลซ้ำแล้วได้ค่าใหม่ (ของปลอมที่สุ่มค่า) */
  bool nackWhenBusy = true;    /*!< NACK ตอนยังวัดไม่เสร็จตาม datasheet */
  bool answersBogusCommand = false; /*!< ตอบข้อมูลให้คำสั่งนอกตาราง (ของปลอม) */
  bool supportsHeater = true;  /*!< รู้จักคำสั่งฮีตเตอร์ไหม */
  bool corruptCrc = false;     /*!< ส่ง CRC ผิดทุกครั้ง */
  uint32_t serial = 0x0A1B2C3D;

  uint16_t rawT = 0x6666;  /*!< ประมาณ 24.6 องศา */
  uint16_t rawRH = 0x8000; /*!< ประมาณ 56.5 %RH */

  /* ฮีตเตอร์: ทุกครั้งที่ยิง ค่าอุณหภูมิดิบจะถูกบวกเพิ่มชั่วคราว */
  uint16_t heatBoostRaw = 800;
  bool lastWasHeater = false;
  uint32_t heaterPulses = 0;

  /* สถานะภายในของธุรกรรมปัจจุบัน */
  uint8_t lastCommand = 0;
  bool commandKnown = false;
  uint32_t readyAtMs = 0;
  uint8_t txBuffer[8];
  uint8_t txLength = 0;
  uint8_t rxBuffer[8];
  uint8_t rxLength = 0;
  uint8_t rxIndex = 0;
  uint32_t commandCount = 0;
  uint32_t serialReads = 0;
  uint32_t nackCount = 0;
};

extern MockSht4x g_mockChip;

/*! คืนชิปจำลองกลับเป็นค่าตั้งต้นของชิปแท้ */
void mockResetChip();

/*! CRC-8 ชุดเดียวกับที่ชิปใช้ (เขียนซ้ำในฝั่ง mock เพื่อไม่พึ่งโค้ดที่กำลังทดสอบ) */
uint8_t mockCrc8(const uint8_t *data, uint8_t length);

/* --------------------------------------------------------------------- */
/* TwoWire จำลอง                                                          */
/* --------------------------------------------------------------------- */

class TwoWire {
public:
  void begin() { _begun = true; }
  void begin(int sda, int scl) {
    _begun = true;
    _sda = sda;
    _scl = scl;
  }
  void setClock(uint32_t frequency) { _frequency = frequency; }

  void beginTransmission(uint8_t address) {
    _address = address;
    g_mockChip.txLength = 0;
  }

  size_t write(uint8_t value) {
    if (g_mockChip.txLength < sizeof(g_mockChip.txBuffer)) {
      g_mockChip.txBuffer[g_mockChip.txLength++] = value;
    }
    return 1;
  }

  uint8_t endTransmission();
  uint8_t requestFrom(uint8_t address, uint8_t length);

  int available() { return (int)(g_mockChip.rxLength - g_mockChip.rxIndex); }
  int read() {
    if (g_mockChip.rxIndex < g_mockChip.rxLength) {
      return g_mockChip.rxBuffer[g_mockChip.rxIndex++];
    }
    return -1;
  }

  int sda() const { return _sda; }
  int scl() const { return _scl; }
  uint32_t frequency() const { return _frequency; }
  bool begun() const { return _begun; }

private:
  bool _begun = false;
  int _sda = -1;
  int _scl = -1;
  uint32_t _frequency = 0;
  uint8_t _address = 0;
};

extern TwoWire Wire;

#endif /* MASSMORE_SHT4X_HOST_SHIM_H */
