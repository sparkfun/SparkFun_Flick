# Baseline as an ESP-IDF project, with Arduino-ESP32 as a component.
#
# `bsk baseline build -t <target>` stages this project per target in
# %LOCALAPPDATA%\BaselineSketcher\<target>\idf (see tools/bsk.py, stage_idf):
#   main/                   baseline/ + targets/<target>/baseline/, and idf/main/
#   components/arduino/     the installed Arduino-ESP32 core, with idf/arduino_component.yml
#   sdkconfig.defaults.arduino   the sdkconfig Arduino's precompiled libraries were built with
#   sdkconfig.defaults           idf/sdkconfig.defaults + targets/<target>/idf/sdkconfig.defaults
# Later defaults files override earlier ones, so Baseline starts from exactly
# Arduino's configuration and changes only what the two override files list.
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults.arduino;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(baseline)
