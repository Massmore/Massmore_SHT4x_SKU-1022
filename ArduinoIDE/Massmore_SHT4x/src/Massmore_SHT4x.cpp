/*!
 * @file Massmore_SHT4x.cpp
 * @brief การทำงานภายในของไลบรารี Massmore SHT4x
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "Massmore_SHT4x.h"

#include <math.h>

/* ค่าคงที่สำหรับสูตร Magnus (ใช้ทั้ง dewPoint และความดันไออิ่มตัว) */
#define MASSMORE_SHT4X_MAGNUS_A 17.62f
#define MASSMORE_SHT4X_MAGNUS_B 243.12f

/* เวลาที่ใช้ "แหย่" ดูว่าชิป NACK ตอนยังวัดไม่เสร็จจริงไหม (ใช้ใน verifyChip) */
#define MASSMORE_SHT4X_PROBE_EARLY_MS 2

/* ========================================================================= */
/* ตัวสร้าง                                                                  */
/* ========================================================================= */

MassmoreSHT4x::MassmoreSHT4x(TwoWire *wire)
    : _wire(wire), _address(MASSMORE_SHT4X_I2C_ADDR_DEFAULT), _begun(false),
      _timeoutMs(MASSMORE_SHT4X_TIMEOUT_DEFAULT_MS),
      _variant(MASSMORE_SHT4X_VARIANT_AUTO),
      _package(MASSMORE_SHT4X_PACKAGE_UNKNOWN),
      _precision(MASSMORE_SHT4X_PRECISION_HIGH),
      _mode(MASSMORE_SHT4X_MODE_IDLE), _humidityClipping(true),
      _temperatureOffset(0.0f), _humidityOffset(0.0f), _operationStartMs(0),
      _operationDurationMs(0), _pendingHeated(false), _updateIntervalMs(1000),
      _lastUpdateStartMs(0), _heaterDutyGuard(true), _heaterOnTimeMs(0),
      _heaterEpochMs(0), _serialNumber(0), _verifyMask(0),
      _genuine(MASSMORE_SHT4X_GENUINE_UNKNOWN), _callback(nullptr),
      _error(MASSMORE_SHT4X_OK) {
  _reading.temperature = NAN;
  _reading.humidity = NAN;
  _reading.rawTemperature = 0;
  _reading.rawHumidity = 0;
  _reading.timestampMs = 0;
  _reading.heated = false;
}

/* ========================================================================= */
/* การเริ่มต้นใช้งาน                                                          */
/* ========================================================================= */

