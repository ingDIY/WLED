@echo off
echo Flashing LEDdrums firmware...
esptool.py write_flash 0x10000 ./LEDdrums.bin
echo.
echo Flashing done !!
timeout 5
exit
