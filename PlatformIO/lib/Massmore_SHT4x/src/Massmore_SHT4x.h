/*!
 * @file Massmore_SHT4x.h
 * @brief ไลบรารี Arduino / PlatformIO สำหรับเซ็นเซอร์อุณหภูมิและความชื้น
 *        Sensirion SHT4x (SHT40 / SHT41 / SHT45) ทั้งรุ่นปกติ (-B)
 *        และรุ่นกันฝุ่นที่มีเมมเบรน Polyimide (-F / DIS-F / Outdoor)
 *
 * บอร์ด Massmore SHT4X SKU-1022
 *
 * จุดเด่นของไลบรารีตัวนี้
 *   - เขียนขึ้นจาก datasheet โดยตรง ไม่พึ่งไลบรารีอื่นนอกจาก Wire
 *   - ไม่ใช้ heap เลย (ไม่มี new / malloc / String ในส่วนแกน)
 *   - ครบทุกคำสั่งที่ชิปมี: วัดสามระดับความละเอียด, ฮีตเตอร์ครบทั้ง 6 โหมด,
 *     อ่านซีเรียลจากโรงงาน, soft reset, general call reset
 *   - มีตัวกันเผลอ (duty cycle guard) ไม่ให้เปิดฮีตเตอร์เกินที่ datasheet อนุญาต
 *   - มีโหมด non-blocking ทั้งการวัดและการยิงฮีตเตอร์
 *   - มี verifyChip() ตรวจ 10 ข้อว่าเป็นชิป Sensirion ของแท้ ไม่ใช่ของเลียนแบบ
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_SHT4X_H
#define MASSMORE_SHT4X_H

#include "Massmore_SHT4x_Registers.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#else
/* ใช้ตอนคอมไพล์ host test บนเครื่อง PC ไฟล์ mock อยู่ในโฟลเดอร์ test/ */
#include "massmore_sht4x_host_shim.h"
#endif

/*! เวอร์ชันของไลบรารี */
#define MASSMORE_SHT4X_VERSION_MAJOR 1
#define MASSMORE_SHT4X_VERSION_MINOR 0
#define MASSMORE_SHT4X_VERSION_PATCH 0
#define MASSMORE_SHT4X_VERSION_STRING "1.0.0"

/*! ความถี่ I2C ที่แนะนำสำหรับบอร์ด Massmore (สาย Qwiic ยาวไม่เกิน 30 ซม.) */
#define MASSMORE_SHT4X_I2C_FREQ_DEFAULT 100000UL

/*! เวลารอสูงสุด (ms) ตอนอ่านข้อมูลจากบัส */
#define MASSMORE_SHT4X_TIMEOUT_DEFAULT_MS 100

/* ========================================================================= */
/* ชนิดข้อมูล                                                                */
/* ========================================================================= */

/*!
 * @brief รุ่นของชิปในตระกูล SHT4x
 *
 * ข้อเท็จจริงที่ผู้ใช้ควรทราบ: SHT40, SHT41 และ SHT45 คือซิลิคอนตัวเดียวกัน
 * ต่างกันที่เกรดความแม่นยำที่โรงงานคัดไว้ (binning) เท่านั้น ตัวชิปไม่มี
 * รีจิสเตอร์บอกรุ่น และหมายเลขซีเรียลก็ไม่มีเอกสารยืนยันว่าถอดรุ่นออกมาได้
 * จึงอ่านแยกรุ่นผ่าน I2C ไม่ได้ทั้งในไลบรารีนี้และไลบรารีใด ๆ
 *
 * ให้ดูช่องติ๊กบนซิลค์สกรีนของบอร์ด (SHT 40 / 41 / 45) หรือรหัสบนตัวชิป
 * แล้วบอกไลบรารีเองผ่าน begin() หรือ setVariant()
 * ค่าที่ตั้งไว้ใช้กำหนดเกณฑ์ความแม่นยำที่ใช้ตรวจสอบและแสดงผลเท่านั้น
 * ไม่มีผลต่อการสื่อสารกับชิป
 */
typedef enum {
  MASSMORE_SHT4X_VARIANT_AUTO = 0, /*!< ไม่ระบุรุ่น ใช้เกณฑ์กว้างสุด (เท่า SHT40) */
  MASSMORE_SHT4X_VARIANT_SHT40,    /*!< ความชื้น +/-1.8 %RH, อุณหภูมิ +/-0.2 องศา */
  MASSMORE_SHT4X_VARIANT_SHT41,    /*!< ความชื้น +/-1.8 %RH, อุณหภูมิ +/-0.2 องศา (max ดีกว่า SHT40) */
  MASSMORE_SHT4X_VARIANT_SHT45     /*!< ความชื้น +/-1.0 %RH, อุณหภูมิ +/-0.1 องศา */
} massmore_sht4x_variant_t;

/*!
 * @brief แบบตัวถังของบอร์ด ใช้เพื่อการแสดงผลและคำแนะนำเท่านั้น
 */
typedef enum {
  MASSMORE_SHT4X_PACKAGE_UNKNOWN = 0, /*!< ไม่ระบุ */
  MASSMORE_SHT4X_PACKAGE_B,           /*!< รุ่นปกติ ช่องเซ็นเซอร์เปิดโล่ง ตอบสนองเร็วสุด */
  MASSMORE_SHT4X_PACKAGE_F            /*!< รุ่นกันฝุ่น มีเมมเบรน Polyimide ปิดหน้าเซ็นเซอร์ */
} massmore_sht4x_package_t;

/*!
 * @brief ระดับความละเอียดของการวัด
 *
 * ยิ่งสูงยิ่งใช้เวลาและพลังงานมากขึ้น แต่ noise ต่ำลง
 */