bool MassmoreSHT4x::begin(uint8_t address, massmore_sht4x_variant_t variant,
                          int8_t sdaPin, int8_t sclPin, uint32_t frequency) {
  if (_wire == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  if (address != MASSMORE_SHT4X_I2C_ADDR_A && address != MASSMORE_SHT4X_I2C_ADDR_B &&
      address != MASSMORE_SHT4X_I2C_ADDR_C) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  /* ESP32 และ ESP8266 เลือกขา I2C ได้อิสระ */
  if (sdaPin >= 0 && sclPin >= 0) {
    _wire->begin((int)sdaPin, (int)sclPin);
  } else {
    _wire->begin();
  }
  _wire->setClock(frequency);
#else
  /* บอร์ดอื่นใช้ขาตายตัวของฮาร์ดแวร์ */
  (void)sdaPin;
  (void)sclPin;
  _wire->begin();
  _wire->setClock(frequency);
#endif

  return beginWithExistingBus(address, variant);
}

bool MassmoreSHT4x::beginWithExistingBus(uint8_t address,
                                         massmore_sht4x_variant_t variant) {
  if (_wire == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  if (address != MASSMORE_SHT4X_I2C_ADDR_A && address != MASSMORE_SHT4X_I2C_ADDR_B &&
      address != MASSMORE_SHT4X_I2C_ADDR_C) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }

  _address = address;
  _variant = variant;
  _begun = true;
  _mode = MASSMORE_SHT4X_MODE_READY;

  /* เริ่มนับ duty cycle ของฮีตเตอร์ใหม่ทุกครั้งที่ begin() */
  resetHeaterStats();

  if (!softReset()) {
    _begun = false;
    _mode = MASSMORE_SHT4X_MODE_IDLE;
    return false;
  }

  /* ยืนยันว่าชิปคุยได้จริงด้วยการอ่านซีเรียลจากโรงงาน */
  uint32_t serial = 0;
  if (!readSerialNumber(&serial)) {
    _begun = false;
    _mode = MASSMORE_SHT4X_MODE_IDLE;
    return false;
  }

  clearError();
  return true;
}

bool MassmoreSHT4x::isConnected() {
  if (_wire == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  _wire->beginTransmission(_address);
  if (_wire->endTransmission() != 0) {
    return setError(MASSMORE_SHT4X_ERR_NO_DEVICE);
  }
  return true;
}

void MassmoreSHT4x::setTimeout(uint16_t milliseconds) { _timeoutMs = milliseconds; }

/* ========================================================================= */
/* การตั้งค่า                                                                 */
/* ========================================================================= */

void MassmoreSHT4x::setPrecision(massmore_sht4x_precision_t precision) {
  _precision = precision;
}

massmore_sht4x_precision_t MassmoreSHT4x::getPrecision() const { return _precision; }

const char *MassmoreSHT4x::precisionToString(massmore_sht4x_precision_t precision) {
  switch (precision) {
  case MASSMORE_SHT4X_PRECISION_LOW:
    return "LOW";
  case MASSMORE_SHT4X_PRECISION_MEDIUM:
    return "MEDIUM";
  case MASSMORE_SHT4X_PRECISION_HIGH:
    return "HIGH";
  default:
    return "UNKNOWN";
  }
}

void MassmoreSHT4x::setTemperatureOffset(float offsetCelsius) {
  _temperatureOffset = offsetCelsius;
}

float MassmoreSHT4x::getTemperatureOffset() const { return _temperatureOffset; }

void MassmoreSHT4x::setHumidityOffset(float offsetPercent) {
  _humidityOffset = offsetPercent;
}

float MassmoreSHT4x::getHumidityOffset() const { return _humidityOffset; }

void MassmoreSHT4x::setHumidityClipping(bool enabled) { _humidityClipping = enabled; }

bool MassmoreSHT4x::getHumidityClipping() const { return _humidityClipping; }

void MassmoreSHT4x::setVariant(massmore_sht4x_variant_t variant) { _variant = variant; }

massmore_sht4x_variant_t MassmoreSHT4x::getVariant() const { return _variant; }

const char *MassmoreSHT4x::getVariantName() const {
  switch (_variant) {
  case MASSMORE_SHT4X_VARIANT_SHT40:
    return "SHT40";
  case MASSMORE_SHT4X_VARIANT_SHT41:
    return "SHT41";
  case MASSMORE_SHT4X_VARIANT_SHT45:
    return "SHT45";
  case MASSMORE_SHT4X_VARIANT_AUTO:
  default:
    return "SHT4x";
  }
}

float MassmoreSHT4x::getTemperatureAccuracy() const {
  switch (_variant) {
  case MASSMORE_SHT4X_VARIANT_SHT45:
    return 0.1f;
  case MASSMORE_SHT4X_VARIANT_SHT40:
  case MASSMORE_SHT4X_VARIANT_SHT41:
  case MASSMORE_SHT4X_VARIANT_AUTO:
  default:
    return 0.2f;
  }
}

float MassmoreSHT4x::getHumidityAccuracy() const {
  switch (_variant) {
  case MASSMORE_SHT4X_VARIANT_SHT45:
    return 1.0f;
  case MASSMORE_SHT4X_VARIANT_SHT40:
  case MASSMORE_SHT4X_VARIANT_SHT41:
  case MASSMORE_SHT4X_VARIANT_AUTO:
  default:
    return 1.8f;
  }
}

void MassmoreSHT4x::setPackage(massmore_sht4x_package_t package) { _package = package; }

massmore_sht4x_package_t MassmoreSHT4x::getPackage() const { return _package; }

const char *MassmoreSHT4x::getPackageName() const {
  switch (_package) {
  case MASSMORE_SHT4X_PACKAGE_B:
    return "B (ช่องเปิด)";
  case MASSMORE_SHT4X_PACKAGE_F:
    return "F (กันฝุ่น)";
  case MASSMORE_SHT4X_PACKAGE_UNKNOWN:
  default:
    return "ไม่ระบุ";
  }
}

/* ========================================================================= */
/* ชั้นสื่อสารกับบัส I2C                                                      */
/* ========================================================================= */

bool MassmoreSHT4x::sendCommand(uint8_t command) {
  if (_wire == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  _wire->beginTransmission(_address);
  _wire->write(command);
  if (_wire->endTransmission() != 0) {
    return setError(MASSMORE_SHT4X_ERR_I2C_WRITE);
  }
  return true;
}

bool MassmoreSHT4x::readBytes(uint8_t *buffer, uint8_t length) {
  if (_wire == nullptr || buffer == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  if (length == 0 || (length % 3) != 0) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }

  /*
   * SHT4x ไม่รองรับ clock stretching ถ้ายังวัดไม่เสร็จชิปจะ NACK
   * ที่ header ทำให้ requestFrom() คืนจำนวนไบต์ไม่ครบ
   * กรณีนี้ไม่ใช่ความเสียหาย แต่แปลว่า "ยังไม่พร้อม"
   */
  uint8_t got = _wire->requestFrom(_address, length);
  if (got != length) {
    return setError(MASSMORE_SHT4X_ERR_I2C_READ);
  }

  uint32_t deadline = millis() + _timeoutMs;
  for (uint8_t i = 0; i < length; i++) {
    while (_wire->available() == 0) {
      if ((int32_t)(millis() - deadline) >= 0) {
        return setError(MASSMORE_SHT4X_ERR_TIMEOUT);
      }
    }
    buffer[i] = (uint8_t)_wire->read();
  }

  /* ทุก 2 ไบต์ข้อมูลจะมี CRC ต่อท้าย 1 ไบต์เสมอ */
  for (uint8_t i = 0; i + 2 < length; i += 3) {
    if (crc8(&buffer[i], 2) != buffer[i + 2]) {
      return setError(MASSMORE_SHT4X_ERR_CRC);
    }
  }
  return true;
}

bool MassmoreSHT4x::commandAndRead(uint8_t command, uint16_t waitMs, uint16_t *first,
                                   uint16_t *second) {
  if (first == nullptr || second == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  if (!sendCommand(command)) {
    return false;
  }
  delay(waitMs);

  uint8_t buffer[MASSMORE_SHT4X_MEAS_FRAME_LEN];
  if (!readBytes(buffer, MASSMORE_SHT4X_MEAS_FRAME_LEN)) {
    return false;
  }
  *first = (uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]);
  *second = (uint16_t)(((uint16_t)buffer[3] << 8) | buffer[4]);
  return true;
}

bool MassmoreSHT4x::readFrame(uint16_t *rawT, uint16_t *rawRH) {
  uint8_t buffer[MASSMORE_SHT4X_MEAS_FRAME_LEN];
  if (!readBytes(buffer, MASSMORE_SHT4X_MEAS_FRAME_LEN)) {
    return false;
  }
  *rawT = (uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]);
  *rawRH = (uint16_t)(((uint16_t)buffer[3] << 8) | buffer[4]);
  return true;
}

/* ========================================================================= */
/* CRC-8                                                                     */
/* ========================================================================= */

uint8_t MassmoreSHT4x::crc8(const uint8_t *data, uint8_t length) {
  uint8_t crc = MASSMORE_SHT4X_CRC8_INIT;
  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (uint8_t)((crc << 1) ^ MASSMORE_SHT4X_CRC8_POLYNOMIAL);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return (uint8_t)(crc ^ MASSMORE_SHT4X_CRC8_FINAL_XOR);
}

/* ========================================================================= */
/* การแปลงค่า                                                                */
/* ========================================================================= */

float MassmoreSHT4x::rawToCelsius(uint16_t raw) {
  return MASSMORE_SHT4X_T_C_OFFSET +
         MASSMORE_SHT4X_T_C_SPAN * ((float)raw / MASSMORE_SHT4X_RAW_FULL_SCALE);
}

float MassmoreSHT4x::rawToFahrenheit(uint16_t raw) {
  return MASSMORE_SHT4X_T_F_OFFSET +
         MASSMORE_SHT4X_T_F_SPAN * ((float)raw / MASSMORE_SHT4X_RAW_FULL_SCALE);
}

float MassmoreSHT4x::rawToHumidity(uint16_t raw) {
  return MASSMORE_SHT4X_RH_OFFSET +
         MASSMORE_SHT4X_RH_SPAN * ((float)raw / MASSMORE_SHT4X_RAW_FULL_SCALE);
}

uint16_t MassmoreSHT4x::celsiusToRaw(float celsius) {
  float ratio = (celsius - MASSMORE_SHT4X_T_C_OFFSET) / MASSMORE_SHT4X_T_C_SPAN;
  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }
  return (uint16_t)(ratio * MASSMORE_SHT4X_RAW_FULL_SCALE + 0.5f);
}

uint16_t MassmoreSHT4x::humidityToRaw(float humidity) {
  float ratio = (humidity - MASSMORE_SHT4X_RH_OFFSET) / MASSMORE_SHT4X_RH_SPAN;
  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }
  return (uint16_t)(ratio * MASSMORE_SHT4X_RAW_FULL_SCALE + 0.5f);
}

