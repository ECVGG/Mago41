# Mago41 - CMake Build System v2
# Arquitectura event-driven, component-based, protocol-pluggable
# Totalmente diferente de RS41HUP_V2 (Arduino/C++ monolítico)

cmake_minimum_required(VERSION 3.16)
project(Mago41 VERSION 1.0.0 LANGUAGES C ASM)

# =============================================================================
# PROJECT CONFIGURATION
# =============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build types
set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -gdwarf-4 -DMAGO_DEBUG=1")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -DMAGO_RELEASE=1")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG -flto -DMAGO_MINSIZE=1")

# Warnings - strict
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter -Wno-unused-variable")

# =============================================================================
# ARCHITECTURE OPTIONS
# =============================================================================

option(MCU_STM32F100 "Target MCU STM32F100 (24MHz)" ON)
option(MCU_STM32F103 "Target MCU STM32F103 (72MHz)" OFF)
option(HAL_BACKEND_STM32 "Use STM32Cube HAL" OFF)
option(HAL_BACKEND_LIBOPENCM3 "Use libopencm3" ON)
option(HAL_BACKEND_BAREMETAL "Use bare metal register access" OFF)
option(HAL_BACKEND_ZEPHYR "Use Zephyr RTOS" OFF)

option(ENABLE_RTTY "Enable RTTY protocol" ON)
option(ENABLE_MFSK "Enable MFSK/Horus protocols" ON)
option(ENABLE_APRS "Enable APRS protocol" ON)
option(ENABLE_MORSE "Enable Morse CW protocol" ON)
option(ENABLE_GPS "Enable GPS module" ON)
option(ENABLE_DEBUG "Enable debug output" ON)
option(ENABLE_TESTS "Build unit tests" OFF)
option(ENABLE_CLI "Enable CLI shell" OFF)

# Validate MCU
if(MCU_STM32F100 AND MCU_STM32F103)
    message(FATAL_ERROR "Select only one MCU")
endif()

# Validate HAL
set(hal_count 0)
foreach(h HAL_BACKEND_STM32 HAL_BACKEND_LIBOPENCM3 HAL_BACKEND_BAREMETAL HAL_BACKEND_ZEPHYR)
    if(${h}) math(EXPR hal_count "${hal_count} + 1") endif()
endforeach()
if(hal_count GREATER 1)
    message(FATAL_ERROR "Select only one HAL backend")
endif()
if(hal_count EQUAL 0)
    set(HAL_BACKEND_LIBOPENCM3 ON)
endif()

# =============================================================================
# MCU DEFINITIONS
# =============================================================================

if(MCU_STM32F100)
    set(MCU_FAMILY "STM32F100xB")
    set(LD_SCRIPT "${CMAKE_SOURCE_DIR}/ld/stm32f100xb.ld")
    set(SYSTEM_CORE_CLOCK 24000000)
    add_compile_definitions(STM32F100xB STM32F1)
elseif(MCU_STM32F103)
    set(MCU_FAMILY "STM32F103xB")
    set(LD_SCRIPT "${CMAKE_SOURCE_DIR}/ld/stm32f103xb.ld")
    set(SYSTEM_CORE_CLOCK 72000000)
    add_compile_definitions(STM32F103xB STM32F1)
endif()

# HAL backend defines
if(HAL_BACKEND_STM32)
    add_compile_definitions(HAL_BACKEND_STM32=1)
    find_package(STM32CubeHAL REQUIRED)
elseif(HAL_BACKEND_LIBOPENCM3)
    add_compile_definitions(HAL_BACKEND_LIBOPENCM3=1)
    find_package(libopencm3 REQUIRED)
elseif(HAL_BACKEND_BAREMETAL)
    add_compile_definitions(HAL_BACKEND_BAREMETAL=1)
elseif(HAL_BACKEND_ZEPHYR)
    add_compile_definitions(HAL_BACKEND_ZEPHYR=1)
    find_package(Zephyr REQUIRED)
endif()

# Feature defines
if(ENABLE_RTTY) add_compile_definitions(MAGO_ENABLE_RTTY=1) endif()
if(ENABLE_MFSK) add_compile_definitions(MAGO_ENABLE_MFSK=1) endif()
if(ENABLE_APRS) add_compile_definitions(MAGO_ENABLE_APRS=1) endif()
if(ENABLE_MORSE) add_compile_definitions(MAGO_ENABLE_MORSE=1) endif()
if(ENABLE_GPS) add_compile_definitions(MAGO_ENABLE_GPS=1) endif()
if(ENABLE_DEBUG) add_compile_definitions(MAGO_ENABLE_DEBUG=1) endif()

add_compile_definitions(
    SYSTEM_CORE_CLOCK=${SYSTEM_CORE_CLOCK}
    MAGO_VERSION_MAJOR=1
    MAGO_VERSION_MINOR=0
    MAGO_VERSION_PATCH=0
)

# =============================================================================
# SOURCE FILES - NEW ARCHITECTURE
# =============================================================================

# Core
set(CORE_SOURCES
    Core/Src/main.c
    Core/Src/mago_app.c
    Core/Src/mago_hal.c
    Core/Src/mago_eventbus.c
    Core/Src/mago_component.c
    Core/Src/mago_config.c
)

