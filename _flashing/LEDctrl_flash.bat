@echo off                                             
echo Flashing LEDctrl firmware...                     
esptool.py write_flash 0x10000 ./LEDctrl.bin          
echo.                                                 
echo Flashing done !!                                 
timeout 5                                             
exit                                                  
