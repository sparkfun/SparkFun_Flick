# arduino_tinyusb, prebuilt. Arduino-ESP32 ships TinyUSB only as a library
# built by its lib-builder (same ESP-IDF, and the sdkconfig Baseline starts
# from), not as a registry component. flick stages the headers in include/, the
# library in lib/, and a Kconfig with the CONFIG_TINYUSB_* values it was built
# with (the Arduino core and Baseline check them).
idf_component_register(INCLUDE_DIRS include/include include/tinyusb/src
                       REQUIRES freertos esp_hw_support usb esp_rom hal soc log)
add_prebuilt_library(arduino_tinyusb_prebuilt "${CMAKE_CURRENT_LIST_DIR}/lib/libarduino_tinyusb.a"
                     REQUIRES freertos esp_hw_support usb esp_rom hal soc log)
target_link_libraries(${COMPONENT_LIB} INTERFACE arduino_tinyusb_prebuilt)