typedef enum {
  MASSMORE_SHT4X_PRECISION_LOW = 0, /*!< คำสั่ง 0xE0 เร็วสุด ~1.3 ms  noise สูงสุด */
  MASSMORE_SHT4X_PRECISION_MEDIUM,  /*!< คำสั่ง 0xF6 ~3.7 ms */
  MASSMORE_SHT4X_PRECISION_HIGH     /*!< คำสั่ง 0xFD ~6.9 ms noise ต่ำสุด (ค่าเริ่มต้น) */
} massmore_sht4x_precision_t;

/*!
 * @brief โหมดของฮีตเตอร์ในตัวชิป
 *
 * ทุกโหมดจะวัดความละเอียดสูงหนึ่งครั้งก่อนดับฮีตเตอร์ให้อัตโนมัติ
 * ค่าที่วัดได้ตอนนั้น "ร้อนกว่าความจริง" ใช้ดูแนวโน้มเท่านั้น อย่าเอาไปรายงาน
 *
 * @warning datasheet กำหนดว่าฮีตเตอร์ออกแบบมาให้ใช้ที่ duty cycle ต่ำกว่า 10%
 *          ไลบรารีมีตัวกันเผลอให้แล้ว ดู setHeaterDutyGuard()
 */
typedef enum {
  MASSMORE_SHT4X_HEATER_200MW_1S = 0, /*!< 200 mW 1 วินาที  (แรงสุด ไล่หยดน้ำ) */
  MASSMORE_SHT4X_HEATER_200MW_0S1,    /*!< 200 mW 0.1 วินาที */
  MASSMORE_SHT4X_HEATER_110MW_1S,     /*!< 110 mW 1 วินาที */
  MASSMORE_SHT4X_HEATER_110MW_0S1,    /*!< 110 mW 0.1 วินาที */
  MASSMORE_SHT4X_HEATER_20MW_1S,      /*!< 20 mW 1 วินาที  (ใช้กันค่าเลื่อนในที่ชื้นจัด) */
  MASSMORE_SHT4X_HEATER_20MW_0S1      /*!< 20 mW 0.1 วินาที (เบาสุด) */
} massmore_sht4x_heater_t;

/*!
 * @brief โหมดการทำงานปัจจุบันของไลบรารี
 */
typedef enum {
  MASSMORE_SHT4X_MODE_IDLE = 0,    /*!< ยังไม่ begin() */
  MASSMORE_SHT4X_MODE_READY,       /*!< พร้อมรับคำสั่ง ไม่มีงานค้าง */
  MASSMORE_SHT4X_MODE_MEASURING,   /*!< สั่งวัดแล้วรอผลอยู่ (non-blocking) */
  MASSMORE_SHT4X_MODE_HEATING      /*!< ฮีตเตอร์กำลังทำงาน (non-blocking) */
} massmore_sht4x_mode_t;

/*!
 * @brief รหัสผลลัพธ์ของทุกฟังก์ชันที่คุยกับชิป
 *
 * ฟังก์ชันส่วนใหญ่คืน bool เพื่อให้เขียนง่าย แล้วเก็บรหัสละเอียดไว้ที่
 * lastError() ให้ไปดูตอนเกิดปัญหา
 */
typedef enum {
  MASSMORE_SHT4X_OK = 0,           /*!< สำเร็จ */
  MASSMORE_SHT4X_ERR_NOT_BEGUN,    /*!< ยังไม่ได้เรียก begin() */
  MASSMORE_SHT4X_ERR_NO_DEVICE,    /*!< ไม่มีอุปกรณ์ตอบที่ address นี้ */
  MASSMORE_SHT4X_ERR_I2C_WRITE,    /*!< เขียนลงบัสไม่สำเร็จ */
  MASSMORE_SHT4X_ERR_I2C_READ,     /*!< อ่านได้ไบต์ไม่ครบ (ชิปยัง NACK อยู่) */
  MASSMORE_SHT4X_ERR_CRC,          /*!< checksum ของข้อมูลที่อ่านมาไม่ตรง */
  MASSMORE_SHT4X_ERR_TIMEOUT,      /*!< รอเกินเวลาที่ตั้งไว้ */
  MASSMORE_SHT4X_ERR_NOT_READY,    /*!< ยังวัดไม่เสร็จ ให้รอแล้วเรียกใหม่ */
  MASSMORE_SHT4X_ERR_WRONG_MODE,   /*!< เรียกผิดจังหวะ เช่นสั่งวัดตอนฮีตเตอร์ทำงาน */
  MASSMORE_SHT4X_ERR_BAD_ARG,      /*!< พารามิเตอร์ไม่ถูกต้อง */
  MASSMORE_SHT4X_ERR_OUT_OF_RANGE, /*!< ค่าที่อ่านได้อยู่นอกช่วงที่ชิปทำได้ */
  MASSMORE_SHT4X_ERR_HEATER_DUTY   /*!< ขอเปิดฮีตเตอร์ถี่เกินที่ datasheet อนุญาต */
} massmore_sht4x_error_t;

/*!
 * @brief ผลการตรวจสอบว่าเป็นชิป Sensirion แท้หรือไม่
 * @see MassmoreSHT4x::verifyChip()
 */
typedef enum {
  MASSMORE_SHT4X_GENUINE_UNKNOWN = 0, /*!< ยังไม่ได้ตรวจ */
  MASSMORE_SHT4X_GENUINE_PASS,        /*!< ผ่านครบทุกข้อ = เป็น SHT4x แท้ */
  MASSMORE_SHT4X_GENUINE_PARTIAL,     /*!< ตอบถูกเป็นส่วนใหญ่ แต่มีบางข้อไม่ผ่าน */
  MASSMORE_SHT4X_GENUINE_SUSPECT,     /*!< ตอบผิดหลายข้อ น่าสงสัยว่าไม่ใช่ของแท้ */
  MASSMORE_SHT4X_GENUINE_NOT_SHT4X    /*!< มีอุปกรณ์อยู่ แต่ไม่ใช่ SHT4x แน่นอน */
} massmore_sht4x_genuine_t;

