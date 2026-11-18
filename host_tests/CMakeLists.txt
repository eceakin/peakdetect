cmake_minimum_required(VERSION 3.16)
project(peakdetect_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Unity test kütüphanesi (GitHub'dan otomatik indirilir) ---
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
)
FetchContent_MakeAvailable(unity)

# --- Firmware'in core bileşeni, ESP-IDF olmadan ---
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components/core)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${CORE_DIR}/*.c)

add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core PUBLIC ${CORE_DIR}/include)
target_compile_options(core PRIVATE -Wall -Wextra)

# --- Testler ---
enable_testing()

# Yeni bir test eklemek için: pd_add_test(test_xxx)
function(pd_add_test name)
    add_executable(${name} ${name}.c ${ARGN})
    target_link_libraries(${name} PRIVATE core unity)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pd_add_test(test_sanity)
pd_add_test(test_frame_codec)
pd_add_test(test_crc16)
pd_add_test(test_imu_convert)
pd_add_test(test_sensitivity)
pd_add_test(test_gravity_filter)
pd_add_test(test_gesture_classifier)
pd_add_test(test_gesture_detector signals.c)
target_link_libraries(test_gesture_detector PRIVATE m)