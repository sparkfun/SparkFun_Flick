# CMakeLists.txt of a Baseline extension (docs/EXTENSIONS.md), staged by flick
# as components/bl_ext_<name> unless the extension brings its own. All of its
# sources; linked whole, because nothing in Baseline refers to them (they
# register themselves with BL_EXTENSION). It may use what Arduino-ESP32 and the
# ESP-IDF components Arduino depends on provide (Bluetooth, WiFi, mbedTLS, ...).
file(GLOB_RECURSE ext_srcs "${CMAKE_CURRENT_LIST_DIR}/*.c" "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
idf_component_register(SRCS ${ext_srcs}
                       INCLUDE_DIRS .
                       PRIV_REQUIRES arduino
                       WHOLE_ARCHIVE)
# sdk/include (baseline_extension.h, baseline_types.h) and the target's sdk folder
target_include_directories(${COMPONENT_LIB} PRIVATE ${FLICK_INCLUDE_DIRS})