/*! หมายเลขข้อของการตรวจ verifyChip() ใช้เป็นบิตใน getVerifyMask() */
#define MASSMORE_SHT4X_CHK_ACK (1u << 0)         /*!< ชิป ACK ที่ address */
#define MASSMORE_SHT4X_CHK_SERIAL_CRC (1u << 1)  /*!< อ่านซีเรียลได้ CRC ถูกทั้งสอง word */
#define MASSMORE_SHT4X_CHK_SERIAL_SANE (1u << 2) /*!< ซีเรียลไม่ใช่ 0 หรือ FFFFFFFF */
#define MASSMORE_SHT4X_CHK_SERIAL_STABLE (1u << 3) /*!< อ่านซีเรียลซ้ำได้ค่าเดิม */
#define MASSMORE_SHT4X_CHK_SOFT_RESET (1u << 4)  /*!< soft reset แล้วชิปกลับมาวัดได้ */
#define MASSMORE_SHT4X_CHK_MEAS_CRC (1u << 5)    /*!< วัดจริงแล้ว CRC ทั้งสอง word ถูก */
#define MASSMORE_SHT4X_CHK_MEAS_RANGE (1u << 6)  /*!< ค่าที่วัดได้อยู่ในช่วงที่เป็นไปได้ */
#define MASSMORE_SHT4X_CHK_NACK_EARLY (1u << 7)  /*!< อ่านก่อนวัดเสร็จแล้วชิป NACK จริง */
#define MASSMORE_SHT4X_CHK_LOW_FASTER (1u << 8)  /*!< ความละเอียดต่ำเสร็จเร็วกว่าจริง */
#define MASSMORE_SHT4X_CHK_BOGUS_CMD (1u << 9)   /*!< คำสั่งนอกตารางไม่ถูกตอบด้วยข้อมูล */
#define MASSMORE_SHT4X_CHK_ALL 0x03FFu           /*!< ครบทั้ง 10 ข้อ */
#define MASSMORE_SHT4X_CHK_COUNT 10

/*!
 * @brief ผลวัดหนึ่งชุด
 */
typedef struct {
  float temperature;       /*!< องศาเซลเซียส (รวม offset ที่ตั้งไว้แล้ว) */
  float humidity;          /*!< %RH (รวม offset และการตัดขอบแล้ว) */
  uint16_t rawTemperature; /*!< ค่าดิบ 16 บิตจากชิป */
  uint16_t rawHumidity;    /*!< ค่าดิบ 16 บิตจากชิป */
  uint32_t timestampMs;    /*!< millis() ตอนที่อ่านค่าได้ */
  bool heated;             /*!< true = ค่านี้มาจากคำสั่งฮีตเตอร์ อย่าเอาไปรายงาน */
} massmore_sht4x_reading_t;

/*! ต้นแบบฟังก์ชัน callback ที่จะถูกเรียกทุกครั้งที่ update() ได้ค่าใหม่ */
typedef void (*massmore_sht4x_callback_t)(const massmore_sht4x_reading_t &reading);

/* ========================================================================= */
/* คลาสหลัก                                                                  */
/* ========================================================================= */

/*!
 * @brief ไดรเวอร์ SHT4x หนึ่งตัว
 *
 * สร้างได้หลายอ็อบเจกต์ในโปรแกรมเดียวกัน (คนละ address หรือคนละบัส I2C)
 * ทุกอ็อบเจกต์ไม่จองหน่วยความจำจาก heap เลย
 */
class MassmoreSHT4x {
public:
  /*!
   * @brief สร้างอ็อบเจกต์ โดยระบุบัส I2C ที่จะใช้
   * @param wire ตัวชี้ไปยัง TwoWire เช่น &Wire หรือ &Wire1 (ค่าเริ่มต้น &Wire)
   */
  explicit MassmoreSHT4x(TwoWire *wire = &Wire);

  /* --------------------------------------------------------------------- */
  /* การเริ่มต้นใช้งาน                                                       */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief เริ่มต้นใช้งาน โดยให้ไลบรารีเรียก Wire.begin() ให้
   * @param address   0x44 (บอร์ด Massmore), 0x45 หรือ 0x46 ตามรหัสรุ่นของชิป
   * @param variant   รุ่นของชิปตามที่ติ๊กไว้บนบอร์ด
   * @param sdaPin    ขา SDA (-1 = ใช้ค่าปริยายของบอร์ด) ใช้ได้กับ ESP32/ESP8266
   * @param sclPin    ขา SCL (-1 = ใช้ค่าปริยายของบอร์ด)
   * @param frequency ความถี่บัส I2C เป็น Hz
   * @return true เมื่อพบชิปและอ่านซีเรียลได้
   */
  bool begin(uint8_t address = MASSMORE_SHT4X_I2C_ADDR_DEFAULT,
             massmore_sht4x_variant_t variant = MASSMORE_SHT4X_VARIANT_AUTO,
             int8_t sdaPin = -1, int8_t sclPin = -1,
             uint32_t frequency = MASSMORE_SHT4X_I2C_FREQ_DEFAULT);

  /*!
   * @brief เริ่มต้นใช้งานโดยที่โปรแกรมเรียก Wire.begin() เองไปแล้ว
   *
   * ใช้กรณีมีอุปกรณ์ I2C หลายตัวบนบัสเดียวกัน และอยากคุมการตั้งค่าบัสเอง
   * @param address address ของชิป
   * @param variant รุ่นของชิป
   * @return true เมื่อพบชิป
   */
  bool beginWithExistingBus(uint8_t address = MASSMORE_SHT4X_I2C_ADDR_DEFAULT,
                            massmore_sht4x_variant_t variant = MASSMORE_SHT4X_VARIANT_AUTO);

