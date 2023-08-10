@echo off
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo ++++              Compiled files and folders cleaning script:               ++++
echo ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
echo.
echo.
RD /S /Q "%cd%\.pio"
RD /S /Q "%cd%\build_output"
RD /S /Q "%cd%\node_modules"
RD /S /Q "%cd%\wled00\extLibs"
RD /S /Q "%cd%\wled00\Release"
echo --------------------------------------------------------------------------------
echo.
echo cleaning complete !!
timeout 2
exit
