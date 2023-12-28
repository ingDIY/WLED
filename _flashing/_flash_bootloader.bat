@echo off
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo ++++                   ESP32 Bootloader flashing script:                    ++++
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo.
echo.
echo Programming Bootloader...
esptool.py write_flash 0x00000 ./esp32_bootloader_v4.bin
echo --------------------------------------------------------------------------------
echo.
echo Programming done !!
timeout 5
exit