  /*!
   * @brief เช็คว่าชิปยังตอบอยู่บนบัสไหม (ส่ง address เปล่า ๆ)
   * @return true ถ้าได้ ACK
   */
  bool isConnected();

  /*! @brief ตั้งเวลารอสูงสุดของการอ่านบัส (มิลลิวินาที) */
  void setTimeout(uint16_t milliseconds);

  /* --------------------------------------------------------------------- */
  /* การตั้งค่า                                                             */
  /* --------------------------------------------------------------------- */

  /*! @brief ตั้งระดับความละเอียดที่จะใช้กับการวัดครั้งถัดไป */
  void setPrecision(massmore_sht4x_precision_t precision);
  /*! @brief อ่านระดับความละเอียดปัจจุบัน */
  massmore_sht4x_precision_t getPrecision() const;
  /*! @brief ชื่อระดับความละเอียดเป็นข้อความ เช่น "HIGH" */
  static const char *precisionToString(massmore_sht4x_precision_t precision);

  /*!
   * @brief ตั้งค่าชดเชยอุณหภูมิ (บวกเข้ากับค่าที่อ่านได้)
   *
   * ใช้แก้กรณีติดตั้งชิปใกล้แหล่งความร้อน เช่นวางติดกับ ESP32
   * @param offsetCelsius ค่าชดเชยเป็นองศาเซลเซียส (ปกติเป็นลบ)
   */
  void setTemperatureOffset(float offsetCelsius);
  /*! @brief อ่านค่าชดเชยอุณหภูมิที่ตั้งไว้ */
  float getTemperatureOffset() const;
  /*! @brief ตั้งค่าชดเชยความชื้นเป็น %RH */
  void setHumidityOffset(float offsetPercent);
  /*! @brief อ่านค่าชดเชยความชื้นที่ตั้งไว้ */
  float getHumidityOffset() const;

  /*!
   * @brief เปิด/ปิดการตัดค่าความชื้นให้อยู่ในช่วง 0 ถึง 100 %RH
   *
   * สูตรของ SHT4x ให้ค่าเลย 0 หรือ 100 ได้เล็กน้อยตามธรรมชาติของการคำนวณ
   * datasheet แนะนำให้ตัดขอบก่อนนำไปแสดงผล ไลบรารีจึงเปิดไว้เป็นค่าเริ่มต้น
   * ปิดได้ถ้าต้องการค่าดิบสำหรับงานสอบเทียบ
   * @param enabled true = ตัดขอบ (ค่าเริ่มต้น)
   */
  void setHumidityClipping(bool enabled);
  /*! @brief การตัดขอบความชื้นเปิดอยู่หรือไม่ */
  bool getHumidityClipping() const;

  /*! @brief ระบุรุ่นของชิปตามที่ติ๊กไว้บนซิลค์สกรีน */
  void setVariant(massmore_sht4x_variant_t variant);
  /*! @brief อ่านรุ่นที่ตั้งไว้ */
  massmore_sht4x_variant_t getVariant() const;
  /*! @brief ชื่อรุ่นเป็นข้อความ เช่น "SHT45" */
  const char *getVariantName() const;
  /*! @brief ความคลาดเคลื่อนอุณหภูมิตามสเปกของรุ่นที่ตั้งไว้ (องศาเซลเซียส) */
  float getTemperatureAccuracy() const;
  /*! @brief ความคลาดเคลื่อนความชื้นตามสเปกของรุ่นที่ตั้งไว้ (%RH) */
  float getHumidityAccuracy() const;

  /*! @brief ระบุแบบตัวถัง (-B ปกติ หรือ -F กันฝุ่น) ใช้แสดงผลอย่างเดียว */
  void setPackage(massmore_sht4x_package_t package);
  /*! @brief อ่านแบบตัวถังที่ตั้งไว้ */
  massmore_sht4x_package_t getPackage() const;
  /*! @brief ชื่อแบบตัวถังเป็นข้อความ */
  const char *getPackageName() const;

  /* --------------------------------------------------------------------- */
  /* การวัดแบบบล็อก                                                          */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief วัดหนึ่งครั้งแล้วคืนทั้งอุณหภูมิและความชื้น
   * @param temperature ตัวรับอุณหภูมิเป็นองศาเซลเซียส (ใส่ nullptr ได้ถ้าไม่ต้องการ)
   * @param humidity    ตัวรับความชื้นเป็น %RH (ใส่ nullptr ได้)
   * @return true เมื่อสำเร็จ
   */
  bool measure(float *temperature, float *humidity);

  /*!
   * @brief วัดหนึ่งครั้งแล้วเก็บผลลงโครงสร้าง reading (มีค่าดิบและ timestamp ด้วย)
   * @param reading ตัวรับผลวัด
   * @return true เมื่อสำเร็จ
   */
  bool measure(massmore_sht4x_reading_t &reading);

  /*!
   * @brief วัดหนึ่งครั้งด้วยความละเอียดที่ระบุเฉพาะครั้งนี้
   * @param precision   ระดับความละเอียดที่ต้องการใช้ครั้งนี้
   * @param temperature ตัวรับอุณหภูมิ (ใส่ nullptr ได้)
   * @param humidity    ตัวรับความชื้น (ใส่ nullptr ได้)
   * @return true เมื่อสำเร็จ
   * @note ไม่เปลี่ยนค่าที่ตั้งไว้ด้วย setPrecision()
   */
  bool measureWith(massmore_sht4x_precision_t precision, float *temperature,
                   float *humidity);

