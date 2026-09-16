/**
 * @file    Massmore_SHT4x.h
 * @brief   ไลบรารี Arduino / PlatformIO สำหรับเซ็นเซอร์อุณหภูมิและความชื้น
 *          Sensirion SHT4x (SHT40 / SHT41 / SHT45) บนบอร์ด Massmore SHT4X (SKU-1022)
 *
 * คุณสมบัติหลัก
 *   - เขียนจาก Datasheet SHT4x V7.3 โดยตรง ทุกค่าคงที่ระบุที่มาไว้ในโค้ด
 *   - ไม่ hardcode GPIO และไม่เรียก Wire.begin() ในไลบรารี (sketch เป็นเจ้าของ I2C Bus)
 *   - Dual API: Simple Blocking API และ Non-blocking FSM (rollover-safe millis())
 *   - Zero heap: ไม่มี new / malloc / String ในไลบรารี ใช้ได้กับ ATmega328P
 *   - Heater ครบ 6 โหมด พร้อม duty-cycle guard ตามข้อกำหนดของ datasheet (< 10 %)
 *   - verifyChipID() / getSerialNumber() / isGenuine() สำหรับตรวจของแท้
 *
 * ข้อเท็จจริงของชิปที่ควรทราบ (จาก datasheet)
 *   - คำสั่งยาว 1 byte ไม่มี register map ชิปตอบข้อมูลเป็น word 16-bit + CRC-8
 *   - ไม่รองรับ clock stretching: ถ้าอ่านผลก่อนวัดเสร็จ ชิปจะ NACK
 *   - ไม่มี CHIP_ID / WHO_AM_I register มีเพียง Serial Number 32-bit (คำสั่ง 0x89)
 *   - I2C address กำหนดจากรหัสรุ่น (0x44 / 0x45 / 0x46) ไม่มีขา ADDR
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license   MIT
 */

#ifndef MASSMORE_SHT4X_H
#define MASSMORE_SHT4X_H

#include <Arduino.h>
#include <Wire.h>

#define MASSMORE_SHT4X_VERSION "2.0.0"

/**
 * @brief ไดรเวอร์ SHT4x หนึ่งตัว (สร้างหลายอ็อบเจกต์บนหลาย Bus ได้)
 */
class Massmore_SHT4x {
public:
  /* ===================================================================== */
  /* ค่าคงที่จาก Datasheet SHT4x V7.3                                       */
  /* ===================================================================== */

  /** I2C address ตามรหัสรุ่น (datasheet §4.1 / ตาราง Product Naming) */
  static const uint8_t I2C_ADDR_A = 0x44;  ///< SHT40-AD1B / SHT41-AD1B / SHT45-AD1B (บอร์ด Massmore)
  static const uint8_t I2C_ADDR_B = 0x45;  ///< SHT40-BD1B
  static const uint8_t I2C_ADDR_C = 0x46;  ///< SHT40-CD1B
  static const uint8_t I2C_ADDR_DEFAULT = I2C_ADDR_A;

  /** ความถี่ I2C สูงสุดที่ชิปรองรับ (datasheet: 1 MHz) */
  static const uint32_t I2C_MAX_CLOCK_HZ = 1000000UL;

  /** ช่วงค่าทางกายภาพ (datasheet: Operating range) */
  static constexpr float TEMP_MIN_C = -40.0f;
  static constexpr float TEMP_MAX_C = 125.0f;
  static constexpr float HUMI_MIN_PERCENT = 0.0f;
  static constexpr float HUMI_MAX_PERCENT = 100.0f;

  /* ===================================================================== */
  /* Enums / Structs (nested เพื่อไม่ชนกับไลบรารีอื่น)                       */
  /* ===================================================================== */

