/**
 * @file    Massmore_SHT4x.cpp
 * @brief   การทำงานภายในของไลบรารี Massmore_SHT4x (อ้างอิง Datasheet SHT4x V7.3)
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license   MIT
 */

#include "Massmore_SHT4x.h"

#include <math.h>

/* ค่าคงที่สูตร Magnus (dewPoint / absoluteHumidity) */
#define MASSMORE_SHT4X_MAGNUS_A 17.62f
#define MASSMORE_SHT4X_MAGNUS_B 243.12f

/* เวลา "แหย่" อ่านก่อนวัดเสร็จเพื่อดูว่าชิป NACK จริง (isGenuine ข้อ 8) */
#define MASSMORE_SHT4X_PROBE_EARLY_MS 1

/* ========================================================================= */
/* Construction / begin                                                      */
/* ========================================================================= */

Massmore_SHT4x::Massmore_SHT4x(TwoWire &wirePort)
    : _wire(&wirePort), _address(I2C_ADDR_DEFAULT), _begun(false),
      _timeoutMs(TIMEOUT_DEFAULT_MS), _variant(Variant::UNKNOWN),
      _temperatureOffset(0.0f), _humidityOffset(0.0f), _error(ErrorCode::OK),
      _state(State::IDLE), _opStartMs(0), _opDurationMs(0), _opHeated(false),
      _dutyGuard(true), _heaterEndMs(0), _heaterLastDurationMs(0),
      _heaterUsed(false), _serialNumber(0), _genuineMask(0) {
  _reading.temperature = NAN;
  _reading.humidity = NAN;
  _reading.rawTemperature = 0;
  _reading.rawHumidity = 0;
  _reading.timestampMs = 0;
  _reading.heated = false;
}

bool Massmore_SHT4x::begin(TwoWire &wirePort, uint8_t address, Variant variant) {
  _wire = &wirePort;
  return begin(address, variant);
}

bool Massmore_SHT4x::begin(uint8_t address, Variant variant) {
  if (address != I2C_ADDR_A && address != I2C_ADDR_B && address != I2C_ADDR_C) {
    return setError(ErrorCode::BAD_ARG);
  }
  _address = address;
  _variant = variant;
  _state = State::IDLE;
  _heaterUsed = false;

  /* datasheet: หลัง power-up ต้องรอสูงสุด 1 ms ก่อนส่งคำสั่งแรก */
  delay(1);

  if (!isConnected()) {
    return false; /* NOT_FOUND */
  }
  if (!softReset()) {
    return false;
  }

  /* ยืนยันว่าเป็น SHT4x จริงด้วยการอ่าน Serial Number (ไม่มี CHIP_ID register) */
  _begun = true;
  if (!verifyChipID()) {
    _begun = false;
    return false;
  }
  return true;
}

bool Massmore_SHT4x::isConnected() {
  _wire->beginTransmission(_address);
  if (_wire->endTransmission() != 0) {
    return setError(ErrorCode::NOT_FOUND);
  }
  _error = ErrorCode::OK;
  return true;
}

void Massmore_SHT4x::setTimeout(uint16_t timeoutMs) { _timeoutMs = timeoutMs; }

/* ========================================================================= */
/* Low-level I2C                                                             */
/* ========================================================================= */

bool Massmore_SHT4x::sendCommand(uint8_t command) {
  _wire->beginTransmission(_address);
  _wire->write(command);
  uint8_t rc = _wire->endTransmission();
  if (rc == 2 || rc == 3) {
    /* 2 = NACK on address, 3 = NACK on data */
    return setError(ErrorCode::NOT_FOUND);
  }
  if (rc != 0) {
    return setError(ErrorCode::BUS_ERROR);
  }
  return true;
}

/*
 * อ่าน frame 6 byte: word(2) + CRC(1) + word(2) + CRC(1)
 * SHT4x ไม่รองรับ clock stretching: ถ้าวัดยังไม่เสร็จชิปจะ NACK ที่ address
 * ทำให้ requestFrom() คืน 0 → รายงานเป็น NOT_READY (ไม่ใช่ความเสียหาย)
 */