  /*!
   * @brief วัดหนึ่งครั้งแล้วคืนเฉพาะอุณหภูมิ
   * @return องศาเซลเซียส หรือ NAN เมื่ออ่านไม่สำเร็จ
   */
  float readTemperature();

  /*!
   * @brief วัดหนึ่งครั้งแล้วคืนเฉพาะอุณหภูมิเป็นฟาเรนไฮต์
   * @return องศาฟาเรนไฮต์ หรือ NAN เมื่ออ่านไม่สำเร็จ
   */
  float readTemperatureF();

  /*!
   * @brief วัดหนึ่งครั้งแล้วคืนเฉพาะความชื้น
   * @return %RH หรือ NAN เมื่ออ่านไม่สำเร็จ
   */
  float readHumidity();

  /* --------------------------------------------------------------------- */
  /* การวัดแบบไม่บล็อก                                                       */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief สั่งชิปเริ่มวัดแล้วคืนทันที ไม่รอ
   *
   * ใช้คู่กับ isMeasurementReady() และ readMeasurement()
   * @return true เมื่อส่งคำสั่งสำเร็จ
   */
  bool startMeasurement();

  /*!
   * @brief ถึงเวลาที่ผลวัดน่าจะพร้อมหรือยัง (คำนวณจากความละเอียดที่ใช้)
   * @return true เมื่อครบเวลาแล้ว
   */
  bool isMeasurementReady() const;

  /*!
   * @brief อ่านผลของ startMeasurement() หรือ startHeater()
   * @param temperature ตัวรับอุณหภูมิ (ใส่ nullptr ได้)
   * @param humidity    ตัวรับความชื้น (ใส่ nullptr ได้)
   * @return true เมื่ออ่านได้และ CRC ถูก, false พร้อม lastError() = ERR_NOT_READY
   *         ถ้ายังไม่ถึงเวลา
   */
  bool readMeasurement(float *temperature, float *humidity);
  bool readMeasurement(massmore_sht4x_reading_t &reading);

  /*!
   * @brief วัดตามจังหวะที่ตั้งไว้ เรียกบ่อย ๆ ใน loop() ได้เลย
   *
   * ฟังก์ชันนี้จัดการวงจร "สั่งวัด -> รอ -> อ่านผล" ให้เอง ไม่บล็อกโปรแกรม
   * ถ้าได้ค่าใหม่จะอัปเดตค่าภายในและเรียก callback ที่ลงทะเบียนไว้
   * @return true เมื่อรอบนี้ได้ค่าใหม่
   * @see setUpdateInterval()
   */
  bool update();

  /*!
   * @brief ตั้งช่วงเวลาระหว่างการวัดของ update() (มิลลิวินาที)
   * @param intervalMs ค่าเริ่มต้น 1000 ms
   * @note อย่าตั้งถี่กว่าที่จำเป็น ชิปที่วัดถี่มากจะอุ่นตัวเองขึ้นเล็กน้อย
   */
  void setUpdateInterval(uint32_t intervalMs);
  /*! @brief อ่านช่วงเวลาที่ตั้งไว้ */
  uint32_t getUpdateInterval() const;

  /*! @brief โหมดปัจจุบัน */
  massmore_sht4x_mode_t getMode() const;

  /*! @brief ลงทะเบียนฟังก์ชันที่จะถูกเรียกเมื่อ update() ได้ค่าใหม่ */
  void setCallback(massmore_sht4x_callback_t callback);

  /* --------------------------------------------------------------------- */
  /* ค่าล่าสุดที่เก็บไว้ภายใน (ไม่คุยกับบัส)                                   */
  /* --------------------------------------------------------------------- */

  /*! @brief อุณหภูมิล่าสุดเป็นองศาเซลเซียส */
  float getTemperature() const;
  /*! @brief อุณหภูมิล่าสุดเป็นองศาฟาเรนไฮต์ */
  float getTemperatureF() const;
  /*! @brief ความชื้นล่าสุดเป็น %RH */
  float getHumidity() const;
  /*! @brief ค่าดิบอุณหภูมิล่าสุด */
  uint16_t getRawTemperature() const;
  /*! @brief ค่าดิบความชื้นล่าสุด */
  uint16_t getRawHumidity() const;
  /*! @brief millis() ตอนที่ได้ค่าล่าสุด */
  uint32_t getLastUpdateMs() const;
  /*! @brief ผลวัดล่าสุดทั้งชุด */
  const massmore_sht4x_reading_t &getLastReading() const;

  /* --------------------------------------------------------------------- */
  /* ฮีตเตอร์                                                               */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief ยิงฮีตเตอร์หนึ่งพัลส์แล้วรอจนจบ (บล็อก)
   *
   * ใช้ไล่หยดน้ำที่เกาะหน้าเซ็นเซอร์ หรือกันค่าเลื่อน (creep) ในที่ชื้นจัด
   * ชิปจะวัดความละเอียดสูงหนึ่งครั้งก่อนดับฮีตเตอร์ ค่านั้นจึงร้อนกว่าความจริง
   *
   * @param mode     โหมดฮีตเตอร์ที่ต้องการ
   * @param reading  ตัวรับผลวัดตอนร้อน (ใส่ nullptr ได้ถ้าไม่สนใจ)
   * @return true เมื่อสำเร็จ, false พร้อม lastError() = ERR_HEATER_DUTY
   *         ถ้ายิงถี่เกินที่ datasheet อนุญาต
   * @note พัลส์ยาวบล็อกโปรแกรมประมาณ 1.1 วินาที ถ้ารับไม่ได้ให้ใช้ startHeater()
   */
  bool runHeater(massmore_sht4x_heater_t mode,
                 massmore_sht4x_reading_t *reading = nullptr);