uint8_t MassmoreSHT4x::precisionCommand(massmore_sht4x_precision_t precision) {
  switch (precision) {
  case MASSMORE_SHT4X_PRECISION_LOW:
    return MASSMORE_SHT4X_CMD_MEAS_LOW;
  case MASSMORE_SHT4X_PRECISION_MEDIUM:
    return MASSMORE_SHT4X_CMD_MEAS_MED;
  case MASSMORE_SHT4X_PRECISION_HIGH:
  default:
    return MASSMORE_SHT4X_CMD_MEAS_HIGH;
  }
}

uint16_t MassmoreSHT4x::precisionDurationMs(massmore_sht4x_precision_t precision) {
  switch (precision) {
  case MASSMORE_SHT4X_PRECISION_LOW:
    return MASSMORE_SHT4X_MEAS_DURATION_LOW_MS;
  case MASSMORE_SHT4X_PRECISION_MEDIUM:
    return MASSMORE_SHT4X_MEAS_DURATION_MED_MS;
  case MASSMORE_SHT4X_PRECISION_HIGH:
  default:
    return MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS;
  }
}

uint8_t MassmoreSHT4x::heaterCommand(massmore_sht4x_heater_t mode) {
  switch (mode) {
  case MASSMORE_SHT4X_HEATER_200MW_1S:
    return MASSMORE_SHT4X_CMD_HEATER_200MW_1S;
  case MASSMORE_SHT4X_HEATER_200MW_0S1:
    return MASSMORE_SHT4X_CMD_HEATER_200MW_0S1;
  case MASSMORE_SHT4X_HEATER_110MW_1S:
    return MASSMORE_SHT4X_CMD_HEATER_110MW_1S;
  case MASSMORE_SHT4X_HEATER_110MW_0S1:
    return MASSMORE_SHT4X_CMD_HEATER_110MW_0S1;
  case MASSMORE_SHT4X_HEATER_20MW_1S:
    return MASSMORE_SHT4X_CMD_HEATER_20MW_1S;
  case MASSMORE_SHT4X_HEATER_20MW_0S1:
  default:
    return MASSMORE_SHT4X_CMD_HEATER_20MW_0S1;
  }
}

uint16_t MassmoreSHT4x::heaterPowerMilliwatt(massmore_sht4x_heater_t mode) {
  switch (mode) {
  case MASSMORE_SHT4X_HEATER_200MW_1S:
  case MASSMORE_SHT4X_HEATER_200MW_0S1:
    return 200;
  case MASSMORE_SHT4X_HEATER_110MW_1S:
  case MASSMORE_SHT4X_HEATER_110MW_0S1:
    return 110;
  case MASSMORE_SHT4X_HEATER_20MW_1S:
  case MASSMORE_SHT4X_HEATER_20MW_0S1:
  default:
    return 20;
  }
}

uint16_t MassmoreSHT4x::heaterDurationMs(massmore_sht4x_heater_t mode) {
  switch (mode) {
  case MASSMORE_SHT4X_HEATER_200MW_1S:
  case MASSMORE_SHT4X_HEATER_110MW_1S:
  case MASSMORE_SHT4X_HEATER_20MW_1S:
    return MASSMORE_SHT4X_HEATER_LONG_MS;
  case MASSMORE_SHT4X_HEATER_200MW_0S1:
  case MASSMORE_SHT4X_HEATER_110MW_0S1:
  case MASSMORE_SHT4X_HEATER_20MW_0S1:
  default:
    return MASSMORE_SHT4X_HEATER_SHORT_MS;
  }
}

const char *MassmoreSHT4x::heaterToString(massmore_sht4x_heater_t mode) {
  switch (mode) {
  case MASSMORE_SHT4X_HEATER_200MW_1S:
    return "200mW/1s";
  case MASSMORE_SHT4X_HEATER_200MW_0S1:
    return "200mW/0.1s";
  case MASSMORE_SHT4X_HEATER_110MW_1S:
    return "110mW/1s";
  case MASSMORE_SHT4X_HEATER_110MW_0S1:
    return "110mW/0.1s";
  case MASSMORE_SHT4X_HEATER_20MW_1S:
    return "20mW/1s";
  case MASSMORE_SHT4X_HEATER_20MW_0S1:
    return "20mW/0.1s";
  default:
    return "ไม่ทราบ";
  }
}

