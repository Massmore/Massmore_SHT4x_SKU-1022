/*!
 * @file massmore_sht4x_host_shim.cpp
 * @brief การทำงานของชิป SHT4x จำลอง (ใช้เฉพาะ host test)
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#include "massmore_sht4x_host_shim.h"

#include "../lib/Massmore_SHT4x/src/Massmore_SHT4x_Registers.h"

uint32_t g_mockMillis = 0;
uint8_t g_mockPinMode[64] = {0};
uint8_t g_mockPinLevel[64] = {0};
MockSht4x g_mockChip;
TwoWire Wire;

/* เวลาที่ชิปจำลองใช้วัดจริง (น้อยกว่าค่าที่ไลบรารีรอ เพื่อเลียนแบบของจริง) */
#define MOCK_DUR_LOW 2
#define MOCK_DUR_MED 4
#define MOCK_DUR_HIGH 7
#define MOCK_DUR_HEATER_LONG 1000
#define MOCK_DUR_HEATER_SHORT 100

void mockResetChip() {
  g_mockChip = MockSht4x();
  g_mockMillis = 0;
}

uint8_t mockCrc8(const uint8_t *data, uint8_t length) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

/*! ใส่ word พร้อม CRC ลงบัฟเฟอร์ตอบกลับ */
static void pushWord(uint16_t value) {
  uint8_t *b = g_mockChip.rxBuffer;
  uint8_t i = g_mockChip.rxLength;
  b[i] = (uint8_t)(value >> 8);
  b[i + 1] = (uint8_t)(value & 0xFF);
  b[i + 2] = mockCrc8(&b[i], 2);
  if (g_mockChip.corruptCrc) {
    b[i + 2] = (uint8_t)(b[i + 2] ^ 0xFF);
  }
  g_mockChip.rxLength = (uint8_t)(i + 3);
}

uint8_t TwoWire::endTransmission() {
  MockSht4x &chip = g_mockChip;

  /* general call reset */
  if (_address == MASSMORE_SHT4X_GENERAL_CALL_ADDR) {
    if (chip.txLength == 1 &&
        chip.txBuffer[0] == MASSMORE_SHT4X_GENERAL_CALL_RESET_BYTE) {
      chip.readyAtMs = g_mockMillis;
      chip.lastWasHeater = false;
      return 0;
    }
    return 2;
  }

  if (!chip.present || _address != chip.address) {
    return 2; /* NACK ที่ address */
  }

  /* ping เปล่า ๆ ใช้ตรวจว่ามีอุปกรณ์อยู่ */
  if (chip.txLength == 0) {
    return 0;
  }

  uint8_t cmd = chip.txBuffer[0];
  chip.lastCommand = cmd;
  chip.commandCount++;
  chip.commandKnown = true;
  chip.lastWasHeater = false;

  switch (cmd) {
  case MASSMORE_SHT4X_CMD_MEAS_LOW:
    chip.readyAtMs = g_mockMillis + MOCK_DUR_LOW;
    return 0;
  case MASSMORE_SHT4X_CMD_MEAS_MED:
    chip.readyAtMs = g_mockMillis + MOCK_DUR_MED;
    return 0;
  case MASSMORE_SHT4X_CMD_MEAS_HIGH:
    chip.readyAtMs = g_mockMillis + MOCK_DUR_HIGH;
    return 0;

  case MASSMORE_SHT4X_CMD_READ_SERIAL:
    if (!chip.supportsSerial) {
      chip.commandKnown = false;
      return 2;
    }
    chip.readyAtMs = g_mockMillis;
    return 0;

  case MASSMORE_SHT4X_CMD_SOFT_RESET:
    chip.readyAtMs = g_mockMillis;
    return 0;

  case MASSMORE_SHT4X_CMD_HEATER_200MW_1S:
  case MASSMORE_SHT4X_CMD_HEATER_110MW_1S:
  case MASSMORE_SHT4X_CMD_HEATER_20MW_1S:
    if (!chip.supportsHeater) {
      chip.commandKnown = false;
      return 2;
    }
    chip.readyAtMs = g_mockMillis + MOCK_DUR_HEATER_LONG;
    chip.lastWasHeater = true;
    chip.heaterPulses++;
    return 0;

  case MASSMORE_SHT4X_CMD_HEATER_200MW_0S1:
  case MASSMORE_SHT4X_CMD_HEATER_110MW_0S1:
  case MASSMORE_SHT4X_CMD_HEATER_20MW_0S1:
    if (!chip.supportsHeater) {
      chip.commandKnown = false;
      return 2;
    }
    chip.readyAtMs = g_mockMillis + MOCK_DUR_HEATER_SHORT;
    chip.lastWasHeater = true;
    chip.heaterPulses++;
    return 0;

  default:
    /* คำสั่งที่ไม่มีในตาราง ชิปแท้รับไบต์ไว้แต่ไม่มีข้อมูลให้อ่าน */
    chip.commandKnown = chip.answersBogusCommand;
    chip.readyAtMs = g_mockMillis;
    return 0;
  }
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t length) {
  MockSht4x &chip = g_mockChip;
  chip.rxLength = 0;
  chip.rxIndex = 0;

  if (!chip.present || address != chip.address) {
    return 0;
  }

  /* ยังวัดไม่เสร็จ ชิปแท้จะ NACK ที่ header */
  if (chip.nackWhenBusy && g_mockMillis < chip.readyAtMs) {
    chip.nackCount++;
    return 0;
  }

  if (!chip.commandKnown) {
    return 0; /* คำสั่งนอกตาราง ไม่มีอะไรให้อ่าน */
  }

  if (chip.lastCommand == MASSMORE_SHT4X_CMD_READ_SERIAL) {
    chip.serialReads++;
    uint32_t value = chip.serial;
    if (chip.serialDrifts) {
      value = chip.serial + chip.serialReads;
    }
    pushWord((uint16_t)(value >> 16));
    pushWord((uint16_t)(value & 0xFFFF));
  } else if (chip.lastCommand == MASSMORE_SHT4X_CMD_SOFT_RESET) {
    return 0; /* soft reset ไม่ตอบข้อมูล */
  } else {
    uint16_t t = chip.rawT;
    if (chip.lastWasHeater) {
      uint32_t boosted = (uint32_t)t + chip.heatBoostRaw;
      t = (boosted > 0xFFFF) ? 0xFFFF : (uint16_t)boosted;
    }
    pushWord(t);
    pushWord(chip.rawRH);
  }

  if (chip.rxLength < length) {
    return chip.rxLength;
  }
  return length;
}
