@echo off
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo ++++                    ESP32 Firmware flashing script:                     ++++
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo.
echo.
echo Programming Bootloader...
esptool.py write_flash 0x10000 ./LEDdrums.bin
echo --------------------------------------------------------------------------------
echo.
echo Programming done !!
timeout 5
exit