/* ========================================================================= */
/* การเก็บผลวัด                                                               */
/* ========================================================================= */

void MassmoreSHT4x::storeReading(uint16_t rawT, uint16_t rawRH, bool heated) {
  float t = rawToCelsius(rawT) + _temperatureOffset;
  float h = rawToHumidity(rawRH) + _humidityOffset;

  if (_humidityClipping) {
    if (h < MASSMORE_SHT4X_RH_MIN_PERCENT) {
      h = MASSMORE_SHT4X_RH_MIN_PERCENT;
    }
    if (h > MASSMORE_SHT4X_RH_MAX_PERCENT) {
      h = MASSMORE_SHT4X_RH_MAX_PERCENT;
    }
  }

  _reading.temperature = t;
  _reading.humidity = h;
  _reading.rawTemperature = rawT;
  _reading.rawHumidity = rawRH;
  _reading.timestampMs = millis();
  _reading.heated = heated;
}

/* ========================================================================= */
/* การวัดแบบบล็อก                                                             */
/* ========================================================================= */

bool MassmoreSHT4x::measureWith(massmore_sht4x_precision_t precision,
                                float *temperature, float *humidity) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  if (_mode == MASSMORE_SHT4X_MODE_HEATING && !isHeaterDone()) {
    return setError(MASSMORE_SHT4X_ERR_WRONG_MODE);
  }

  uint16_t rawT = 0;
  uint16_t rawRH = 0;
  if (!commandAndRead(precisionCommand(precision), precisionDurationMs(precision),
                      &rawT, &rawRH)) {
    return false;
  }

  storeReading(rawT, rawRH, false);
  _mode = MASSMORE_SHT4X_MODE_READY;

  if (temperature != nullptr) {
    *temperature = _reading.temperature;
  }
  if (humidity != nullptr) {
    *humidity = _reading.humidity;
  }
  clearError();
  return true;
}

bool MassmoreSHT4x::measure(float *temperature, float *humidity) {
  return measureWith(_precision, temperature, humidity);
}

bool MassmoreSHT4x::measure(massmore_sht4x_reading_t &reading) {
  if (!measureWith(_precision, nullptr, nullptr)) {
    return false;
  }
  reading = _reading;
  return true;
}

float MassmoreSHT4x::readTemperature() {
  float t = NAN;
  if (!measure(&t, nullptr)) {
    return NAN;
  }
  return t;
}

float MassmoreSHT4x::readTemperatureF() {
  float t = readTemperature();
  if (isnan(t)) {
    return NAN;
  }
  return celsiusToFahrenheit(t);
}

float MassmoreSHT4x::readHumidity() {
  float h = NAN;
  if (!measure(nullptr, &h)) {
    return NAN;
  }
  return h;
}

/* ========================================================================= */
/* การวัดแบบไม่บล็อก                                                          */
/* ========================================================================= */

bool MassmoreSHT4x::startMeasurement() {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  if (_mode == MASSMORE_SHT4X_MODE_HEATING && !isHeaterDone()) {
    return setError(MASSMORE_SHT4X_ERR_WRONG_MODE);
  }
  if (!sendCommand(precisionCommand(_precision))) {
    return false;
  }
  _operationStartMs = millis();
  _operationDurationMs = precisionDurationMs(_precision);
  _pendingHeated = false;
  _mode = MASSMORE_SHT4X_MODE_MEASURING;
  clearError();
  return true;
}

bool MassmoreSHT4x::isMeasurementReady() const {
  if (_mode != MASSMORE_SHT4X_MODE_MEASURING && _mode != MASSMORE_SHT4X_MODE_HEATING) {
    return false;
  }
  return (uint32_t)(millis() - _operationStartMs) >= _operationDurationMs;
}

bool MassmoreSHT4x::isHeaterDone() const {
  if (_mode != MASSMORE_SHT4X_MODE_HEATING) {
    return true;
  }
  return (uint32_t)(millis() - _operationStartMs) >= _operationDurationMs;
}

bool MassmoreSHT4x::readMeasurement(float *temperature, float *humidity) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  if (_mode != MASSMORE_SHT4X_MODE_MEASURING && _mode != MASSMORE_SHT4X_MODE_HEATING) {
    return setError(MASSMORE_SHT4X_ERR_WRONG_MODE);
  }
  if (!isMeasurementReady()) {
    return setError(MASSMORE_SHT4X_ERR_NOT_READY);
  }

  uint16_t rawT = 0;
  uint16_t rawRH = 0;
  if (!readFrame(&rawT, &rawRH)) {
    /* ชิป NACK = ยังวัดไม่เสร็จจริง ๆ ให้ผู้ใช้ลองใหม่ ไม่ใช่ความเสียหาย */
    if (_error == MASSMORE_SHT4X_ERR_I2C_READ) {
      return setError(MASSMORE_SHT4X_ERR_NOT_READY);
    }
    return false;
  }

  storeReading(rawT, rawRH, _pendingHeated);
  _mode = MASSMORE_SHT4X_MODE_READY;

  if (temperature != nullptr) {
    *temperature = _reading.temperature;
  }
  if (humidity != nullptr) {
    *humidity = _reading.humidity;
  }
  clearError();
  return true;
}

bool MassmoreSHT4x::readMeasurement(massmore_sht4x_reading_t &reading) {
  if (!readMeasurement(nullptr, nullptr)) {
    return false;
  }
  reading = _reading;
  return true;
}

void MassmoreSHT4x::setUpdateInterval(uint32_t intervalMs) {
  _updateIntervalMs = intervalMs;
}

uint32_t MassmoreSHT4x::getUpdateInterval() const { return _updateIntervalMs; }