bool Massmore_SHT4x::readFrame(uint16_t &first, uint16_t &second) {
  uint8_t buf[FRAME_LEN];
  uint8_t got = _wire->requestFrom((uint8_t)_address, (uint8_t)FRAME_LEN);
  if (got != FRAME_LEN) {
    /* ทิ้งข้อมูลค้าง (ถ้ามี) */
    while (_wire->available() > 0) {
      (void)_wire->read();
    }
    return setError(ErrorCode::NOT_READY);
  }
  for (uint8_t i = 0; i < FRAME_LEN; i++) {
    buf[i] = (uint8_t)_wire->read();
  }
  if (crc8(&buf[0], 2) != buf[2] || crc8(&buf[3], 2) != buf[5]) {
    return setError(ErrorCode::CRC_FAIL);
  }
  first = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
  second = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);
  return true;
}

/* ลองอ่านซ้ำทุก 1 ms จนกว่าชิปจะเลิก NACK หรือครบ timeout (rollover-safe) */
bool Massmore_SHT4x::readFrameWithRetry(uint16_t &first, uint16_t &second,
                                        uint16_t timeoutMs) {
  uint32_t t0 = millis();
  for (;;) {
    if (readFrame(first, second)) {
      return true;
    }
    if (_error != ErrorCode::NOT_READY) {
      return false; /* CRC_FAIL หรืออื่น ๆ ไม่ต้องรอต่อ */
    }
    if ((uint32_t)(millis() - t0) >= timeoutMs) {
      return setError(ErrorCode::TIMEOUT);
    }
    delay(1);
  }
}

bool Massmore_SHT4x::measureBlocking(uint8_t command, uint16_t waitMs, bool heated) {
  if (!_begun) {
    return setError(ErrorCode::NOT_BEGUN);
  }
  if (_state == State::MEASURING || _state == State::HEATING) {
    return setError(ErrorCode::BUSY);
  }
  if (!sendCommand(command)) {
    return false;
  }
  delay(waitMs);
  uint16_t rawT = 0;
  uint16_t rawRH = 0;
  if (!readFrameWithRetry(rawT, rawRH, _timeoutMs)) {
    return false;
  }
  storeReading(rawT, rawRH, heated);
  _error = ErrorCode::OK;
  return true;
}

void Massmore_SHT4x::storeReading(uint16_t rawT, uint16_t rawRH, bool heated) {
  float h = rawToHumidity(rawRH) + _humidityOffset;
  /* datasheet: ค่าอาจเลย 0..100 เล็กน้อย ให้ clip ก่อนใช้งาน */
  if (h < HUMI_MIN_PERCENT) {
    h = HUMI_MIN_PERCENT;
  } else if (h > HUMI_MAX_PERCENT) {
    h = HUMI_MAX_PERCENT;
  }
  _reading.temperature = rawToCelsius(rawT) + _temperatureOffset;
  _reading.humidity = h;
  _reading.rawTemperature = rawT;
  _reading.rawHumidity = rawRH;
  _reading.timestampMs = millis();
  _reading.heated = heated;
}

bool Massmore_SHT4x::setError(ErrorCode error) {
  _error = error;
  return false;
}

/* ========================================================================= */
/* Simple Blocking API                                                       */
/* ========================================================================= */

float Massmore_SHT4x::readTemperature(Precision precision) {
  if (!measureBlocking(precisionCommand(precision), precisionDurationMs(precision),
                       false)) {
    return NAN;
  }
  return _reading.temperature;
}

float Massmore_SHT4x::readHumidity(Precision precision) {
  if (!measureBlocking(precisionCommand(precision), precisionDurationMs(precision),
                       false)) {
    return NAN;
  }
  return _reading.humidity;
}

bool Massmore_SHT4x::readAll(Readings &out, Precision precision) {
  if (!measureBlocking(precisionCommand(precision), precisionDurationMs(precision),
                       false)) {
    return false;
  }
  out = _reading;
  return true;
}

