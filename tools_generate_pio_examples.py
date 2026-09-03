#!/usr/bin/env python3
"""
สร้าง PlatformIO/examples/<ชื่อ>/main.cpp จาก ArduinoIDE/.../examples/<ชื่อ>/<ชื่อ>.ino
เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่หัวไฟล์ 6 บรรทัด + #include <Arduino.h>

รันทุกครั้งที่แก้ .ino:   python3 tools_generate_pio_examples.py
"""
import os
import shutil

ROOT = os.path.dirname(os.path.abspath(__file__))
INO_DIR = os.path.join(ROOT, "ArduinoIDE", "Massmore_SHT4x", "examples")
PIO_DIR = os.path.join(ROOT, "PlatformIO", "examples")

HEADER = """/*
  ไฟล์นี้สร้างจากตัวอย่างชื่อเดียวกันในโฟลเดอร์ ArduinoIDE
  เนื้อหาเหมือนกันทุกบรรทัด ต่างแค่ #include <Arduino.h> ที่ PlatformIO ต้องการ
  วิธีใช้: คัดลอกไฟล์นี้ไปทับ PlatformIO/src/main.cpp แล้วกด Upload
*/

#include <Arduino.h>

"""

def main():
    names = sorted(os.listdir(INO_DIR))
    for name in names:
        ino = os.path.join(INO_DIR, name, name + ".ino")
        if not os.path.isfile(ino):
            continue
        out_dir = os.path.join(PIO_DIR, name)
        os.makedirs(out_dir, exist_ok=True)
        with open(ino, encoding="utf-8") as f:
            body = f.read()
        with open(os.path.join(out_dir, "main.cpp"), "w", encoding="utf-8") as f:
            f.write(HEADER + body)
        print("สร้าง", os.path.join("PlatformIO", "examples", name, "main.cpp"))

    # ตัวอย่างแรกเป็นค่าเริ่มต้นของ src/main.cpp
    first = names[0]
    shutil.copy(os.path.join(PIO_DIR, first, "main.cpp"),
                os.path.join(ROOT, "PlatformIO", "src", "main.cpp"))
    print("คัดลอก", first, "ไปเป็น PlatformIO/src/main.cpp")

if __name__ == "__main__":
    main()
