FROM ubuntu:latest

RUN apt update && apt install -y wget xvfb xz-utils libxrender1  libxtst6  libxi6

# before install
RUN export DISPLAY=:1.0
RUN wget https://downloads.arduino.cc/arduino-1.8.10-linux64.tar.xz
RUN tar xf arduino-1.8.10-linux64.tar.xz
RUN mv arduino-1.8.10 /usr/local/share/arduino
RUN ln -s /usr/local/share/arduino/arduino /usr/local/bin/arduino
RUN wget https://github.com/me-no-dev/ESPAsyncWebServer/archive/master.zip
RUN apt install -y unzip
RUN unzip master.zip
RUN mv ESPAsyncWebServer-master /usr/local/share/arduino/libraries/ESPAsyncWebServer
RUN rm master.zip
RUN wget https://github.com/me-no-dev/AsyncTCP/archive/master.zip
RUN unzip master.zip
RUN mv AsyncTCP-master /usr/local/share/arduino/libraries/AsyncTCP
RUN wget https://github.com/me-no-dev/arduino-esp32fs-plugin/releases/download/1.0/ESP32FS-1.0.zip
RUN wget https://github.com/lewisxhe/AXP202X_Library/archive/v1.0.zip
RUN unzip v1.0.zip
RUN mv AXP202X_Library-1.0 /usr/local/share/arduino/libraries/


# install

RUN Xvfb :99 &

ENV DISPLAY :99

RUN arduino --pref "boardsmanager.additional.urls=https://dl.espressif.com/dl/package_esp32_index.json" --save-prefs
RUN arduino --install-boards esp32:esp32 --save-prefs
RUN arduino --pref "build.verbose=true" --save-prefs
RUN arduino --pref "custom_FlashFreq=ttgo-lora32-v1_80" --save-prefs
RUN arduino --pref "build.path=/work/build" --save-prefs
RUN arduino --install-library "U8g2"
RUN arduino --install-library "MicroNMEA"

RUN apt install -y python python-serial 


ENV ESP32TOOLS /root/.arduino15/packages/esp32/hardware/esp32/1.0.4/tools
ENV MKSPIFFS /root/.arduino15/packages/esp32/tools/mkspiffs/0.2.3/mkspiffs


CMD ["./build.sh"]