bool Massmore_SHT4x::readAll(float &temperature, float &humidity,
                             Precision precision) {
  if (!measureBlocking(precisionCommand(precision), precisionDurationMs(precision),
                       false)) {
    temperature = NAN;
    humidity = NAN;
    return false;
  }
  temperature = _reading.temperature;
  humidity = _reading.humidity;
  return true;
}

/* ========================================================================= */
/* Non-blocking FSM                                                          */
/* ========================================================================= */

bool Massmore_SHT4x::requestConversion(Precision precision) {
  if (!_begun) {
    return setError(ErrorCode::NOT_BEGUN);
  }
  if (_state == State::MEASURING || _state == State::HEATING) {
    return setError(ErrorCode::BUSY);
  }
  if (!sendCommand(precisionCommand(precision))) {
    return false;
  }
  _opStartMs = millis();
  _opDurationMs = precisionDurationMs(precision);
  _opHeated = false;
  _state = State::MEASURING;
  _error = ErrorCode::OK;
  return true;
}

bool Massmore_SHT4x::requestHeater(HeaterMode mode) {
  if (!_begun) {
    return setError(ErrorCode::NOT_BEGUN);
  }
  if (_state == State::MEASURING || _state == State::HEATING) {
    return setError(ErrorCode::BUSY);
  }
  uint16_t duration = heaterDurationMs(mode);
  if (!heaterAllowed(duration)) {
    return setError(ErrorCode::HEATER_DUTY);
  }
  if (!sendCommand(heaterCommand(mode))) {
    return false;
  }
  accountHeater(duration);
  _opStartMs = millis();
  _opDurationMs = duration;
  _opHeated = true;
  _state = State::HEATING;
  _error = ErrorCode::OK;
  return true;
}

bool Massmore_SHT4x::update() {
  if (_state != State::MEASURING && _state != State::HEATING) {
    return false;
  }
  uint32_t elapsed = (uint32_t)(millis() - _opStartMs);
  if (elapsed < _opDurationMs) {
    return false; /* ยังไม่ถึงเวลา ไม่แตะ Bus */
  }

  uint16_t rawT = 0;
  uint16_t rawRH = 0;
  if (readFrame(rawT, rawRH)) {
    storeReading(rawT, rawRH, _opHeated);
    _state = State::DATA_READY;
    _error = ErrorCode::OK;
    return true;
  }
  if (_error == ErrorCode::NOT_READY) {
    /* ชิปยัง NACK: รอต่อจนกว่าจะเกิน timeout */
    if (elapsed >= (uint32_t)_opDurationMs + _timeoutMs) {
      _state = State::IDLE;
      setError(ErrorCode::TIMEOUT);
    }
    return false;
  }
  /* CRC_FAIL หรือ Bus error: ยกเลิกงานนี้ */
  _state = State::IDLE;
  return false;
}

bool Massmore_SHT4x::isDataReady() const { return _state == State::DATA_READY; }

bool Massmore_SHT4x::getReadings(Readings &out) {
  if (_state != State::DATA_READY) {
    return setError(ErrorCode::NOT_READY);
  }
  out = _reading;
  _state = State::IDLE;
  _error = ErrorCode::OK;
  return true;
}

Massmore_SHT4x::State Massmore_SHT4x::getState() const { return _state; }

const Massmore_SHT4x::Readings &Massmore_SHT4x::lastReadings() const {
  return _reading;
}

/* ========================================================================= */
/* Heater                                                                    */
/* ========================================================================= */

bool Massmore_SHT4x::runHeater(HeaterMode mode, Readings *out) {
  if (!_begun) {
    return setError(ErrorCode::NOT_BEGUN);
  }
  uint16_t duration = heaterDurationMs(mode);
  if (!heaterAllowed(duration)) {
    return setError(ErrorCode::HEATER_DUTY);
  }
  /* นับ duty ก่อนยิง เพราะถึงอ่านผลไม่สำเร็จ Heater ก็ทำงานไปแล้ว */
  accountHeater(duration);
  if (!measureBlocking(heaterCommand(mode), duration, true)) {
    return false;
  }
  if (out != nullptr) {
    *out = _reading;
  }
  return true;
}

