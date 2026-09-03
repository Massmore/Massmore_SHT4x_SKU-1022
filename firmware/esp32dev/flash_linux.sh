#!/bin/bash
# ============================================================================
#  Massmore SHT4X (SKU-1022) - อัปโหลดเฟิร์มแวร์ Factory Test ลง ESP32
#  สำหรับ Linux :  chmod +x flash_linux.sh && ./flash_linux.sh
# ============================================================================

cd "$(dirname "$0")" || exit 1

BAUD=512000
MERGED="Massmore_SHT4x_FactoryTest_v1.0.0_esp32dev_merged.bin"

if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "ไม่พบ esptool  ติดตั้งด้วย:  pip3 install esptool"
  exit 1
fi

PORT=""
for p in /dev/ttyUSB* /dev/ttyACM*; do
  if [ -e "$p" ]; then
    PORT="$p"
    break
  fi
done

if [ -z "$PORT" ]; then
  echo "ไม่พบพอร์ตของบอร์ด (ลอง ls /dev/ttyUSB*)"
  echo "ถ้าเจอพอร์ตแต่เปิดไม่ได้ ให้สั่ง: sudo usermod -a -G dialout \$USER แล้วล็อกเอาต์เข้าใหม่"
  exit 1
fi

echo "พอร์ตที่พบ : $PORT   ความเร็ว : $BAUD"

$ESPTOOL --chip esp32 --port "$PORT" --baud $BAUD \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
  0x0 "$MERGED"