bool MassmoreSHT4x::update() {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }

  /* กำลังรอผลอยู่ ถ้าครบเวลาแล้วก็เก็บผล */
  if (_mode == MASSMORE_SHT4X_MODE_MEASURING || _mode == MASSMORE_SHT4X_MODE_HEATING) {
    if (!isMeasurementReady()) {
      return false;
    }
    if (!readMeasurement(nullptr, nullptr)) {
      return false;
    }
    if (_callback != nullptr) {
      _callback(_reading);
    }
    return true;
  }

  /* ว่างอยู่ ถ้าถึงรอบถัดไปแล้วก็สั่งวัดใหม่ */
  uint32_t now = millis();
  if (_reading.timestampMs != 0 &&
      (uint32_t)(now - _lastUpdateStartMs) < _updateIntervalMs) {
    return false;
  }
  _lastUpdateStartMs = now;
  startMeasurement();
  return false;
}

massmore_sht4x_mode_t MassmoreSHT4x::getMode() const { return _mode; }

void MassmoreSHT4x::setCallback(massmore_sht4x_callback_t callback) {
  _callback = callback;
}

/* ========================================================================= */
/* ค่าล่าสุด                                                                  */
/* ========================================================================= */

float MassmoreSHT4x::getTemperature() const { return _reading.temperature; }

float MassmoreSHT4x::getTemperatureF() const {
  if (isnan(_reading.temperature)) {
    return NAN;
  }
  return celsiusToFahrenheit(_reading.temperature);
}

float MassmoreSHT4x::getHumidity() const { return _reading.humidity; }

uint16_t MassmoreSHT4x::getRawTemperature() const { return _reading.rawTemperature; }

uint16_t MassmoreSHT4x::getRawHumidity() const { return _reading.rawHumidity; }

uint32_t MassmoreSHT4x::getLastUpdateMs() const { return _reading.timestampMs; }

const massmore_sht4x_reading_t &MassmoreSHT4x::getLastReading() const {
  return _reading;
}

/* ========================================================================= */
/* ฮีตเตอร์                                                                   */
/* ========================================================================= */

bool MassmoreSHT4x::heaterDutyAllows(uint16_t durationMs) const {
  if (!_heaterDutyGuard) {
    return true;
  }
  uint32_t elapsed = (uint32_t)(millis() - _heaterEpochMs);
  uint32_t wouldBeOn = _heaterOnTimeMs + durationMs;

  /*
   * ช่วงแรกหลัง begin() เวลายังผ่านไปน้อย ยอมให้ยิงพัลส์แรกได้เสมอ
   * มิฉะนั้นการเรียก runHeater() ทันทีหลัง begin() จะถูกปฏิเสธเสมอ
   */
  if (elapsed < 2000UL) {
    return _heaterOnTimeMs == 0;
  }
  /* on / elapsed <= 10%  เขียนแบบคูณไขว้เพื่อเลี่ยงเลขทศนิยม */
  return (wouldBeOn * 100UL) <= (elapsed * MASSMORE_SHT4X_HEATER_MAX_DUTY_PERCENT);
}

void MassmoreSHT4x::accountHeater(uint16_t durationMs) {
  _heaterOnTimeMs += durationMs;
}

bool MassmoreSHT4x::runHeater(massmore_sht4x_heater_t mode,
                              massmore_sht4x_reading_t *reading) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  uint16_t duration = heaterDurationMs(mode);
  if (!heaterDutyAllows(duration)) {
    return setError(MASSMORE_SHT4X_ERR_HEATER_DUTY);
  }

  uint16_t rawT = 0;
  uint16_t rawRH = 0;
  if (!commandAndRead(heaterCommand(mode), duration, &rawT, &rawRH)) {
    return false;
  }
  accountHeater(duration);

  storeReading(rawT, rawRH, true);
  _mode = MASSMORE_SHT4X_MODE_READY;
  if (reading != nullptr) {
    *reading = _reading;
  }
  clearError();
  return true;
}

bool MassmoreSHT4x::startHeater(massmore_sht4x_heater_t mode) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  uint16_t duration = heaterDurationMs(mode);
  if (!heaterDutyAllows(duration)) {
    return setError(MASSMORE_SHT4X_ERR_HEATER_DUTY);
  }
  if (!sendCommand(heaterCommand(mode))) {
    return false;
  }
  accountHeater(duration);
  _operationStartMs = millis();
  _operationDurationMs = duration;
  _pendingHeated = true;
  _mode = MASSMORE_SHT4X_MODE_HEATING;
  clearError();
  return true;
}

bool MassmoreSHT4x::removeCondensation(uint8_t pulses, uint32_t restMs) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  if (pulses == 0) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }

  bool guardWas = _heaterDutyGuard;
  /*
   * ระหว่างกู้สถานการณ์เราเว้นจังหวะพักเองด้วย restMs อยู่แล้ว
   * จึงปิดตัวกันเผลอชั่วคราวแล้วเปิดคืนตอนจบ
   */
  _heaterDutyGuard = false;
  bool ok = true;
  for (uint8_t i = 0; i < pulses; i++) {
    if (!runHeater(MASSMORE_SHT4X_HEATER_200MW_1S, nullptr)) {
      ok = false;
      break;
    }
    if (i + 1 < pulses) {
      delay(restMs);
    }
  }
  _heaterDutyGuard = guardWas;
  return ok;
}

bool MassmoreSHT4x::measureAfterHeating(uint32_t coolDownMs,
                                        massmore_sht4x_reading_t &reading) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }
  delay(coolDownMs);
  return measure(reading);
}

void MassmoreSHT4x::setHeaterDutyGuard(bool enabled) { _heaterDutyGuard = enabled; }

bool MassmoreSHT4x::getHeaterDutyGuard() const { return _heaterDutyGuard; }

float MassmoreSHT4x::getHeaterDutyPercent() const {
  uint32_t elapsed = (uint32_t)(millis() - _heaterEpochMs);
  if (elapsed == 0) {
    return 0.0f;
  }
  return 100.0f * ((float)_heaterOnTimeMs / (float)elapsed);
}

uint32_t MassmoreSHT4x::getHeaterOnTimeMs() const { return _heaterOnTimeMs; }