void Massmore_SHT4x::setHeaterDutyGuard(bool enabled) { _dutyGuard = enabled; }

/*
 * duty-cycle guard: datasheet กำหนดให้ใช้ Heater ที่ duty cycle < 10 %
 * กติกาที่ใช้: หลังจบ pulse ยาว d ms ต้องพัก >= 9·d ms ก่อนยิงครั้งถัดไป
 * (pulse แรกหลัง begin() ยิงได้ทันที)
 */
bool Massmore_SHT4x::heaterAllowed(uint16_t durationMs) const {
  (void)durationMs;
  if (!_dutyGuard) {
    return true;
  }
  return heaterCooldownRemainingMs() == 0;
}

uint32_t Massmore_SHT4x::heaterCooldownRemainingMs() const {
  if (!_dutyGuard || !_heaterUsed) {
    return 0;
  }
  uint32_t required = (uint32_t)_heaterLastDurationMs *
                      (uint32_t)((100 / HEATER_MAX_DUTY_PERCENT) - 1);
  uint32_t since = (uint32_t)(millis() - _heaterEndMs);
  return (since >= required) ? 0 : (required - since);
}

void Massmore_SHT4x::accountHeater(uint16_t durationMs) {
  _heaterUsed = true;
  _heaterLastDurationMs = durationMs;
  _heaterEndMs = millis() + durationMs;
}

uint16_t Massmore_SHT4x::heaterPowerMilliwatt(HeaterMode mode) {
  switch (mode) {
  case HeaterMode::MW200_1S:
  case HeaterMode::MW200_100MS:
    return 200;
  case HeaterMode::MW110_1S:
  case HeaterMode::MW110_100MS:
    return 110;
  default:
    return 20;
  }
}

uint16_t Massmore_SHT4x::heaterDurationMs(HeaterMode mode) {
  switch (mode) {
  case HeaterMode::MW200_1S:
  case HeaterMode::MW110_1S:
  case HeaterMode::MW20_1S:
    return HEATER_LONG_MS;
  default:
    return HEATER_SHORT_MS;
  }
}

uint8_t Massmore_SHT4x::heaterCommand(HeaterMode mode) {
  switch (mode) {
  case HeaterMode::MW200_1S:
    return CMD_HEATER_200MW_1S;
  case HeaterMode::MW200_100MS:
    return CMD_HEATER_200MW_0S1;
  case HeaterMode::MW110_1S:
    return CMD_HEATER_110MW_1S;
  case HeaterMode::MW110_100MS:
    return CMD_HEATER_110MW_0S1;
  case HeaterMode::MW20_1S:
    return CMD_HEATER_20MW_1S;
  default:
    return CMD_HEATER_20MW_0S1;
  }
}

/* ========================================================================= */
/* Chip identity                                                             */
/* ========================================================================= */

bool Massmore_SHT4x::readSerialNumber(uint32_t &serial) {
  if (!sendCommand(CMD_READ_SERIAL)) {
    return false;
  }
  delay(SERIAL_READ_MS);
  uint16_t high = 0;
  uint16_t low = 0;
  if (!readFrameWithRetry(high, low, _timeoutMs)) {
    return false;
  }
  serial = ((uint32_t)high << 16) | (uint32_t)low;
  _serialNumber = serial;
  _error = ErrorCode::OK;
  return true;
}

uint32_t Massmore_SHT4x::getSerialNumber() const { return _serialNumber; }

bool Massmore_SHT4x::verifyChipID() {
  uint32_t serial = 0;
  if (!readSerialNumber(serial)) {
    return false;
  }
  if (serial == 0x00000000UL || serial == 0xFFFFFFFFUL) {
    return setError(ErrorCode::WRONG_ID);
  }
  return true;
}

