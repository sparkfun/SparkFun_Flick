# Baseline's own sources (staged here by bsk, which touches this file when the
# set of files changes so CMake globs again). With no REQUIRES, the main
# component may use every other component in the build.
file(GLOB_RECURSE bl_srcs "${CMAKE_CURRENT_LIST_DIR}/src/*.c" "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
idf_component_register(SRCS baseline_main.cpp ${bl_srcs}
                       INCLUDE_DIRS . src)

# sdk/include and targets/<target>/sdk (shared with sketches); bsk passes them
# with -DBSK_INCLUDE_DIRS=...
target_include_directories(${COMPONENT_LIB} PRIVATE ${BSK_INCLUDE_DIRS})
# Bluetooth Classic SPP uses Arduino's BluetoothSerial, which is marked deprecated.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations)