void MassmoreSHT4x::resetHeaterStats() {
  _heaterOnTimeMs = 0;
  _heaterEpochMs = millis();
}

/* ========================================================================= */
/* รีเซ็ต                                                                     */
/* ========================================================================= */

bool MassmoreSHT4x::softReset() {
  if (!sendCommand(MASSMORE_SHT4X_CMD_SOFT_RESET)) {
    return false;
  }
  delay(MASSMORE_SHT4X_SOFT_RESET_MS);
  _mode = _begun ? MASSMORE_SHT4X_MODE_READY : _mode;
  clearError();
  return true;
}

bool MassmoreSHT4x::generalCallReset() {
  if (_wire == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  _wire->beginTransmission(MASSMORE_SHT4X_GENERAL_CALL_ADDR);
  _wire->write((uint8_t)MASSMORE_SHT4X_GENERAL_CALL_RESET_BYTE);
  if (_wire->endTransmission() != 0) {
    return setError(MASSMORE_SHT4X_ERR_I2C_WRITE);
  }
  delay(MASSMORE_SHT4X_SOFT_RESET_MS);
  _mode = _begun ? MASSMORE_SHT4X_MODE_READY : _mode;
  clearError();
  return true;
}

/* ========================================================================= */
/* ตัวตนของชิป                                                                */
/* ========================================================================= */

bool MassmoreSHT4x::readSerialNumber(uint32_t *serial) {
  if (serial == nullptr) {
    return setError(MASSMORE_SHT4X_ERR_BAD_ARG);
  }
  uint16_t high = 0;
  uint16_t low = 0;
  if (!commandAndRead(MASSMORE_SHT4X_CMD_READ_SERIAL, MASSMORE_SHT4X_SERIAL_READ_MS,
                      &high, &low)) {
    return false;
  }
  _serialNumber = ((uint32_t)high << 16) | (uint32_t)low;
  *serial = _serialNumber;
  clearError();
  return true;
}

uint32_t MassmoreSHT4x::getSerialNumber() const { return _serialNumber; }

massmore_sht4x_genuine_t MassmoreSHT4x::verifyChip() {
  _verifyMask = 0;
  _genuine = MASSMORE_SHT4X_GENUINE_UNKNOWN;

  /* --- ข้อ 1: มีอุปกรณ์ตอบที่ address นี้ไหม --- */
  if (!isConnected()) {
    _genuine = MASSMORE_SHT4X_GENUINE_NOT_SHT4X;
    return _genuine;
  }
  _verifyMask |= MASSMORE_SHT4X_CHK_ACK;

  bool wasBegun = _begun;
  _begun = true;
  _mode = MASSMORE_SHT4X_MODE_READY;

  /* --- ข้อ 2-3: อ่านซีเรียลจากโรงงาน --- */
  uint32_t serial1 = 0;
  bool serialOk = readSerialNumber(&serial1);
  if (serialOk) {
    _verifyMask |= MASSMORE_SHT4X_CHK_SERIAL_CRC;
    if (serial1 != 0x00000000UL && serial1 != 0xFFFFFFFFUL) {
      _verifyMask |= MASSMORE_SHT4X_CHK_SERIAL_SANE;
    }
  }

  /* --- ข้อ 4: อ่านซ้ำต้องได้ค่าเดิม --- */
  delay(MASSMORE_SHT4X_CMD_GAP_MS);
  uint32_t serial2 = 0;
  if (serialOk && readSerialNumber(&serial2) && serial2 == serial1) {
    _verifyMask |= MASSMORE_SHT4X_CHK_SERIAL_STABLE;
  }
  _serialNumber = serial1;

  /* --- ข้อ 5: soft reset แล้วยังวัดได้ --- */
  bool resetOk = softReset();
  delay(MASSMORE_SHT4X_CMD_GAP_MS);
  float t = NAN;
  float h = NAN;
  bool measOk = measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &t, &h);
  if (resetOk && measOk) {
    _verifyMask |= MASSMORE_SHT4X_CHK_SOFT_RESET;
  }

  /* --- ข้อ 6-7: CRC ของผลวัด และความสมเหตุสมผลของค่า --- */
  if (measOk) {
    _verifyMask |= MASSMORE_SHT4X_CHK_MEAS_CRC;
    bool rawSane = (_reading.rawTemperature != 0x0000) &&
                   (_reading.rawTemperature != 0xFFFF) &&
                   (_reading.rawHumidity != 0xFFFF);
    if (rawSane && t > MASSMORE_SHT4X_T_MIN_C && t < MASSMORE_SHT4X_T_MAX_C &&
        h >= MASSMORE_SHT4X_RH_MIN_PERCENT && h <= MASSMORE_SHT4X_RH_MAX_PERCENT) {
      _verifyMask |= MASSMORE_SHT4X_CHK_MEAS_RANGE;
    }
  }

  /*
   * --- ข้อ 8: อ่านผลก่อนวัดเสร็จ ชิปแท้ต้อง NACK ---
   * datasheet: "The sensor does not support clock-stretching ... it will return
   * a NACK" ของเลียนแบบที่ทำเป็นตารางค่าคงที่มักตอบทันที จึงตกข้อนี้
   */
  if (sendCommand(MASSMORE_SHT4X_CMD_MEAS_HIGH)) {
    delay(MASSMORE_SHT4X_PROBE_EARLY_MS);
    uint8_t probe[MASSMORE_SHT4X_MEAS_FRAME_LEN];
    bool early = readBytes(probe, MASSMORE_SHT4X_MEAS_FRAME_LEN);
    if (!early) {
      _verifyMask |= MASSMORE_SHT4X_CHK_NACK_EARLY;
    }
    /* รอให้ครบเวลาแล้วเก็บผลทิ้ง เพื่อไม่ให้ค้างคาบัส */
    delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
    uint16_t dummyT = 0;
    uint16_t dummyRH = 0;
    readFrame(&dummyT, &dummyRH);
  }

  /* --- ข้อ 9: ความละเอียดต่ำต้องเสร็จเร็วกว่าความละเอียดสูงจริง --- */
  delay(MASSMORE_SHT4X_CMD_GAP_MS);
  if (sendCommand(MASSMORE_SHT4X_CMD_MEAS_LOW)) {
    delay(MASSMORE_SHT4X_MEAS_DURATION_LOW_MS);
    uint16_t lowT = 0;
    uint16_t lowRH = 0;
    if (readFrame(&lowT, &lowRH)) {
      /* ต้องอ่านได้ที่ 3 ms ซึ่งเป็นเวลาที่คำสั่งความละเอียดสูงยังไม่เสร็จ */
      _verifyMask |= MASSMORE_SHT4X_CHK_LOW_FASTER;
    }
  }

  /* --- ข้อ 10: คำสั่งนอกตารางต้องไม่ถูกตอบด้วยข้อมูล --- */
  delay(MASSMORE_SHT4X_CMD_GAP_MS);
  bool bogusAnswered = false;
  if (sendCommand(MASSMORE_SHT4X_CMD_BOGUS)) {
    delay(MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS);
    uint8_t probe[MASSMORE_SHT4X_MEAS_FRAME_LEN];
    bogusAnswered = readBytes(probe, MASSMORE_SHT4X_MEAS_FRAME_LEN);
  }
  if (!bogusAnswered) {
    _verifyMask |= MASSMORE_SHT4X_CHK_BOGUS_CMD;
  }

  /* คืนสภาพเดิม */
  softReset();
  _begun = wasBegun;
  _mode = wasBegun ? MASSMORE_SHT4X_MODE_READY : MASSMORE_SHT4X_MODE_IDLE;
  clearError();

  /* --- สรุปผล --- */
  uint8_t passed = getVerifyPassCount();
  /*
   * แกนกลางที่ตัดสินว่า "เป็น SHT4x หรือไม่ใช่เลย" คือซีเรียลกับ CRC ของผลวัด
   * ชิปตระกูลอื่นที่ใช้ address 0x44 เหมือนกัน (เช่น SHT3x หรือ AHT2x)
   * จะตอบคำสั่ง 1 ไบต์แบบนี้ไม่ได้ จึงตกสองข้อนี้
   */
  bool coreOk = (_verifyMask & MASSMORE_SHT4X_CHK_SERIAL_CRC) &&
                (_verifyMask & MASSMORE_SHT4X_CHK_MEAS_CRC);
  if (!coreOk) {
    _genuine = MASSMORE_SHT4X_GENUINE_NOT_SHT4X;
  } else if (passed == MASSMORE_SHT4X_CHK_COUNT) {
    _genuine = MASSMORE_SHT4X_GENUINE_PASS;
  } else if (passed >= 8) {
    _genuine = MASSMORE_SHT4X_GENUINE_PARTIAL;
  } else {
    _genuine = MASSMORE_SHT4X_GENUINE_SUSPECT;
  }
  return _genuine;
}