  /*!
   * @brief สั่งยิงฮีตเตอร์แล้วคืนทันที ไม่รอ (ไม่บล็อก)
   *
   * ใช้คู่กับ isHeaterDone() แล้วอ่านผลด้วย readMeasurement()
   * @param mode โหมดฮีตเตอร์
   * @return true เมื่อส่งคำสั่งสำเร็จ
   */
  bool startHeater(massmore_sht4x_heater_t mode);

  /*! @brief ฮีตเตอร์ยิงครบเวลาแล้วหรือยัง */
  bool isHeaterDone() const;

  /*!
   * @brief ยิงฮีตเตอร์แรงสุดหลายครั้งเพื่อไล่หยดน้ำออกจากหน้าเซ็นเซอร์
   *
   * เหมาะกับตอนเซ็นเซอร์ค้างอยู่ที่ 100 %RH ไม่ยอมลงเพราะมีน้ำเกาะ
   * @param pulses   จำนวนพัลส์ 200 mW 1 วินาที (ค่าเริ่มต้น 3)
   * @param restMs   เวลาพักระหว่างพัลส์เพื่อรักษา duty cycle (ค่าเริ่มต้น 10000 ms)
   * @return true เมื่อยิงครบทุกพัลส์
   * @warning ฟังก์ชันนี้บล็อกนานหลายสิบวินาที ใช้ตอนกู้สถานการณ์เท่านั้น
   */
  bool removeCondensation(uint8_t pulses = 3, uint32_t restMs = 10000);

  /*!
   * @brief วัดค่าจริงหลังจากยิงฮีตเตอร์ โดยรอให้เซ็นเซอร์เย็นลงก่อน
   * @param coolDownMs  เวลารอให้เย็น (ค่าเริ่มต้น 3000 ms)
   * @param reading     ตัวรับผลวัดหลังเย็นแล้ว
   * @return true เมื่อสำเร็จ
   */
  bool measureAfterHeating(uint32_t coolDownMs, massmore_sht4x_reading_t &reading);

  /*!
   * @brief เปิด/ปิดตัวกันเผลอเรื่อง duty cycle ของฮีตเตอร์
   *
   * เปิดไว้ (ค่าเริ่มต้น) ไลบรารีจะจำว่าฮีตเตอร์ทำงานไปแล้วกี่มิลลิวินาที
   * ถ้าสั่งยิงถี่จนเกิน 10% ของเวลาที่ผ่านไป จะปฏิเสธพร้อม ERR_HEATER_DUTY
   * @param enabled true = เปิดตัวกันเผลอ
   */
  void setHeaterDutyGuard(bool enabled);
  /*! @brief ตัวกันเผลอเปิดอยู่หรือไม่ */
  bool getHeaterDutyGuard() const;
  /*! @brief duty cycle ของฮีตเตอร์ที่ใช้ไปแล้วตั้งแต่ begin() คิดเป็นเปอร์เซ็นต์ */
  float getHeaterDutyPercent() const;
  /*! @brief เวลารวมที่ฮีตเตอร์ทำงานไปแล้ว (มิลลิวินาที) */
  uint32_t getHeaterOnTimeMs() const;
  /*! @brief ล้างสถิติ duty cycle เริ่มนับใหม่ */
  void resetHeaterStats();

  /*! @brief กำลังไฟฟ้าของโหมดฮีตเตอร์ที่ระบุ (มิลลิวัตต์) */
  static uint16_t heaterPowerMilliwatt(massmore_sht4x_heater_t mode);
  /*! @brief ระยะเวลาพัลส์ของโหมดฮีตเตอร์ที่ระบุ (มิลลิวินาที) */
  static uint16_t heaterDurationMs(massmore_sht4x_heater_t mode);
  /*! @brief ชื่อโหมดฮีตเตอร์เป็นข้อความ เช่น "200mW/1s" */
  static const char *heaterToString(massmore_sht4x_heater_t mode);

  /* --------------------------------------------------------------------- */
  /* รีเซ็ต                                                                 */
  /* --------------------------------------------------------------------- */

  /*! @brief รีเซ็ตด้วยคำสั่ง 0x94 */
  bool softReset();

  /*!
   * @brief รีเซ็ตอุปกรณ์ทุกตัวบนบัสด้วย I2C general call (address 0x00 data 0x06)
   * @warning อุปกรณ์ I2C ตัวอื่นบนบัสเดียวกันจะถูกรีเซ็ตไปด้วย
   */
  bool generalCallReset();

  /* --------------------------------------------------------------------- */
  /* ตัวตนของชิป                                                            */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief อ่านหมายเลขซีเรียล 32 บิตที่โรงงานเขียนไว้ (คำสั่ง 0x89)
   * @param serial ตัวรับค่า
   * @return true เมื่ออ่านได้และ CRC ทั้งสอง word ถูก
   * @note ชิปเลียนแบบส่วนใหญ่ไม่รู้จักคำสั่ง 0x89 จึงจะ NACK หรือคืน 0x00000000
   */
  bool readSerialNumber(uint32_t *serial);

  /*! @brief ซีเรียลที่อ่านได้ล่าสุด (0 = ยังไม่เคยอ่านสำเร็จ) */
  uint32_t getSerialNumber() const;