  /** @brief รหัสข้อผิดพลาด ดูได้จาก lastError() หลังฟังก์ชันคืน false / NAN */
  enum class ErrorCode : uint8_t {
    OK = 0,        ///< สำเร็จ
    NOT_BEGUN,     ///< ยังไม่ได้เรียก begin()
    NOT_FOUND,     ///< ไม่มีอุปกรณ์ ACK ที่ address นี้
    WRONG_ID,      ///< Serial Number อ่านได้แต่ค่าไม่สมเหตุสมผล (0x00000000 / 0xFFFFFFFF)
    TIMEOUT,       ///< ชิป NACK นานเกิน timeout (วัดไม่เสร็จ หรือหลุดจาก Bus)
    CRC_FAIL,      ///< CRC-8 ของข้อมูลไม่ตรง
    BUS_ERROR,     ///< endTransmission() คืนค่าผิดพลาด
    NOT_READY,     ///< FSM: ยังไม่มีผลใหม่ให้อ่าน
    BUSY,          ///< FSM: มีงานค้างอยู่ (กำลังวัดหรือกำลังใช้ Heater)
    BAD_ARG,       ///< พารามิเตอร์ไม่ถูกต้อง
    HEATER_DUTY    ///< สั่ง Heater ถี่เกิน duty cycle 10 % ที่ datasheet อนุญาต
  };

  /** @brief ระดับความละเอียดของการวัด (datasheet ตาราง "Measurement commands") */
  enum class Precision : uint8_t {
    LOW_RES = 0,   ///< คำสั่ง 0xE0  ~1.3 ms (max 1.6 ms)  noise สูงสุด ประหยัดไฟสุด
    MEDIUM_RES,    ///< คำสั่ง 0xF6  ~3.7 ms (max 4.5 ms)
    HIGH_RES       ///< คำสั่ง 0xFD  ~6.9 ms (max 8.3 ms)  noise ต่ำสุด (ค่าเริ่มต้น)
  };

  /**
   * @brief โหมด Heater (datasheet ตาราง "Heater commands")
   * ทุกโหมดชิปจะวัดแบบ HIGH_RES หนึ่งครั้งก่อนปิด Heater ให้อัตโนมัติ
   * ค่าที่ได้ "ร้อนกว่าจริง" ใช้ดูแนวโน้มเท่านั้น
   */
  enum class HeaterMode : uint8_t {
    MW200_1S = 0,  ///< 0x39  200 mW 1 s   (แรงสุด ใช้ไล่หยดน้ำ)
    MW200_100MS,   ///< 0x32  200 mW 0.1 s
    MW110_1S,      ///< 0x2F  110 mW 1 s
    MW110_100MS,   ///< 0x24  110 mW 0.1 s
    MW20_1S,       ///< 0x1E  20 mW 1 s    (กัน creep ในที่ชื้นจัด)
    MW20_100MS     ///< 0x15  20 mW 0.1 s  (เบาสุด)
  };

  /**
   * @brief รุ่นของชิป ใช้เฉพาะแสดงผลและระบุ accuracy spec
   * SHT40/41/45 อ่านแยกผ่าน I2C ไม่ได้ (datasheet ไม่มี register บอกรุ่น)
   * ให้ดูช่องที่ติ๊กบน silkscreen ของบอร์ดแล้วบอกไลบรารีเอง
   */
  enum class Variant : uint8_t {
    UNKNOWN = 0,
    SHT40,   ///< ±1.8 %RH, ±0.2 °C
    SHT41,   ///< ±1.8 %RH, ±0.2 °C
    SHT45    ///< ±1.0 %RH, ±0.1 °C
  };

  /** @brief สถานะของ Non-blocking FSM */
  enum class State : uint8_t {
    IDLE = 0,     ///< ว่าง ไม่มีงานค้าง
    MEASURING,    ///< ส่งคำสั่งวัดแล้ว รอผล
    HEATING,      ///< ส่งคำสั่ง Heater แล้ว รอผล
    DATA_READY    ///< มีผลใหม่ รอให้เรียก getReadings()
  };

  /** @brief ผลวัดหนึ่งชุด */
  struct Readings {
    float temperature;        ///< °C (รวม offset แล้ว)
    float humidity;           ///< %RH (รวม offset และ clip 0–100 แล้ว)
    uint16_t rawTemperature;  ///< ค่าดิบ 16-bit จากชิป
    uint16_t rawHumidity;     ///< ค่าดิบ 16-bit จากชิป
    uint32_t timestampMs;     ///< millis() ตอนอ่านผลได้
    bool heated;              ///< true = ผลนี้มาจากคำสั่ง Heater (ร้อนกว่าจริง)
  };

