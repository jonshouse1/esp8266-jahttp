#
# Define local paths for build tools.
#
# For each path below that does not match your setup,
# uncomment the definition line and change the path
# 

# Base directory for the compiler
XTENSA_TOOLS_ROOT = /home/jon/esp-open-sdk/xtensa-lx106-elf/bin

# base directory of the ESP8266 SDK package, absolute
SDK_BASE = /home/jon/esp-open-sdk/sdk

#Esptool.py path and port
#ESPTOOL = esptool.py
#ESPPORT = /dev/ttyUSB0

# program to create firmware files that can be flashed
#FW_TOOL = /usr/bin/esptool
#FW_TOOL = /home/jon/esp8266-examples/esp8266ws2812i2s/esptool/esptool
FW_TOOL = ./tools/esptool/esptool

