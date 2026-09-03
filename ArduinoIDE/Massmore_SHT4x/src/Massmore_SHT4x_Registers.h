/*!
 * @file Massmore_SHT4x_Registers.h
 * @brief ตารางคำสั่ง I2C, ค่าเวลา และค่าคงที่การแปลงสัญญาณของ SHT4x
 *
 * ทุกค่าในไฟล์นี้อ้างอิงจากเอกสารต้นทางโดยตรง ไม่ได้คัดลอกมาจากไลบรารีเจ้าอื่น
 *   - Datasheet SHT4x (Sensirion, Version 7.3)
 *     https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf
 *
 * ข้อแตกต่างสำคัญจาก SHT3x ที่คนย้ายมาจากไลบรารีเดิมต้องรู้
 *   1. คำสั่งของ SHT4x ยาว "1 ไบต์" ไม่ใช่ 16 บิตแบบ SHT3x
 *   2. SHT4x ไม่มี status register, ไม่มีขา ALERT, ไม่มีโหมด periodic และไม่มี ART
 *      มีแต่การวัดแบบ single shot สามระดับความละเอียด กับฮีตเตอร์ 6 โหมด
 *   3. SHT4x ไม่รองรับ clock stretching เลย ถ้าอ่านผลก่อนวัดเสร็จ ชิปจะ NACK
 *      (datasheet: "The sensor does not support clock-stretching ... it will return a NACK")
 *   4. สูตรความชื้นมี offset -6 และ span 125 ไม่ใช่ 0 ถึง 100 แบบ SHT3x
 *
 * @copyright Copyright (c) 2026 Massmore Biz Co., Ltd.
 * @license MIT
 */

#ifndef MASSMORE_SHT4X_REGISTERS_H
#define MASSMORE_SHT4X_REGISTERS_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* I2C address                                                               */
/*                                                                           */
/* SHT4x กำหนด address มาจากโรงงานตามรหัสรุ่น เปลี่ยนเองไม่ได้ ไม่มีขา ADDR    */
/*   SHT40-AD1B / SHT41-AD1B / SHT43-ADCB / SHT45-AD1B  ->  0x44             */
/*   SHT40-BD1B                                          ->  0x45             */
/*   SHT40-CD1B                                          ->  0x46             */
/* บอร์ด Massmore SHT4X (SKU-1022) ใช้รุ่น -A ทั้งหมด จึงเป็น 0x44 เสมอ        */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_I2C_ADDR_A 0x44 /*!< รุ่น A (บอร์ด Massmore ใช้ตัวนี้) */
#define MASSMORE_SHT4X_I2C_ADDR_B 0x45 /*!< รุ่น B เช่น SHT40-BD1B */
#define MASSMORE_SHT4X_I2C_ADDR_C 0x46 /*!< รุ่น C เช่น SHT40-CD1B */
/*! ค่าเริ่มต้นที่ begin() ใช้เมื่อไม่ระบุ */
#define MASSMORE_SHT4X_I2C_ADDR_DEFAULT MASSMORE_SHT4X_I2C_ADDR_A

/* ------------------------------------------------------------------------- */
/* คำสั่งวัด (1 ไบต์) - ชิปตอบ T(2) + CRC(1) + RH(2) + CRC(1)                 */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_CMD_MEAS_HIGH 0xFD /*!< ความละเอียดสูง  ~6.9 ms (สูงสุด 8.3) */
#define MASSMORE_SHT4X_CMD_MEAS_MED 0xF6  /*!< ความละเอียดกลาง ~3.7 ms (สูงสุด 4.5) */
#define MASSMORE_SHT4X_CMD_MEAS_LOW 0xE0  /*!< ความละเอียดต่ำ  ~1.3 ms (สูงสุด 1.6) */

/* ------------------------------------------------------------------------- */
/* คำสั่งระบบ                                                                 */
/* ------------------------------------------------------------------------- */

/*! อ่านหมายเลขซีเรียลจากโรงงาน ตอบ 2 word + CRC ต่อ word (รวม 6 ไบต์) */
#define MASSMORE_SHT4X_CMD_READ_SERIAL 0x89
/*! รีเซ็ตซอฟต์แวร์ ใช้เวลาไม่เกิน 1 ms */
#define MASSMORE_SHT4X_CMD_SOFT_RESET 0x94

/* ------------------------------------------------------------------------- */
/* คำสั่งฮีตเตอร์                                                             */
/*                                                                           */
/* ทุกคำสั่งฮีตเตอร์จะ "วัดความละเอียดสูงหนึ่งครั้งก่อนดับฮีตเตอร์" ให้อัตโนมัติ  */
/* จึงตอบข้อมูล 6 ไบต์เหมือนคำสั่งวัดปกติ                                      */
/* datasheet กำหนดว่าฮีตเตอร์ออกแบบมาให้ใช้ที่ duty cycle ต่ำกว่า 10%           */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_CMD_HEATER_200MW_1S 0x39   /*!< 200 mW นาน 1 วินาที */
#define MASSMORE_SHT4X_CMD_HEATER_200MW_0S1 0x32  /*!< 200 mW นาน 0.1 วินาที */
#define MASSMORE_SHT4X_CMD_HEATER_110MW_1S 0x2F   /*!< 110 mW นาน 1 วินาที */
#define MASSMORE_SHT4X_CMD_HEATER_110MW_0S1 0x24  /*!< 110 mW นาน 0.1 วินาที */
#define MASSMORE_SHT4X_CMD_HEATER_20MW_1S 0x1E    /*!< 20 mW นาน 1 วินาที */
#define MASSMORE_SHT4X_CMD_HEATER_20MW_0S1 0x15   /*!< 20 mW นาน 0.1 วินาที */