  /** Bitmask ผลตรวจรายข้อของ isGenuine() ดูจาก getGenuineMask() */
  static const uint16_t CHK_ACK = (1u << 0);            ///< ชิป ACK ที่ address
  static const uint16_t CHK_SERIAL_CRC = (1u << 1);     ///< อ่าน Serial ได้ CRC ถูกทั้ง 2 word
  static const uint16_t CHK_SERIAL_SANE = (1u << 2);    ///< Serial ไม่ใช่ 0 / 0xFFFFFFFF
  static const uint16_t CHK_SERIAL_STABLE = (1u << 3);  ///< อ่าน Serial ซ้ำได้ค่าเดิม
  static const uint16_t CHK_SOFT_RESET = (1u << 4);     ///< soft reset (0x94) แล้ววัดต่อได้
  static const uint16_t CHK_MEAS_CRC = (1u << 5);       ///< ผลวัด CRC ถูกทั้ง 2 word
  static const uint16_t CHK_MEAS_RANGE = (1u << 6);     ///< ผลวัดอยู่ในช่วงกายภาพ
  static const uint16_t CHK_NACK_EARLY = (1u << 7);     ///< อ่านก่อนวัดเสร็จแล้วชิป NACK (datasheet §4.3)
  static const uint16_t CHK_LOW_FASTER = (1u << 8);     ///< LOW_RES เสร็จเร็วกว่า HIGH_RES จริง
  static const uint16_t CHK_ALL = 0x01FFu;
  static const uint8_t CHK_COUNT = 9;

  /* ===================================================================== */
  /* Construction / begin                                                  */
  /* ===================================================================== */

  /**
   * @brief  สร้างอ็อบเจกต์ โดยรับ reference ของ I2C Bus ที่จะใช้
   * @param  wirePort  TwoWire ที่ sketch เรียก begin() ไว้แล้ว (ค่าเริ่มต้น Wire)
   */
  explicit Massmore_SHT4x(TwoWire &wirePort = Wire);

  /**
   * @brief  เริ่มใช้งาน: ตรวจ ACK, soft reset และอ่าน Serial Number เพื่อยืนยันชิป
   * @note   sketch ต้องเรียก Wire.begin(...) ก่อน ไลบรารีไม่แตะ Bus lifecycle
   * @param  address  0x44 (บอร์ด Massmore), 0x45 หรือ 0x46
   * @param  variant  รุ่นของชิปตาม silkscreen (ใช้แสดงผลเท่านั้น)
   * @return true เมื่อพบชิปและอ่าน Serial ได้ (ดู lastError() เมื่อ false)
   */
  bool begin(uint8_t address = I2C_ADDR_DEFAULT, Variant variant = Variant::UNKNOWN);

  /**
   * @brief  เริ่มใช้งานพร้อมเปลี่ยน I2C Bus (เช่น Wire1 บน ESP32)
   * @param  wirePort  TwoWire ที่ sketch เรียก begin() ไว้แล้ว
   * @param  address   I2C address
   * @param  variant   รุ่นของชิป
   * @return true เมื่อสำเร็จ
   */
  bool begin(TwoWire &wirePort, uint8_t address, Variant variant = Variant::UNKNOWN);

  /**
   * @brief  ตรวจว่าชิปยัง ACK บน Bus หรือไม่ (ส่ง address เปล่า)
   * @return true ถ้าได้ ACK
   */
  bool isConnected();

  /** @brief ตั้ง timeout (ms) สำหรับรอชิปหยุด NACK หลังวัด (ค่าเริ่มต้น 50 ms) */
  void setTimeout(uint16_t timeoutMs);

  /* ===================================================================== */
  /* Simple Blocking API                                                   */
  /* ===================================================================== */

  /**
   * @brief  อ่านอุณหภูมิแบบ Blocking (รอจน conversion เสร็จ)
   * @param  precision  ระดับความละเอียด
   * @return float °C หรือ NAN หากอ่านไม่สำเร็จ (ดู lastError())
   */
  float readTemperature(Precision precision = Precision::HIGH_RES);

  /**
   * @brief  อ่านความชื้นแบบ Blocking
   * @param  precision  ระดับความละเอียด
   * @return float %RH หรือ NAN หากอ่านไม่สำเร็จ
   */
  float readHumidity(Precision precision = Precision::HIGH_RES);

  /**
   * @brief  วัดครั้งเดียวได้ทั้งอุณหภูมิและความชื้น (แนะนำ ประหยัดเวลาและพลังงานกว่าอ่านแยก)
   * @param  out        struct รับผล
   * @param  precision  ระดับความละเอียด
   * @return true เมื่อสำเร็จ
   */
  bool readAll(Readings &out, Precision precision = Precision::HIGH_RES);