bool Massmore_SHT4x::isGenuine() {
  _genuineMask = 0;

  /* 1. ACK ที่ address */
  if (!isConnected()) {
    return false;
  }
  _genuineMask |= CHK_ACK;

  bool wasBegun = _begun;
  State wasState = _state;
  _begun = true;
  _state = State::IDLE;

  /* 2-3. Serial + CRC และค่าต้องสมเหตุสมผล */
  uint32_t serial1 = 0;
  bool serialOk = readSerialNumber(serial1);
  if (serialOk) {
    _genuineMask |= CHK_SERIAL_CRC;
    if (serial1 != 0x00000000UL && serial1 != 0xFFFFFFFFUL) {
      _genuineMask |= CHK_SERIAL_SANE;
    }
  }

  /* 4. อ่านซ้ำต้องได้ค่าเดิม */
  uint32_t serial2 = 0;
  if (serialOk && readSerialNumber(serial2) && serial2 == serial1) {
    _genuineMask |= CHK_SERIAL_STABLE;
  }
  _serialNumber = serial1;

  /* 5-7. soft reset แล้ววัดต่อได้, CRC ถูก, ค่าอยู่ในช่วง */
  bool resetOk = softReset();
  bool measOk = measureBlocking(CMD_MEAS_HIGH, MEAS_HIGH_MS, false);
  if (resetOk && measOk) {
    _genuineMask |= CHK_SOFT_RESET;
  }
  if (measOk) {
    _genuineMask |= CHK_MEAS_CRC;
    bool rawSane = (_reading.rawTemperature != 0x0000) &&
                   (_reading.rawTemperature != 0xFFFF) &&
                   (_reading.rawHumidity != 0xFFFF);
    float t = rawToCelsius(_reading.rawTemperature);
    float h = rawToHumidity(_reading.rawHumidity);
    if (rawSane && t > TEMP_MIN_C && t < TEMP_MAX_C && h > -6.0f && h < 119.0f) {
      _genuineMask |= CHK_MEAS_RANGE;
    }
  }

  /*
   * 8. อ่านก่อนวัดเสร็จ ชิปต้อง NACK
   * datasheet §4.3: "The sensor does not support clock-stretching ...
   * it will return a NACK" ของเลียนแบบที่ตอบจากตารางค่ามักตอบทันที
   */
  if (sendCommand(CMD_MEAS_HIGH)) {
    delay(MASSMORE_SHT4X_PROBE_EARLY_MS);
    uint16_t a = 0;
    uint16_t b = 0;
    if (!readFrame(a, b) && _error == ErrorCode::NOT_READY) {
      _genuineMask |= CHK_NACK_EARLY;
    }
    /* เก็บผลทิ้งเพื่อไม่ให้ค้าง */
    delay(MEAS_HIGH_MS);
    readFrameWithRetry(a, b, _timeoutMs);
  }

  /* 9. LOW_RES ต้องเสร็จภายใน 2 ms ซึ่ง HIGH_RES ยังไม่เสร็จแน่นอน */
  if (sendCommand(CMD_MEAS_LOW)) {
    delay(MEAS_LOW_MS);
    uint16_t a = 0;
    uint16_t b = 0;
    if (readFrame(a, b)) {
      _genuineMask |= CHK_LOW_FASTER;
    } else {
      delay(MEAS_HIGH_MS);
      readFrameWithRetry(a, b, _timeoutMs);
    }
  }

  /* คืนสภาพ */
  softReset();
  _begun = wasBegun;
  _state = (wasState == State::DATA_READY) ? State::DATA_READY : State::IDLE;
  _error = ErrorCode::OK;

  return (_genuineMask & CHK_ALL) == CHK_ALL;
}

uint16_t Massmore_SHT4x::getGenuineMask() const { return _genuineMask; }

uint8_t Massmore_SHT4x::getGenuinePassCount() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < CHK_COUNT; i++) {
    if (_genuineMask & (1u << i)) {
      n++;
    }
  }
  return n;
}

const char *Massmore_SHT4x::genuineCheckName(uint8_t index) {
  switch (index) {
  case 0:
    return "I2C_ACK";
  case 1:
    return "SERIAL_CRC";
  case 2:
    return "SERIAL_SANE";
  case 3:
    return "SERIAL_STABLE";
  case 4:
    return "SOFT_RESET";
  case 5:
    return "MEAS_CRC";
  case 6:
    return "MEAS_RANGE";
  case 7:
    return "NACK_WHILE_BUSY";
  case 8:
    return "LOW_RES_FASTER";
  default:
    return "UNKNOWN";
  }
}