/*!
 * คำสั่งที่ไม่มีในตารางของ datasheet ใช้ทดสอบว่าชิปปฏิเสธคำสั่งแปลกปลอมจริง
 * ชิปแท้จะไม่ตอบข้อมูลกลับมา (NACK ตอนอ่าน)
 */
#define MASSMORE_SHT4X_CMD_BOGUS 0x77

/*! General call reset: ส่งไปที่ address 0x00 ด้วยข้อมูล 1 ไบต์ = 0x06 */
#define MASSMORE_SHT4X_GENERAL_CALL_ADDR 0x00
#define MASSMORE_SHT4X_GENERAL_CALL_RESET_BYTE 0x06

/* ------------------------------------------------------------------------- */
/* CRC-8 ตาม datasheet ตาราง "CRC properties"                                */
/* ตรวจสอบด้วยเวกเตอร์ทดสอบของ Sensirion: CRC(0xBEEF) = 0x92                  */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_CRC8_POLYNOMIAL 0x31 /*!< x^8 + x^5 + x^4 + 1 */
#define MASSMORE_SHT4X_CRC8_INIT 0xFF       /*!< ค่าเริ่มต้น */
#define MASSMORE_SHT4X_CRC8_FINAL_XOR 0x00  /*!< ไม่ XOR ตอนจบ */

/* ------------------------------------------------------------------------- */
/* เวลาที่ต้องรอ (มิลลิวินาที) เผื่อจากค่าสูงสุดใน datasheet ไว้แล้ว             */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_MEAS_DURATION_LOW_MS 3   /*!< สูงสุด 1.6 ms + เผื่อ */
#define MASSMORE_SHT4X_MEAS_DURATION_MED_MS 6   /*!< สูงสุด 4.5 ms + เผื่อ */
#define MASSMORE_SHT4X_MEAS_DURATION_HIGH_MS 10 /*!< สูงสุด 8.3 ms + เผื่อ */

#define MASSMORE_SHT4X_HEATER_LONG_MS 1100  /*!< พัลส์ยาว สูงสุด 1.1 s */
#define MASSMORE_SHT4X_HEATER_SHORT_MS 120  /*!< พัลส์สั้น สูงสุด 0.11 s */

#define MASSMORE_SHT4X_SOFT_RESET_MS 2 /*!< สูงสุด 1 ms + เผื่อ */
#define MASSMORE_SHT4X_POWER_UP_MS 2   /*!< สูงสุด 1 ms + เผื่อ */
#define MASSMORE_SHT4X_CMD_GAP_MS 1    /*!< เว้นระหว่างคำสั่งอย่างน้อย 1 ms */
#define MASSMORE_SHT4X_SERIAL_READ_MS 2 /*!< รอก่อนอ่านซีเรียล */

/*! duty cycle สูงสุดของฮีตเตอร์ตาม datasheet (เปอร์เซ็นต์) */
#define MASSMORE_SHT4X_HEATER_MAX_DUTY_PERCENT 10

/* ------------------------------------------------------------------------- */
/* ค่าคงที่สำหรับการแปลงสัญญาณ (datasheet หัวข้อ 4.6)                          */
/*   T(C) = -45 + 175 * S_T  / (2^16 - 1)                                    */
/*   T(F) = -49 + 315 * S_T  / (2^16 - 1)                                    */
/*   RH   =  -6 + 125 * S_RH / (2^16 - 1)                                    */
/* ต้องหารด้วย 65535 ไม่ใช่ 65536                                             */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_RAW_FULL_SCALE 65535.0f /*!< 2^16 - 1 */
#define MASSMORE_SHT4X_T_C_OFFSET (-45.0f)
#define MASSMORE_SHT4X_T_C_SPAN 175.0f
#define MASSMORE_SHT4X_T_F_OFFSET (-49.0f)
#define MASSMORE_SHT4X_T_F_SPAN 315.0f
#define MASSMORE_SHT4X_RH_OFFSET (-6.0f)
#define MASSMORE_SHT4X_RH_SPAN 125.0f

/*! ความยาวข้อมูลตอนอ่านผลวัด: T(2) + CRC(1) + RH(2) + CRC(1) */
#define MASSMORE_SHT4X_MEAS_FRAME_LEN 6
/*! ความยาวข้อมูลตอนอ่านซีเรียล: word(2) + CRC(1) + word(2) + CRC(1) */
#define MASSMORE_SHT4X_SERIAL_FRAME_LEN 6

/* ------------------------------------------------------------------------- */
/* ขอบเขตทางกายภาพของชิป ใช้ตรวจความสมเหตุสมผลของค่าที่อ่านได้                 */
/* ------------------------------------------------------------------------- */

#define MASSMORE_SHT4X_T_MIN_C (-40.0f)
#define MASSMORE_SHT4X_T_MAX_C 125.0f
#define MASSMORE_SHT4X_RH_MIN_PERCENT 0.0f
#define MASSMORE_SHT4X_RH_MAX_PERCENT 100.0f

#endif /* MASSMORE_SHT4X_REGISTERS_H */