  /*!
   * @brief ตรวจสอบว่าเป็นชิป Sensirion SHT4x ของแท้หรือไม่
   *
   * SHT4x ไม่มี status register ให้ตรวจแบบ SHT3x ไลบรารีจึงตรวจจาก
   * "พฤติกรรมและจังหวะเวลา" 10 ข้อ ที่ของเลียนแบบมักทำไม่ครบ
   *   1. ตอบ ACK ที่ address
   *   2. อ่านซีเรียลด้วยคำสั่ง 0x89 ได้ CRC ถูกทั้งสอง word
   *   3. ซีเรียลไม่ใช่ 0x00000000 และไม่ใช่ 0xFFFFFFFF
   *   4. อ่านซีเรียลซ้ำได้ค่าเดิม (ของปลอมที่สุ่มค่าจะตกข้อนี้)
   *   5. สั่ง soft reset 0x94 แล้วชิปกลับมาวัดได้ตามปกติ
   *   6. วัดจริงแล้ว CRC ของทั้งสอง word ถูก
   *   7. ค่าที่วัดได้อยู่ในช่วงที่ซิลิคอนตัวนี้ทำได้ และค่าดิบไม่ใช่ 0x0000/0xFFFF
   *   8. อ่านผลก่อนวัดเสร็จแล้วชิป NACK จริงตาม datasheet (ข้อนี้ของปลอมตกบ่อยสุด)
   *   9. ความละเอียดต่ำ (0xE0) วัดเสร็จเร็วกว่าความละเอียดสูง (0xFD) จริง
   *  10. คำสั่งที่ไม่มีในตาราง (0x77) ต้องไม่ถูกตอบด้วยข้อมูล
   *
   * @warning นี่คือการตรวจสอบเชิงพฤติกรรมระดับโปรโตคอล ไม่ใช่ลายเซ็นดิจิทัล
   *          ตอบได้ว่า "ชิปตัวนี้ทำตัวเหมือน SHT4x แท้ทุกประการหรือไม่"
   *          บอร์ด Massmore ใช้ชิปแท้จาก Sensirion ประกอบในไทย
   * @return ผลสรุป ดูรายละเอียดรายข้อได้จาก getVerifyMask()
   */
  massmore_sht4x_genuine_t verifyChip();

  /*! @brief บิตแมสก์ผลการตรวจรายข้อจาก verifyChip() ครั้งล่าสุด */
  uint16_t getVerifyMask() const;
  /*! @brief จำนวนข้อที่ผ่านจาก verifyChip() ครั้งล่าสุด */
  uint8_t getVerifyPassCount() const;
  /*! @brief ชื่อข้อตรวจลำดับที่ index (0..9) เป็นข้อความสั้น ๆ */
  static const char *getVerifyCheckName(uint8_t index);
  /*! @brief ผลสรุปของ verifyChip() เป็นข้อความ */
  static const char *genuineToString(massmore_sht4x_genuine_t result);

  /* --------------------------------------------------------------------- */
  /* self test                                                             */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief ทดสอบว่าเซ็นเซอร์ยังตอบสนองจริงด้วยการยิงฮีตเตอร์แล้วดูอุณหภูมิ
   *
   * วัดค่าก่อน ยิงฮีตเตอร์ 200 mW แล้วเทียบอุณหภูมิ ถ้าไม่ขยับขึ้นแปลว่า
   * เซ็นเซอร์อาจเสียหรือค่าที่อ่านมาไม่ได้มาจากการวัดจริง
   * @param mode     โหมดฮีตเตอร์ที่ใช้ทดสอบ (ค่าเริ่มต้น 200 mW 1 วินาที)
   * @param minRiseC อุณหภูมิต้องขึ้นอย่างน้อยกี่องศาจึงถือว่าผ่าน
   * @param riseOut  ตัวรับค่าอุณหภูมิที่ขึ้นจริง (ใส่ nullptr ได้)
   * @return true เมื่อผ่าน
   * @note ฟังก์ชันนี้บล็อกประมาณ 1.2 วินาที เหมาะกับใช้ตอนทดสอบเท่านั้น
   */
  bool runHeaterSelfTest(massmore_sht4x_heater_t mode = MASSMORE_SHT4X_HEATER_200MW_1S,
                         float minRiseC = 0.5f, float *riseOut = nullptr);

  /* --------------------------------------------------------------------- */
  /* ค่าที่คำนวณต่อจากอุณหภูมิและความชื้น                                     */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief จุดน้ำค้าง (dew point) ด้วยสูตร Magnus
   * @param temperature องศาเซลเซียส
   * @param humidity    %RH
   * @return องศาเซลเซียส
   */
  static float dewPoint(float temperature, float humidity);

  /*!
   * @brief ความชื้นสัมบูรณ์ (กรัมของไอน้ำต่ออากาศหนึ่งลูกบาศก์เมตร)
   * @param temperature องศาเซลเซียส
   * @param humidity    %RH
   * @return g/m^3
   */
  static float absoluteHumidity(float temperature, float humidity);

  /*!
   * @brief ดัชนีความร้อน (heat index) สูตร Rothfusz ของ NOAA
   * @param temperature องศาเซลเซียส
   * @param humidity    %RH
   * @return องศาเซลเซียส
   */
  static float heatIndex(float temperature, float humidity);

  /*!
   * @brief ความดันไออิ่มตัวที่อุณหภูมินั้น
   * @param temperature องศาเซลเซียส
   * @return hPa
   */
  static float saturationVaporPressure(float temperature);

  /*! @brief แปลงเซลเซียสเป็นฟาเรนไฮต์ */
  static float celsiusToFahrenheit(float celsius);
  /*! @brief แปลงฟาเรนไฮต์เป็นเซลเซียส */
  static float fahrenheitToCelsius(float fahrenheit);

  /* --------------------------------------------------------------------- */
  /* ยูทิลิตี้ระดับล่าง เผื่อผู้ใช้ต่อยอดเอง                                    */
  /* --------------------------------------------------------------------- */

  /*!
   * @brief คำนวณ CRC-8 ตามสเปกของ Sensirion
   * @param data ตัวชี้ข้อมูล
   * @param length จำนวนไบต์
   * @return ค่า CRC
   */
  static uint8_t crc8(const uint8_t *data, uint8_t length);