  /**
   * @brief  วัดครั้งเดียวได้ทั้งสองค่า (รูปแบบ float สำหรับผู้เริ่มต้น)
   * @param  temperature  ตัวรับ °C
   * @param  humidity     ตัวรับ %RH
   * @param  precision    ระดับความละเอียด
   * @return true เมื่อสำเร็จ
   */
  bool readAll(float &temperature, float &humidity,
               Precision precision = Precision::HIGH_RES);

  /* ===================================================================== */
  /* Advanced Non-blocking FSM                                             */
  /* ===================================================================== */

  /**
   * @brief  สั่งชิปเริ่มวัดแล้วคืนทันที ไม่รอ
   * @param  precision  ระดับความละเอียด
   * @return true เมื่อส่งคำสั่งสำเร็จ (false + BUSY ถ้ามีงานค้าง)
   */
  bool requestConversion(Precision precision = Precision::HIGH_RES);

  /**
   * @brief  สั่ง Heater แบบ Non-blocking (ผลอ่านผ่าน getReadings() เหมือนการวัดปกติ)
   * @param  mode  โหมด Heater
   * @return true เมื่อส่งคำสั่งสำเร็จ (false + HEATER_DUTY ถ้ายิงถี่เกิน)
   */
  bool requestHeater(HeaterMode mode);

  /**
   * @brief  ขับเคลื่อน FSM ต้องเรียกบ่อย ๆ ใน loop() ไม่ Block
   * @return true เมื่อรอบนี้ได้ผลใหม่ (state เปลี่ยนเป็น DATA_READY)
   */
  bool update();

  /** @brief มีผลใหม่รออยู่หรือไม่ */
  bool isDataReady() const;

  /**
   * @brief  รับผลล่าสุดจาก FSM แล้วล้างสถานะ DATA_READY
   * @param  out  struct รับผล
   * @return true เมื่อมีผลใหม่ (false + NOT_READY ถ้ายังไม่มี)
   */
  bool getReadings(Readings &out);

  /** @brief สถานะปัจจุบันของ FSM */
  State getState() const;

  /** @brief ผลล่าสุดที่เก็บไว้ (ไม่คุยกับ Bus) ใช้ได้ทั้ง Blocking และ FSM */
  const Readings &lastReadings() const;

  /* ===================================================================== */
  /* Heater (Blocking)                                                     */
  /* ===================================================================== */

  /**
   * @brief  ยิง Heater หนึ่ง pulse แล้วรอจนจบ (Block 0.11 s หรือ 1.1 s)
   * @param  mode  โหมด Heater
   * @param  out   ตัวรับผลวัดตอนร้อน (nullptr ได้)
   * @return true เมื่อสำเร็จ
   */
  bool runHeater(HeaterMode mode, Readings *out = nullptr);

  /** @brief เปิด/ปิด duty-cycle guard (ค่าเริ่มต้นเปิด) */
  void setHeaterDutyGuard(bool enabled);

  /** @brief เวลาที่ต้องรอ (ms) ก่อนยิง Heater ครั้งถัดไปโดยไม่เกิน duty cycle 10 % */
  uint32_t heaterCooldownRemainingMs() const;

  /** @brief กำลังของโหมด Heater (mW) */
  static uint16_t heaterPowerMilliwatt(HeaterMode mode);
  /** @brief ระยะเวลา pulse ของโหมด Heater (ms) */
  static uint16_t heaterDurationMs(HeaterMode mode);

  /* ===================================================================== */
  /* Chip identity (ตรวจของแท้)                                             */
  /* ===================================================================== */

  /**
   * @brief  ยืนยันตัวตนชิป: SHT4x ไม่มี CHIP_ID register จึงใช้ Serial Number
   *         (คำสั่ง 0x89) แทน — ต้องอ่านได้, CRC ถูกทั้ง 2 word และค่าไม่ใช่ 0 / 0xFFFFFFFF
   * @return true เมื่อผ่าน (false + WRONG_ID / CRC_FAIL / TIMEOUT)
   */
  bool verifyChipID();

  /**
   * @brief  อ่าน Serial Number 32-bit จากโรงงาน (คำสั่ง 0x89)
   * @param  serial  ตัวรับค่า
   * @return true เมื่ออ่านได้และ CRC ถูก
   */
  bool readSerialNumber(uint32_t &serial);