/* ========================================================================= */
/* Reset                                                                     */
/* ========================================================================= */

bool Massmore_SHT4x::softReset() {
  if (!sendCommand(CMD_SOFT_RESET)) {
    return false;
  }
  delay(SOFT_RESET_MS);
  _state = State::IDLE;
  _error = ErrorCode::OK;
  return true;
}

bool Massmore_SHT4x::generalCallReset() {
  _wire->beginTransmission(GENERAL_CALL_ADDR);
  _wire->write(GENERAL_CALL_RESET);
  if (_wire->endTransmission() != 0) {
    return setError(ErrorCode::BUS_ERROR);
  }
  delay(SOFT_RESET_MS);
  _state = State::IDLE;
  _error = ErrorCode::OK;
  return true;
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

void Massmore_SHT4x::setTemperatureOffset(float offsetC) { _temperatureOffset = offsetC; }
void Massmore_SHT4x::setHumidityOffset(float offsetPercent) {
  _humidityOffset = offsetPercent;
}
float Massmore_SHT4x::getTemperatureOffset() const { return _temperatureOffset; }
float Massmore_SHT4x::getHumidityOffset() const { return _humidityOffset; }

void Massmore_SHT4x::setVariant(Variant variant) { _variant = variant; }
Massmore_SHT4x::Variant Massmore_SHT4x::getVariant() const { return _variant; }

const char *Massmore_SHT4x::getVariantName() const {
  switch (_variant) {
  case Variant::SHT40:
    return "SHT40";
  case Variant::SHT41:
    return "SHT41";
  case Variant::SHT45:
    return "SHT45";
  default:
    return "SHT4x";
  }
}

/* accuracy typ. จาก datasheet ตาราง "Humidity/Temperature sensor specifications" */
float Massmore_SHT4x::getTemperatureAccuracy() const {
  return (_variant == Variant::SHT45) ? 0.1f : 0.2f;
}
float Massmore_SHT4x::getHumidityAccuracy() const {
  return (_variant == Variant::SHT45) ? 1.0f : 1.8f;
}

uint8_t Massmore_SHT4x::getAddress() const { return _address; }

/* ========================================================================= */
/* Conversion & derived values                                               */
/* ========================================================================= */

uint8_t Massmore_SHT4x::precisionCommand(Precision precision) {
  switch (precision) {
  case Precision::LOW_RES:
    return CMD_MEAS_LOW;
  case Precision::MEDIUM_RES:
    return CMD_MEAS_MED;
  default:
    return CMD_MEAS_HIGH;
  }
}

uint8_t Massmore_SHT4x::precisionDurationMs(Precision precision) {
  switch (precision) {
  case Precision::LOW_RES:
    return MEAS_LOW_MS;
  case Precision::MEDIUM_RES:
    return MEAS_MED_MS;
  default:
    return MEAS_HIGH_MS;
  }
}

/* datasheet §4.6: T = -45 + 175 · S_T / (2^16 - 1) */
float Massmore_SHT4x::rawToCelsius(uint16_t raw) {
  return -45.0f + 175.0f * ((float)raw / 65535.0f);
}

/* datasheet §4.6: RH = -6 + 125 · S_RH / (2^16 - 1) */
float Massmore_SHT4x::rawToHumidity(uint16_t raw) {
  return -6.0f + 125.0f * ((float)raw / 65535.0f);
}

/* datasheet §4.4 "Checksum Calculation": poly 0x31, init 0xFF, CRC(0xBEEF)=0x92 */
uint8_t Massmore_SHT4x::crc8(const uint8_t *data, uint8_t length) {
  uint8_t crc = CRC8_INIT;
  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ CRC8_POLY) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

float Massmore_SHT4x::dewPoint(float temperatureC, float humidityPercent) {
  if (isnan(temperatureC) || isnan(humidityPercent) || humidityPercent <= 0.0f) {
    return NAN;
  }
  float gamma = (MASSMORE_SHT4X_MAGNUS_A * temperatureC) /
                    (MASSMORE_SHT4X_MAGNUS_B + temperatureC) +
                logf(humidityPercent / 100.0f);
  return (MASSMORE_SHT4X_MAGNUS_B * gamma) / (MASSMORE_SHT4X_MAGNUS_A - gamma);
}

float Massmore_SHT4x::absoluteHumidity(float temperatureC, float humidityPercent) {
  if (isnan(temperatureC) || isnan(humidityPercent)) {
    return NAN;
  }
  float es = 6.112f * expf((MASSMORE_SHT4X_MAGNUS_A * temperatureC) /
                           (MASSMORE_SHT4X_MAGNUS_B + temperatureC));
  return 216.7f * ((humidityPercent / 100.0f) * es) / (273.15f + temperatureC);
}

float Massmore_SHT4x::heatIndex(float temperatureC, float humidityPercent) {
  if (isnan(temperatureC) || isnan(humidityPercent)) {
    return NAN;
  }
  float tF = temperatureC * 9.0f / 5.0f + 32.0f;
  float rh = humidityPercent;
  float hi = 0.5f * (tF + 61.0f + ((tF - 68.0f) * 1.2f) + (rh * 0.094f));
  if (((hi + tF) / 2.0f) >= 80.0f) {
    hi = -42.379f + 2.04901523f * tF + 10.14333127f * rh - 0.22475541f * tF * rh -
         0.00683783f * tF * tF - 0.05481717f * rh * rh +
         0.00122874f * tF * tF * rh + 0.00085282f * tF * rh * rh -
         0.00000199f * tF * tF * rh * rh;
    if (rh < 13.0f && tF >= 80.0f && tF <= 112.0f) {
      hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - fabsf(tF - 95.0f)) / 17.0f);
    } else if (rh > 85.0f && tF >= 80.0f && tF <= 87.0f) {
      hi += ((rh - 85.0f) / 10.0f) * ((87.0f - tF) / 5.0f);
    }
  }
  return (hi - 32.0f) * 5.0f / 9.0f;
}