set(CORE_HEADERS
    Core/Inc/mago_config.h
    Core/Inc/mago_types.h
    Core/Inc/mago_hal.h
    Core/Inc/mago_app.h
    Core/Inc/mago_eventbus.h
    Core/Inc/mago_component.h
)

# Protocol Framework
set(PROTOCOL_FRAMEWORK_SOURCES
    Modules/Protocol/Src/mago_protocol.c
)

set(PROTOCOL_FRAMEWORK_HEADERS
    Modules/Protocol/Inc/mago_protocol.h
)

# Protocol Implementations
set(PROTOCOL_IMPL_SOURCES
    Modules/Protocol/Src/mago_rtty.c
    Modules/Protocol/Src/mago_mfsk.c
    Modules/Protocol/Src/mago_aprs.c
    Modules/Protocol/Src/mago_morse.c
)

set(PROTOCOL_IMPL_HEADERS
    Modules/Protocol/Inc/mago_rtty.h
    Modules/Protocol/Inc/mago_mfsk.h
    Modules/Protocol/Inc/mago_aprs.h
    Modules/Protocol/Inc/mago_morse.h
)

# Hardware Modules
set(RADIO_SOURCES
    Modules/Radio/Src/mago_radio.c
)
set(RADIO_HEADERS
    Modules/Radio/Inc/mago_radio.h
)

set(GPS_SOURCES
    Modules/GPS/Src/mago_gps.c
)
set(GPS_HEADERS
    Modules/GPS/Inc/mago_gps.h
)

set(POWER_SOURCES
    Modules/Power/Src/mago_power.c
)
set(POWER_HEADERS
    Modules/Power/Inc/mago_power.h
)

# Startup
set(STARTUP_FILE "${CMAKE_SOURCE_DIR}/startup/startup_${MCU_FAMILY}.s")

# =============================================================================
# INCLUDE DIRECTORIES
# =============================================================================

include_directories(
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Modules/Radio/Inc
    ${CMAKE_SOURCE_DIR}/Modules/GPS/Inc
    ${CMAKE_SOURCE_DIR}/Modules/Protocol/Inc
    ${CMAKE_SOURCE_DIR}/Modules/Power/Inc
)

if(HAL_BACKEND_STM32)
    include_directories(${STM32CUBE_HAL_INCLUDE_DIRS})
elseif(HAL_BACKEND_LIBOPENCM3)
    include_directories(${LIBOPENCM3_INCLUDE_DIRS})
elseif(HAL_BACKEND_ZEPHYR)
    include_directories(${ZEPHYR_INCLUDE_DIRS})
endif()

# =============================================================================
# LINKER SCRIPT
# =============================================================================

if(NOT EXISTS ${LD_SCRIPT})
    message(WARNING "Linker script not found: ${LD_SCRIPT}")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${LD_SCRIPT} -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map")

# =============================================================================
# MAIN EXECUTABLE
# =============================================================================

add_executable(${PROJECT_NAME}.elf
    ${STARTUP_FILE}
    ${CORE_SOURCES}
    ${PROTOCOL_FRAMEWORK_SOURCES}
    ${PROTOCOL_IMPL_SOURCES}
    ${RADIO_SOURCES}
    ${GPS_SOURCES}
    ${POWER_SOURCES}
)

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fmessage-length=0
    -fsigned-char
    -fno-exceptions
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fno-rtti
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -specs=nano.specs
    -specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# Link libraries
if(HAL_BACKEND_STM32)
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${STM32CUBE_HAL_LIBRARIES})
elseif(HAL_BACKEND_LIBOPENCM3)
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${LIBOPENCM3_LIBRARIES})
elseif(HAL_BACKEND_ZEPHYR)
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${ZEPHYR_LIBRARIES})
endif()

target_link_libraries(${PROJECT_NAME}.elf PRIVATE c m gcc)

# =============================================================================
# OUTPUT FORMATS
# =============================================================================

add_custom_command(
    TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJDUMP} -S $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.lst
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Generating hex/bin/lst and size report"
)

# =============================================================================
# CUSTOM TARGETS
# =============================================================================

add_custom_target(flash
    COMMAND openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program ${PROJECT_NAME}.elf verify reset exit"
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing via OpenOCD"
)

add_custom_target(debug
    COMMAND gdb-multiarch -ex "target extended-remote localhost:3333" -ex "load" ${PROJECT_NAME}.elf
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Starting GDB debug session"
)

add_custom_target(size
    COMMAND ${CMAKE_SIZE} -A -d $<TARGET_FILE:${PROJECT_NAME}.elf>
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Detailed size report"
)

add_custom_target(clean_generated
    COMMAND rm -f *.hex *.bin *.lst *.map
    COMMENT "Cleaning generated files"
)

# Unit tests
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# LINKER SCRIPT GENERATION
# =============================================================================

configure_file(
    ${CMAKE_SOURCE_DIR}/ld/stm32f1_template.ld.in
    ${LD_SCRIPT}
    @ONLY
)

# =============================================================================
# INSTALL & PACKAGING
# =============================================================================

install(TARGETS ${PROJECT_NAME}.elf RUNTIME DESTINATION bin)
install(FILES ${PROJECT_NAME}.hex ${PROJECT_NAME}.bin DESTINATION bin)

include(CPack)
set(CPACK_PACKAGE_NAME "Mago41")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")