uint16_t MassmoreSHT4x::getVerifyMask() const { return _verifyMask; }

uint8_t MassmoreSHT4x::getVerifyPassCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MASSMORE_SHT4X_CHK_COUNT; i++) {
    if (_verifyMask & (1u << i)) {
      count++;
    }
  }
  return count;
}

const char *MassmoreSHT4x::getVerifyCheckName(uint8_t index) {
  switch (index) {
  case 0:
    return "ตอบ ACK ที่ address";
  case 1:
    return "CRC ของซีเรียล";
  case 2:
    return "ซีเรียลไม่ใช่ค่าว่าง";
  case 3:
    return "อ่านซีเรียลซ้ำได้ค่าเดิม";
  case 4:
    return "soft reset แล้ววัดต่อได้";
  case 5:
    return "CRC ของผลวัด";
  case 6:
    return "ค่าที่วัดอยู่ในช่วง";
  case 7:
    return "NACK ตอนยังวัดไม่เสร็จ";
  case 8:
    return "ความละเอียดต่ำเสร็จเร็วกว่า";
  case 9:
    return "ปฏิเสธคำสั่งนอกตาราง";
  default:
    return "ไม่ทราบ";
  }
}

const char *MassmoreSHT4x::genuineToString(massmore_sht4x_genuine_t result) {
  switch (result) {
  case MASSMORE_SHT4X_GENUINE_PASS:
    return "ของแท้ Sensirion SHT4x";
  case MASSMORE_SHT4X_GENUINE_PARTIAL:
    return "น่าจะแท้ แต่มีบางข้อไม่ผ่าน";
  case MASSMORE_SHT4X_GENUINE_SUSPECT:
    return "น่าสงสัย ตอบผิดหลายข้อ";
  case MASSMORE_SHT4X_GENUINE_NOT_SHT4X:
    return "ไม่ใช่ SHT4x";
  case MASSMORE_SHT4X_GENUINE_UNKNOWN:
  default:
    return "ยังไม่ได้ตรวจ";
  }
}

/* ========================================================================= */
/* self test                                                                 */
/* ========================================================================= */

bool MassmoreSHT4x::runHeaterSelfTest(massmore_sht4x_heater_t mode, float minRiseC,
                                      float *riseOut) {
  if (!_begun) {
    return setError(MASSMORE_SHT4X_ERR_NOT_BEGUN);
  }

  float before = NAN;
  if (!measureWith(MASSMORE_SHT4X_PRECISION_HIGH, &before, nullptr)) {
    return false;
  }

  massmore_sht4x_reading_t hot;
  if (!runHeater(mode, &hot)) {
    return false;
  }

  float rise = hot.temperature - before;
  if (riseOut != nullptr) {
    *riseOut = rise;
  }
  if (rise < minRiseC) {
    return setError(MASSMORE_SHT4X_ERR_OUT_OF_RANGE);
  }
  clearError();
  return true;
}

/* ========================================================================= */
/* ค่าที่คำนวณต่อ                                                             */
/* ========================================================================= */