  /*!
   * @brief ส่งคำสั่ง 1 ไบต์ดิบ ๆ ไปยังชิป
   * @param command คำสั่ง เช่น MASSMORE_SHT4X_CMD_SOFT_RESET
   * @return true เมื่อชิปตอบ ACK
   */
  bool sendCommand(uint8_t command);

  /*!
   * @brief อ่านข้อมูลดิบต่อจากคำสั่งล่าสุด พร้อมตรวจ CRC ทุก 3 ไบต์
   * @param buffer  บัฟเฟอร์ปลายทาง
   * @param length  จำนวนไบต์ที่ต้องการ (ต้องหารด้วย 3 ลงตัว)
   * @return true เมื่ออ่านครบและ CRC ถูกทุก word
   */
  bool readBytes(uint8_t *buffer, uint8_t length);

  /*!
   * @brief ส่งคำสั่ง รอตามเวลาที่กำหนด แล้วอ่านสอง word กลับมา
   * @param command คำสั่ง 1 ไบต์
   * @param waitMs  เวลารอก่อนอ่าน
   * @param first   ตัวรับ word แรก
   * @param second  ตัวรับ word ที่สอง
   * @return true เมื่อสำเร็จ
   */
  bool commandAndRead(uint8_t command, uint16_t waitMs, uint16_t *first,
                      uint16_t *second);

  /*! @brief แปลงค่าดิบ 16 บิตเป็นองศาเซลเซียส */
  static float rawToCelsius(uint16_t raw);
  /*! @brief แปลงค่าดิบ 16 บิตเป็นองศาฟาเรนไฮต์ */
  static float rawToFahrenheit(uint16_t raw);
  /*! @brief แปลงค่าดิบ 16 บิตเป็น %RH (ยังไม่ตัดขอบ) */
  static float rawToHumidity(uint16_t raw);
  /*! @brief แปลงองศาเซลเซียสกลับเป็นค่าดิบ 16 บิต */
  static uint16_t celsiusToRaw(float celsius);
  /*! @brief แปลง %RH กลับเป็นค่าดิบ 16 บิต */
  static uint16_t humidityToRaw(float humidity);
  /*! @brief คำสั่งวัดที่ตรงกับระดับความละเอียดที่ระบุ */
  static uint8_t precisionCommand(massmore_sht4x_precision_t precision);
  /*! @brief เวลาที่ต้องรอของระดับความละเอียดที่ระบุ (มิลลิวินาที) */
  static uint16_t precisionDurationMs(massmore_sht4x_precision_t precision);
  /*! @brief คำสั่งฮีตเตอร์ที่ตรงกับโหมดที่ระบุ */
  static uint8_t heaterCommand(massmore_sht4x_heater_t mode);

  /* --------------------------------------------------------------------- */
  /* การรายงานข้อผิดพลาด                                                    */
  /* --------------------------------------------------------------------- */

  /*! @brief รหัสข้อผิดพลาดล่าสุด */
  massmore_sht4x_error_t lastError() const;
  /*! @brief คำอธิบายข้อผิดพลาดล่าสุดเป็นภาษาไทย */
  const char *lastErrorString() const;
  /*! @brief แปลงรหัสข้อผิดพลาดเป็นข้อความ */
  static const char *errorToString(massmore_sht4x_error_t error);
  /*! @brief ล้างรหัสข้อผิดพลาดกลับเป็น OK */
  void clearError();

  /*! @brief address ที่ใช้อยู่ */
  uint8_t getAddress() const;
  /*! @brief เวอร์ชันไลบรารีเป็นข้อความ */
  static const char *getLibraryVersion();

  /*!
   * @brief สแกนหา SHT4x บนบัสที่กำหนด (ลอง 0x44, 0x45 และ 0x46)
   * @param wire  บัสที่จะสแกน
   * @param found อาเรย์รับ address ที่พบ ต้องมีที่ว่างอย่างน้อย 3 ช่อง
   * @return จำนวนที่พบ (0-3)
   */
  static uint8_t scan(TwoWire *wire, uint8_t *found);

private:
  TwoWire *_wire;
  uint8_t _address;
  bool _begun;
  uint16_t _timeoutMs;

  massmore_sht4x_variant_t _variant;
  massmore_sht4x_package_t _package;
  massmore_sht4x_precision_t _precision;
  massmore_sht4x_mode_t _mode;
  bool _humidityClipping;

  float _temperatureOffset;
  float _humidityOffset;

  massmore_sht4x_reading_t _reading;
  uint32_t _operationStartMs;   /*!< millis() ตอนส่งคำสั่งวัด/ฮีตเตอร์ */
  uint16_t _operationDurationMs;/*!< เวลาที่ต้องรอของงานที่ค้างอยู่ */
  bool _pendingHeated;          /*!< งานที่ค้างอยู่มาจากคำสั่งฮีตเตอร์หรือไม่ */

  uint32_t _updateIntervalMs;
  uint32_t _lastUpdateStartMs;

  bool _heaterDutyGuard;
  uint32_t _heaterOnTimeMs;
  uint32_t _heaterEpochMs;

  uint32_t _serialNumber;
  uint16_t _verifyMask;
  massmore_sht4x_genuine_t _genuine;

  massmore_sht4x_callback_t _callback;
  massmore_sht4x_error_t _error;

  /* ตัวช่วยภายใน */
  bool readFrame(uint16_t *rawT, uint16_t *rawRH);
  void storeReading(uint16_t rawT, uint16_t rawRH, bool heated);
  bool heaterDutyAllows(uint16_t durationMs) const;
  void accountHeater(uint16_t durationMs);
  bool setError(massmore_sht4x_error_t error);
};

#endif /* MASSMORE_SHT4X_H */
