# Baseline as an ESP-IDF project, with Arduino-ESP32 as a component.
#
# `flick baseline build -t <target>` stages this project per target in
# %LOCALAPPDATA%\Flick\<target>\idf (see tools/flick.py, stage_idf):
#   main/                   baseline/ + targets/<target>/baseline/, and idf/main/
#   components/arduino/     the installed Arduino-ESP32 core, with idf/arduino_component.yml
#   sdkconfig.defaults.arduino   the sdkconfig Arduino's precompiled libraries were built with
#   sdkconfig.defaults           idf/sdkconfig.defaults + targets/<target>/idf/sdkconfig.defaults
# Later defaults files override earlier ones, so Baseline starts from exactly
# Arduino's configuration and changes only what the two override files list.
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults.arduino;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Compile definitions every component sees, like the board options of an
# Arduino-IDE build (target.json "idf_defines"; flick passes -DFLICK_DEFINES=...).
if(FLICK_DEFINES)
    idf_build_set_property(COMPILE_DEFINITIONS "${FLICK_DEFINES}" APPEND)
endif()
# Classic ESP32: Arduino's network event task peaks at ~1 KB of its 4 KB stack
# (Baseline's handlers only queue events for the sketch); 3 KB leaves internal
# RAM for Bluetooth and WiFi together (chips/esp32/idf/sdkconfig.defaults).
if(IDF_TARGET STREQUAL "esp32")
    idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_NETWORK_EVENT_TASK_STACK_SIZE=3072" APPEND)
endif()
project(baseline)
