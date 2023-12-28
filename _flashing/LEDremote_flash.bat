@echo off
echo Flashing LEDremote firmware...
esptool.py write_flash 0x10000 ./LEDremote.bin
echo.
echo Flashing done !!
timeout 5
exit