  /** @brief Serial Number ที่อ่านได้ล่าสุด (0 = ยังไม่เคยอ่านสำเร็จ) */
  uint32_t getSerialNumber() const;

  /**
   * @brief  Heuristic ตรวจของแท้ 9 ข้อ อิงพฤติกรรมที่ datasheet ระบุเท่านั้น
   *         (Serial + CRC, soft reset, NACK ตอนยังวัดไม่เสร็จ, เวลาวัดของแต่ละ precision)
   * @note   ใช้เวลา ~50 ms และจะ soft reset ชิปตอนจบ
   * @return true = GENUINE (ผ่านครบ 9 ข้อ) ดูรายข้อจาก getGenuineMask()
   */
  bool isGenuine();

  /** @brief Bitmask รายข้อจาก isGenuine() ครั้งล่าสุด (CHK_*) */
  uint16_t getGenuineMask() const;
  /** @brief จำนวนข้อที่ผ่านจาก isGenuine() ครั้งล่าสุด */
  uint8_t getGenuinePassCount() const;
  /** @brief ชื่อข้อตรวจ (English, สำหรับ Factory Test) index 0..8 */
  static const char *genuineCheckName(uint8_t index);

  /* ===================================================================== */
  /* Reset                                                                 */
  /* ===================================================================== */

  /** @brief Soft reset ด้วยคำสั่ง 0x94 (รอ 1 ms ตาม datasheet) */
  bool softReset();

  /**
   * @brief   I2C General Call reset (address 0x00, data 0x06)
   * @warning อุปกรณ์ตัวอื่นบน Bus เดียวกันที่รองรับ General Call จะถูก reset ด้วย
   */
  bool generalCallReset();

  /* ===================================================================== */
  /* Configuration                                                         */
  /* ===================================================================== */

  /** @brief ชดเชยอุณหภูมิ (°C) บวกเข้ากับทุกค่าที่อ่าน เช่นกรณีติดใกล้ MCU ที่ร้อน */
  void setTemperatureOffset(float offsetC);
  /** @brief ชดเชยความชื้น (%RH) */
  void setHumidityOffset(float offsetPercent);
  float getTemperatureOffset() const;
  float getHumidityOffset() const;

  /** @brief ระบุรุ่นชิปตาม silkscreen */
  void setVariant(Variant variant);
  Variant getVariant() const;
  /** @brief ชื่อรุ่นเป็นข้อความ เช่น "SHT45" */
  const char *getVariantName() const;
  /** @brief accuracy spec อุณหภูมิของรุ่นที่ตั้งไว้ (±°C) */
  float getTemperatureAccuracy() const;
  /** @brief accuracy spec ความชื้นของรุ่นที่ตั้งไว้ (±%RH) */
  float getHumidityAccuracy() const;

  /** @brief address ที่ใช้อยู่ */
  uint8_t getAddress() const;

  /* ===================================================================== */
  /* Derived values (static, ไม่คุยกับ Bus)                                  */
  /* ===================================================================== */

  /** @brief จุดน้ำค้าง (°C) สูตร Magnus */
  static float dewPoint(float temperatureC, float humidityPercent);
  /** @brief ความชื้นสัมบูรณ์ (g/m³) */
  static float absoluteHumidity(float temperatureC, float humidityPercent);
  /** @brief Heat index (°C) สูตร Rothfusz (NOAA) */
  static float heatIndex(float temperatureC, float humidityPercent);

  /** @brief แปลงค่าดิบเป็น °C:  T = -45 + 175 · S_T / 65535 (datasheet §4.6) */
  static float rawToCelsius(uint16_t raw);
  /** @brief แปลงค่าดิบเป็น %RH: RH = -6 + 125 · S_RH / 65535 (ยังไม่ clip) */
  static float rawToHumidity(uint16_t raw);

  /** @brief CRC-8 ของ Sensirion: poly 0x31, init 0xFF, no reflection, no final XOR */
  static uint8_t crc8(const uint8_t *data, uint8_t length);

  /* ===================================================================== */
  /* Error reporting                                                       */
  /* ===================================================================== */

