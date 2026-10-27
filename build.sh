#!/bin/bash
# Mago41 - Build Script
# Usage: ./build.sh [options]

set -e

# Default configuration
MCU="STM32F100"
HAL="libopencm3"
BUILD_TYPE="Release"
ENABLE_TESTS="OFF"
ENABLE_CLI="OFF"
PROTOCOLS="rtty mfsk"
CLEAN=false
FLASH=false
VERBOSE=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
    -m, --mcu <f100|f103>       Target MCU (default: f100)
    -h, --hal <libopencm3|stm32|baremetal|zephyr>  HAL backend (default: libopencm3)
    -t, --type <Debug|Release|MinSizeRel>  Build type (default: Release)
    -p, --protocols <list>      Protocols to enable (default: rtty mfsk)
                                Available: rtty mfsk aprs morse
    --enable-tests              Build unit tests
    --enable-cli                Enable CLI shell
    -c, --clean                 Clean build directory first
    -f, --flash                 Flash after build (requires OpenOCD)
    -v, --verbose               Verbose output
    --help                      Show this help

Examples:
    $0                          # Default build
    $0 -m f103 -t Debug         # STM32F103 debug build
    $0 -p "rtty mfsk aprs" -f   # All protocols + flash
    $0 -c                       # Clean build
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mcu)
            MCU="$2"
            shift 2
            ;;
        -h|--hal)
            HAL="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -p|--protocols)
            PROTOCOLS="$2"
            shift 2
            ;;
        --enable-tests)
            ENABLE_TESTS="ON"
            shift
            ;;
        --enable-cli)
            ENABLE_CLI="ON"
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -f|--flash)
            FLASH=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

# Validate MCU
if [[ "$MCU" != "f100" && "$MCU" != "f103" ]]; then
    echo -e "${RED}Invalid MCU: $MCU (must be f100 or f103)${NC}"
    exit 1
fi

# Validate HAL
if [[ "$HAL" != "libopencm3" && "$HAL" != "stm32" && "$HAL" != "baremetal" && "$HAL" != "zephyr" ]]; then
    echo -e "${RED}Invalid HAL: $HAL${NC}"
    exit 1
fi

# Build directory
BUILD_DIR="build_${MCU}_${HAL}_${BUILD_TYPE}"

echo -e "${GREEN}=== Mago41 Build ===${NC}"
echo "MCU:       $MCU"
echo "HAL:       $HAL"
echo "Type:      $BUILD_TYPE"
echo "Protocols: $PROTOCOLS"
echo "Tests:     $ENABLE_TESTS"
echo "CLI:       $ENABLE_CLI"
echo "Build dir: $BUILD_DIR"
echo ""

# Clean if requested
if [[ "$CLEAN" == true ]]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake options
CMAKE_OPTS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DMCU_STM32F100=$([[ "$MCU" == "f100" ]] && echo ON || echo OFF)
    -DMCU_STM32F103=$([[ "$MCU" == "f103" ]] && echo ON || echo OFF)
    -DHAL_BACKEND_LIBOPENCM3=$([[ "$HAL" == "libopencm3" ]] && echo ON || echo OFF)
    -DHAL_BACKEND_STM32=$([[ "$HAL" == "stm32" ]] && echo ON || echo OFF)
    -DHAL_BACKEND_BAREMETAL=$([[ "$HAL" == "baremetal" ]] && echo ON || echo OFF)
    -DHAL_BACKEND_ZEPHYR=$([[ "$HAL" == "zephyr" ]] && echo ON || echo OFF)
    -DENABLE_TESTS="$ENABLE_TESTS"
    -DENABLE_CLI="$ENABLE_CLI"
)

# Enable protocols
for p in rtty mfsk aprs morse; do
    if [[ " $PROTOCOLS " == *" $p "* ]]; then
        CMAKE_OPTS+=("-DENABLE_${p^^}=ON")
    else
        CMAKE_OPTS+=("-DENABLE_${p^^}=OFF")
    fi
done

# Configure
echo -e "${GREEN}Configuring...${NC}"
if [[ "$VERBOSE" == true ]]; then
    cmake "${CMAKE_OPTS[@]}" ..
else
    cmake "${CMAKE_OPTS[@]}" .. > cmake_configure.log 2>&1
fi

# Build
echo -e "${GREEN}Building...${NC}"
if [[ "$VERBOSE" == true ]]; then
    make -j$(nproc)
else
    make -j$(nproc) > build.log 2>&1
fi

# Check results
if [[ -f Mago41.elf ]]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    arm-none-eabi-size Mago41.elf
    echo ""
    ls -lh Mago41.elf Mago41.hex Mago41.bin 2>/dev/null || true
else
    echo -e "${RED}Build failed!${NC}"
    [[ "$VERBOSE" != true ]] && cat build.log
    exit 1
fi

# Flash if requested
if [[ "$FLASH" == true ]]; then
    echo -e "${GREEN}Flashing...${NC}"
    openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program Mago41.elf verify reset exit"
fi

echo -e "${GREEN}Done!${NC}"