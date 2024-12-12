#!/bin/bash

if [ $# -lt 1 ]; then													# sanity check: at least one parameter is given
	echo "Usage: $0 {-e|-b|-f} <file>"
	exit 1
fi

COMMAND=$1																	# input command
IN_FILE=$2																	# input file

case $COMMAND in
	-e)
		echo "Erasing flash..."
		esptool.py erase_flash
		if [ $? -ne 0 ]; then											# check for esptool.py error code
			echo "Error: failed to erase flash."
			exit 1
		fi
		echo "Erase complete !!"
		;;

	-b)
		if [ -f "$IN_FILE" ]; then										# sanity check: is it a file?
			echo "Error: bootloader file is required!"
			exit 1
		fi
		echo "Flashing bootloader..."
		esptool.py write_flash 0x00000 "$IN_FILE"
		if [ $? -ne 0 ]; then											# check for esptool.py error code
			echo "Error: failed to flash bootloader."
			exit 1
		fi
		echo "Flashing done !!"
		;;

	-f)
		if [ -f "$IN_FILE" ]; then										# sanity check: is it a file?
			echo "Error: firmware file is required!"
			exit 1
		fi
		echo "Flashing firmware..."
		esptool.py write_flash 0x10000 "$IN_FILE"
		if [ $? -ne 0 ]; then											# check for esptool.py error code
			echo "Error: failed to flash firmware."
			exit 1
		fi
		echo "Flashing done !!"
		;;

	*)
		echo "Invalid command! Usage: $0 {-e|-b|-f} <file>"
		exit 1
		;;
esac