  /** @brief รหัสข้อผิดพลาดล่าสุด */
  ErrorCode lastError() const;
  /** @brief ข้อความข้อผิดพลาด (English, สั้น เหมาะกับ Serial log) */
  static const char *errorToString(ErrorCode error);
  /** @brief ข้อความข้อผิดพลาดล่าสุด */
  const char *lastErrorString() const;

  /**
   * @brief  สแกนหา SHT4x บน Bus (ลอง 0x44, 0x45, 0x46)
   * @param  wirePort  Bus ที่จะสแกน (ต้อง begin() แล้ว)
   * @param  found     array รับ address อย่างน้อย 3 ช่อง
   * @return จำนวนที่พบ (0..3)
   */
  static uint8_t scan(TwoWire &wirePort, uint8_t *found);

  /** @brief เวอร์ชันไลบรารี */
  static const char *version();

private:
  /* ---- คำสั่ง 1 byte จาก datasheet ---- */
  static const uint8_t CMD_MEAS_HIGH = 0xFD;
  static const uint8_t CMD_MEAS_MED = 0xF6;
  static const uint8_t CMD_MEAS_LOW = 0xE0;
  static const uint8_t CMD_READ_SERIAL = 0x89;
  static const uint8_t CMD_SOFT_RESET = 0x94;
  static const uint8_t CMD_HEATER_200MW_1S = 0x39;
  static const uint8_t CMD_HEATER_200MW_0S1 = 0x32;
  static const uint8_t CMD_HEATER_110MW_1S = 0x2F;
  static const uint8_t CMD_HEATER_110MW_0S1 = 0x24;
  static const uint8_t CMD_HEATER_20MW_1S = 0x1E;
  static const uint8_t CMD_HEATER_20MW_0S1 = 0x15;
  static const uint8_t GENERAL_CALL_ADDR = 0x00;
  static const uint8_t GENERAL_CALL_RESET = 0x06;

  /* ---- เวลารอ (ms) = ค่า max ใน datasheet + margin ---- */
  static const uint8_t MEAS_LOW_MS = 2;      ///< max 1.6 ms
  static const uint8_t MEAS_MED_MS = 5;      ///< max 4.5 ms
  static const uint8_t MEAS_HIGH_MS = 9;     ///< max 8.3 ms
  static const uint16_t HEATER_LONG_MS = 1100;  ///< max 1.1 s (รวมการวัด)
  static const uint8_t HEATER_SHORT_MS = 110;   ///< max 0.11 s
  static const uint8_t SOFT_RESET_MS = 1;    ///< max 1 ms
  static const uint8_t SERIAL_READ_MS = 1;   ///< เวลาให้ชิปเตรียมข้อมูล serial

  static const uint8_t FRAME_LEN = 6;        ///< word(2)+CRC(1)+word(2)+CRC(1)
  static const uint8_t CRC8_POLY = 0x31;
  static const uint8_t CRC8_INIT = 0xFF;
  static const uint16_t TIMEOUT_DEFAULT_MS = 50;
  static const uint8_t HEATER_MAX_DUTY_PERCENT = 10;

  TwoWire *_wire;
  uint8_t _address;
  bool _begun;
  uint16_t _timeoutMs;
  Variant _variant;
  float _temperatureOffset;
  float _humidityOffset;

  Readings _reading;
  ErrorCode _error;

  /* FSM */
  State _state;
  uint32_t _opStartMs;
  uint16_t _opDurationMs;
  bool _opHeated;

  /* Heater duty guard */
  bool _dutyGuard;
  uint32_t _heaterEndMs;
  uint16_t _heaterLastDurationMs;
  bool _heaterUsed;

  /* Genuine check */
  uint32_t _serialNumber;
  uint16_t _genuineMask;

  /* Low-level I2C */
  bool sendCommand(uint8_t command);
  bool readFrame(uint16_t &first, uint16_t &second);
  bool readFrameWithRetry(uint16_t &first, uint16_t &second, uint16_t timeoutMs);
  bool measureBlocking(uint8_t command, uint16_t waitMs, bool heated);
  void storeReading(uint16_t rawT, uint16_t rawRH, bool heated);
  bool heaterAllowed(uint16_t durationMs) const;
  void accountHeater(uint16_t durationMs);
  bool setError(ErrorCode error);

  static uint8_t precisionCommand(Precision precision);
  static uint8_t precisionDurationMs(Precision precision);
  static uint8_t heaterCommand(HeaterMode mode);
};

#endif /* MASSMORE_SHT4X_H */
