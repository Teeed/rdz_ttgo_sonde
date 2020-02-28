#!/bin/bash
set -e
set +x

cd /work

mkdir -p $PWD/build 
ln -s $PWD/libraries/SondeLib /usr/local/share/arduino/libraries/SondeLib 
ln -s $PWD/libraries/SX1278FSK /usr/local/share/arduino/libraries/SX1278FSK 
arduino --board esp32:esp32:t-beam --verify $PWD/RX_FSK/RX_FSK.ino 

mkdir -p $PWD/build/out

$MKSPIFFS -c $PWD/RX_FSK/data -b 4096 -p 256 -s 1503232 $PWD/build/out/spiffs.bin 
$PWD/scripts/makeimage.py $ESP32TOOLS $PWD/build/RX_FSK.ino.bin $PWD/build/out/spiffs.bin $PWD/build/out/out.bin