float MassmoreSHT4x::celsiusToFahrenheit(float celsius) {
  return celsius * 9.0f / 5.0f + 32.0f;
}

float MassmoreSHT4x::fahrenheitToCelsius(float fahrenheit) {
  return (fahrenheit - 32.0f) * 5.0f / 9.0f;
}

float MassmoreSHT4x::saturationVaporPressure(float temperature) {
  /* สูตร Magnus: 6.112 * exp(a*T/(b+T)) หน่วย hPa */
  return 6.112f * expf((MASSMORE_SHT4X_MAGNUS_A * temperature) /
                       (MASSMORE_SHT4X_MAGNUS_B + temperature));
}

float MassmoreSHT4x::dewPoint(float temperature, float humidity) {
  if (isnan(temperature) || isnan(humidity) || humidity <= 0.0f) {
    return NAN;
  }
  float gamma = (MASSMORE_SHT4X_MAGNUS_A * temperature) /
                    (MASSMORE_SHT4X_MAGNUS_B + temperature) +
                logf(humidity / 100.0f);
  return (MASSMORE_SHT4X_MAGNUS_B * gamma) / (MASSMORE_SHT4X_MAGNUS_A - gamma);
}

float MassmoreSHT4x::absoluteHumidity(float temperature, float humidity) {
  if (isnan(temperature) || isnan(humidity)) {
    return NAN;
  }
  /* g/m^3 = 216.7 * (RH/100 * es(T)) / (273.15 + T) */
  float es = saturationVaporPressure(temperature);
  return 216.7f * ((humidity / 100.0f) * es) / (273.15f + temperature);
}

float MassmoreSHT4x::heatIndex(float temperature, float humidity) {
  if (isnan(temperature) || isnan(humidity)) {
    return NAN;
  }
  /* สูตร Rothfusz ของ NOAA ทำงานในหน่วยฟาเรนไฮต์ */
  float tF = celsiusToFahrenheit(temperature);
  float rh = humidity;

  float hi = 0.5f * (tF + 61.0f + ((tF - 68.0f) * 1.2f) + (rh * 0.094f));
  if (((hi + tF) / 2.0f) >= 80.0f) {
    hi = -42.379f + 2.04901523f * tF + 10.14333127f * rh -
         0.22475541f * tF * rh - 0.00683783f * tF * tF -
         0.05481717f * rh * rh + 0.00122874f * tF * tF * rh +
         0.00085282f * tF * rh * rh - 0.00000199f * tF * tF * rh * rh;
    if (rh < 13.0f && tF >= 80.0f && tF <= 112.0f) {
      hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - fabsf(tF - 95.0f)) / 17.0f);
    } else if (rh > 85.0f && tF >= 80.0f && tF <= 87.0f) {
      hi += ((rh - 85.0f) / 10.0f) * ((87.0f - tF) / 5.0f);
    }
  }
  return fahrenheitToCelsius(hi);
}

/* ========================================================================= */
/* ข้อผิดพลาดและข้อมูลทั่วไป                                                   */
/* ========================================================================= */

bool MassmoreSHT4x::setError(massmore_sht4x_error_t error) {
  _error = error;
  return false;
}

void MassmoreSHT4x::clearError() { _error = MASSMORE_SHT4X_OK; }

massmore_sht4x_error_t MassmoreSHT4x::lastError() const { return _error; }

const char *MassmoreSHT4x::lastErrorString() const { return errorToString(_error); }

const char *MassmoreSHT4x::errorToString(massmore_sht4x_error_t error) {
  switch (error) {
  case MASSMORE_SHT4X_OK:
    return "ปกติ";
  case MASSMORE_SHT4X_ERR_NOT_BEGUN:
    return "ยังไม่ได้เรียก begin()";
  case MASSMORE_SHT4X_ERR_NO_DEVICE:
    return "ไม่พบอุปกรณ์บนบัส I2C";
  case MASSMORE_SHT4X_ERR_I2C_WRITE:
    return "เขียนลงบัส I2C ไม่สำเร็จ";
  case MASSMORE_SHT4X_ERR_I2C_READ:
    return "อ่านจากบัส I2C ได้ไม่ครบ";
  case MASSMORE_SHT4X_ERR_CRC:
    return "checksum ของข้อมูลไม่ตรง";
  case MASSMORE_SHT4X_ERR_TIMEOUT:
    return "รอข้อมูลเกินเวลาที่กำหนด";
  case MASSMORE_SHT4X_ERR_NOT_READY:
    return "ยังวัดไม่เสร็จ";
  case MASSMORE_SHT4X_ERR_WRONG_MODE:
    return "เรียกใช้ผิดจังหวะ";
  case MASSMORE_SHT4X_ERR_BAD_ARG:
    return "พารามิเตอร์ไม่ถูกต้อง";
  case MASSMORE_SHT4X_ERR_OUT_OF_RANGE:
    return "ค่าที่ได้อยู่นอกช่วงที่คาดไว้";
  case MASSMORE_SHT4X_ERR_HEATER_DUTY:
    return "ใช้ฮีตเตอร์ถี่เกินที่ datasheet อนุญาต";
  default:
    return "ข้อผิดพลาดที่ไม่รู้จัก";
  }
}

uint8_t MassmoreSHT4x::getAddress() const { return _address; }

const char *MassmoreSHT4x::getLibraryVersion() { return MASSMORE_SHT4X_VERSION_STRING; }

uint8_t MassmoreSHT4x::scan(TwoWire *wire, uint8_t *found) {
  if (wire == nullptr || found == nullptr) {
    return 0;
  }
  uint8_t count = 0;
  const uint8_t candidates[3] = {MASSMORE_SHT4X_I2C_ADDR_A, MASSMORE_SHT4X_I2C_ADDR_B,
                                 MASSMORE_SHT4X_I2C_ADDR_C};
  for (uint8_t i = 0; i < 3; i++) {
    wire->beginTransmission(candidates[i]);
    if (wire->endTransmission() == 0) {
      found[count++] = candidates[i];
    }
  }
  return count;
}
