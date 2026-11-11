# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Get External projects here before importing the SDK
Include(FetchContent)

# TinyUSB 0.20.0
FetchContent_Declare(
  tinyusb
  GIT_REPOSITORY https://github.com/hathach/tinyusb
  GIT_TAG        3af1bec1a9161ee8dec29487831f7ac7ade9e189
)
FetchContent_MakeAvailable(tinyusb)

SET(PICO_TINYUSB_PATH "${tinyusb_SOURCE_DIR}")


SET(CMAKE_CXX_FLAGS_RELEASE "-O2")
SET(CMAKE_C_FLAGS_RELEASE "-O2")

# SET(LOG 2)

include(pico_sdk_import.cmake)

project(pico_asha VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_AUDIO_ENC_STATS "Enable audio downsampling and encoding statistics" OFF)
option(ENABLE_L2CAP_TX_TRACE "Emit per-SDU L2CAP transmit handshake diagnostics" OFF)

# Firmware variants for the Raspberry Pi Pico 2 W. Each one is its own executable:
#   pico_asha          ASHA only. The only variant which streams audio.
#   pico_asha_mfi      MFi only. Diagnostic probe, no audio.
#   pico_asha_classic  Bluetooth Classic only. Diagnostic probe, no audio.
# To build only some of them: -DPICO_ASHA_VARIANTS="asha;mfi"
set(PICO_ASHA_VARIANTS "asha;mfi;classic" CACHE STRING "Firmware variants to build (any of: asha;mfi;classic)")

pico_sdk_init()

message("Pico SDK version is ${PICO_SDK_VERSION_STRING}")
if(PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
  message( FATAL_ERROR "Pico SDK version must be 2.2.0 or greater" )
endif()

FetchContent_Declare(
  etl
  GIT_REPOSITORY https://github.com/ETLCPP/etl
  GIT_TAG        20.38.17
)
FetchContent_MakeAvailable(etl)

set(ASHA_SRC
  src/main.cpp
  src/runtime_settings.cpp
  src/asha_audio.c
  src/asha_fir.c
  src/asha_bt.cpp
  src/asha_comms.cpp
  src/asha_led.cpp
  src/gatt_server.cpp
  src/hearing_aid.cpp
  src/usb_descriptors.cpp
  src/usb_audio.cpp
  src/util.cpp
  lib/g722/g722_encode.cc
  lib/nanocobs/cobs.c
)

# gatt_server.cpp builds its ATT database at run time with att_db_util.
# Recent Pico SDKs already compile att_db_util.c as part of pico_btstack_ble;
# add it only if it is not there.
if (NOT PICO_BTSTACK_PATH)
  set(PICO_BTSTACK_PATH "${PICO_SDK_PATH}/lib/btstack")
endif()
get_target_property(PICO_ASHA_BLE_SRCS pico_btstack_ble INTERFACE_SOURCES)
if (NOT PICO_ASHA_BLE_SRCS MATCHES "att_db_util\\.c")
  if (EXISTS "${PICO_BTSTACK_PATH}/src/ble/att_db_util.c")
    message(STATUS "Adding att_db_util.c to the firmware sources")
    list(APPEND ASHA_SRC "${PICO_BTSTACK_PATH}/src/ble/att_db_util.c")
  else()
    message(FATAL_ERROR "att_db_util.c not found in ${PICO_BTSTACK_PATH}/src/ble")
  endif()
endif()

# -Wall only: -Werror is not used so that a new compiler warning does not stop the build
set_source_files_properties(${ASHA_SRC} src/classic_bt.cpp PROPERTIES COMPILE_OPTIONS "-Wall")

if (ENABLE_AUDIO_ENC_STATS)
  list(APPEND pico_asha_defines "PICO_ASHA_ENC_STATS=1")
endif(ENABLE_AUDIO_ENC_STATS)

if (ENABLE_L2CAP_TX_TRACE)
  list(APPEND pico_asha_defines "PICO_ASHA_L2CAP_TX_TRACE=1")
endif(ENABLE_L2CAP_TX_TRACE)

list(APPEND pico_asha_defines "PICO_ASHA_FW_VERS_MAJOR=${CMAKE_PROJECT_VERSION_MAJOR}")
list(APPEND pico_asha_defines "PICO_ASHA_FW_VERS_MINOR=${CMAKE_PROJECT_VERSION_MINOR}")
list(APPEND pico_asha_defines "PICO_ASHA_FW_VERS_PATCH=${CMAKE_PROJECT_VERSION_PATCH}")

list(APPEND pico_asha_defines "PICO_FLASH_BANK_TOTAL_SIZE=(FLASH_SECTOR_SIZE * 8u)")
list(APPEND pico_asha_defines "PICO_USE_FASTEST_SUPPORTED_CLOCK=1")

list(APPEND pico_asha_defines "CYW43_RESOURCE_ATTRIBUTE=__attribute__((aligned(4))) __in_flash(\"cyw43_firmware\")")

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wdeprecated-enum-enum-conversion HAVE_DEP_ENNUM_ENUM_CONV)

# add_pico_asha_variant(<target> <variant definition> [extra sources...])
function(add_pico_asha_variant target variant_define)
  add_executable(${target} ${ASHA_SRC} ${ARGN})
  pico_set_binary_type(${target} copy_to_ram)

  target_include_directories(${target} PUBLIC src/ lib/ include/)

  # list(APPEND pico_asha_defines "PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1")
  # CDC is owned directly by the core-0 communication transport. Enabling USB
  # stdio would add a second CDC reader/writer and route protocol frames to UART.
  pico_enable_stdio_usb(${target} 0)
  pico_enable_stdio_uart(${target} 1)

  if (HAVE_DEP_ENNUM_ENUM_CONV)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)
  endif (HAVE_DEP_ENNUM_ENUM_CONV)

  target_compile_definitions(${target} PUBLIC ${pico_asha_defines} ${variant_define})

  pico_add_extra_outputs(${target})

  target_link_libraries(${target}
    pico_stdlib
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
    pico_multicore
    tinyusb_device
    tinyusb_board
    etl::etl
  )

  target_link_options(${target} PUBLIC -Wl,--print-memory-usage)
endfunction()

if ("asha" IN_LIST PICO_ASHA_VARIANTS)
  add_pico_asha_variant(pico_asha PICO_ASHA_VARIANT_ASHA=1)
endif()

if ("mfi" IN_LIST PICO_ASHA_VARIANTS)
  add_pico_asha_variant(pico_asha_mfi PICO_ASHA_VARIANT_MFI=1)
endif()

if ("classic" IN_LIST PICO_ASHA_VARIANTS)
  if (TARGET pico_btstack_classic)
    add_pico_asha_variant(pico_asha_classic PICO_ASHA_VARIANT_CLASSIC=1 src/classic_bt.cpp)
    target_link_libraries(pico_asha_classic pico_btstack_classic)
  else()
    message(WARNING "pico_btstack_classic not found in this Pico SDK: the Classic variant is not built")
  endif()
endif()