/* ========================================================================= */
/* Error reporting / misc                                                    */
/* ========================================================================= */

Massmore_SHT4x::ErrorCode Massmore_SHT4x::lastError() const { return _error; }

const char *Massmore_SHT4x::lastErrorString() const { return errorToString(_error); }

const char *Massmore_SHT4x::errorToString(ErrorCode error) {
  switch (error) {
  case ErrorCode::OK:
    return "OK";
  case ErrorCode::NOT_BEGUN:
    return "NOT_BEGUN";
  case ErrorCode::NOT_FOUND:
    return "NOT_FOUND";
  case ErrorCode::WRONG_ID:
    return "WRONG_ID";
  case ErrorCode::TIMEOUT:
    return "TIMEOUT";
  case ErrorCode::CRC_FAIL:
    return "CRC_FAIL";
  case ErrorCode::BUS_ERROR:
    return "BUS_ERROR";
  case ErrorCode::NOT_READY:
    return "NOT_READY";
  case ErrorCode::BUSY:
    return "BUSY";
  case ErrorCode::BAD_ARG:
    return "BAD_ARG";
  case ErrorCode::HEATER_DUTY:
    return "HEATER_DUTY";
  default:
    return "UNKNOWN";
  }
}

uint8_t Massmore_SHT4x::scan(TwoWire &wirePort, uint8_t *found) {
  if (found == nullptr) {
    return 0;
  }
  const uint8_t candidates[3] = {I2C_ADDR_A, I2C_ADDR_B, I2C_ADDR_C};
  uint8_t count = 0;
  for (uint8_t i = 0; i < 3; i++) {
    wirePort.beginTransmission(candidates[i]);
    if (wirePort.endTransmission() == 0) {
      found[count++] = candidates[i];
    }
  }
  return count;
}

const char *Massmore_SHT4x::version() { return MASSMORE_SHT4X_VERSION; }
