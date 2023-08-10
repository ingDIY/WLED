@echo off
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo ++++                       ESP32 Flash Erase script:                        ++++
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo.
echo.
echo Erasing Flash...
esptool.py erase_flash
echo --------------------------------------------------------------------------------
echo.
echo Erase complete !!
timeout 5
exit
