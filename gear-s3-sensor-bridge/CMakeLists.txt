cmake_minimum_required(VERSION 3.5)
project(gear_s3_sensor_bridge C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SB_BUILD_APP "Build the Tizen ARM application" ON)
option(SB_BUILD_TESTS "Build portable protocol tests" OFF)

if(SB_BUILD_APP)
  if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSROOT MATCHES "wearable-4\\.0-device\\.core/?$")
    message(FATAL_ERROR
      "Gear S3 requires the Wearable 4.0 ARM SDK/rootstrap. The current VS Code "
      "Baseline SDK (8.0-10.0) cannot replace it. See SETUP.md and run "
      "python3 tools/check_sdk.py. For portable tests only, use "
      "-DSB_BUILD_APP=OFF -DSB_BUILD_TESTS=ON.")
  endif()
  include(FindPkgConfig)
  pkg_check_modules(TIZEN REQUIRED
  capi-appfw-application
  capi-system-sensor
  capi-network-bluetooth
  capi-system-info
  capi-privacy-privilege-manager
  elementary
  ecore
  dlog
  )

include_directories(${TIZEN_INCLUDE_DIRS} src)
link_directories(${TIZEN_LIBRARY_DIRS})
add_definitions(${TIZEN_CFLAGS_OTHER})
add_definitions(-std=gnu99)
add_executable(gear_s3_sensor_bridge
  src/main.c
  src/ui.c
  src/sensors.c
  src/ble_server.c
  src/protocol.c
)
target_link_libraries(gear_s3_sensor_bridge ${TIZEN_LIBRARIES} pthread m)
install(TARGETS gear_s3_sensor_bridge DESTINATION bin)
install(FILES tizen-manifest.xml DESTINATION .)
endif()

if(SB_BUILD_TESTS)
enable_testing()
add_executable(protocol_test tests/protocol_test.c src/protocol.c)
target_include_directories(protocol_test PRIVATE src)
target_link_libraries(protocol_test m)
add_test(NAME protocol_test COMMAND protocol_test)
endif()
