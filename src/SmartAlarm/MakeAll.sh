#!/bin/bash
export PATH=/opt/xtensa-esp32-elf/bin:$PATH
rm -rf build
rm -f sdkconfig
rm -f sdkconfig.old
cd ..
#rm -rf esp-idf
#unzip esp-idf.zip -d .
chmod -R 777 esp-idf/
cd SmartAlarm
unset IDF_PATH
export IDF_PATH="./../esp-idf"
make all
