# Mago41

Modular firmware for Vaisala RS41 radiosonde (STM32F100/F103 + SI4032 + u-blox GPS).

## Features

- **Pure C11** - No C++, no Arduino framework
- **Event-driven architecture** - Typed message bus with lock-free SPSC queues
- **Component/plugin system** - Explicit lifecycle (init→start→tick→stop→deinit)
- **Pluggable protocol framework** - Protocols register at runtime with independent encode/decode/tx/rx
- **Separated signal processing** - FEC (Golay 24,12), interleaving (block), scrambling (xorshift), symbol mapping (2FSK/4FSK/16FSK/OOK)
- **HAL with vtable interface** - Supports libopencm3, STM32Cube HAL, bare metal, Zephyr backends
- **Declarative configuration** - Struct-based with runtime validation, flash persistence
- **Zero dynamic allocation** - Static memory pools, circular buffers
- **Microsecond-precision time** - uint64_t timestamps, result-type error handling

## Supported Hardware

- **MCU**: STM32F100xB (24MHz) or STM32F103xB (72MHz)
- **Radio**: SI4032/SI4432 (434/868/915 MHz)
- **GPS**: u-blox MAX-6/7/8, M8, M9 (UBX protocol)
- **Battery**: 2x Li-SOCl2 AA (3.6V nominal)

## Protocols

| Protocol | Modulation | FEC | Use Case |
|----------|------------|-----|----------|
| RTTY | 2FSK (ITA2/Baudot) | None | Traditional tracking |
| Horus V1 | 4FSK | Golay(24,12) + Interleave | High-altitude balloon |
| Horus V2 | 4FSK | Golay(24,12) + Interleave + Scramble | Improved Horus |
| APRS | AFSK 1200/2200 | AX.25/NRZI | APRS network |
| Morse CW | OOK | None | Beacon/identification |

## Building

### Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- CMake 3.16+
- libopencm3 (default HAL backend)
- OpenOCD (for flashing)

### Configure & Build

```bash
# Default: STM32F100 + libopencm3 + RTTY + MFSK
mkdir build && cd build
cmake ..
make

# STM32F103 (72MHz)
cmake -DMCU_STM32F100=OFF -DMCU_STM32F103=ON ..

# STM32Cube HAL backend
cmake -DHAL_BACKEND_LIBOPENCM3=OFF -DHAL_BACKEND_STM32=ON ..

# Enable all protocols
cmake -DENABLE_APRS=ON -DENABLE_MORSE=ON ..

# Release build
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### Flash

```bash
make flash
```

### Outputs

- `Mago41.elf` - ELF with debug symbols
- `Mago41.hex` - Intel HEX for flashing
- `Mago41.bin` - Raw binary
- `Mago41.lst` - Disassembly listing
- `Mago41.map` - Linker map

## Project Structure

```
Mago41/
├── Core/
│   ├── Inc/           # Core headers
│   │   ├── mago_config.h      # Declarative configuration
│   │   ├── mago_types.h       # Base types, message bus, pools
│   │   ├── mago_hal.h         # HAL vtable interface
│   │   ├── mago_eventbus.h    # Event bus API
│   │   ├── mago_component.h   # Component system
│   │   └── mago_app.h         # Application state machine
│   └── Src/           # Core implementation
│       ├── main.c
│       ├── mago_app.c
│       ├── mago_hal.c
│       ├── mago_eventbus.c
│       ├── mago_component.c
│       └── mago_config.c
├── Modules/
│   ├── Radio/         # SI4032 driver
│   ├── GPS/           # u-blox UBX parser
│   ├── Power/         # Battery/power management
│   └── Protocol/      # Protocol framework + implementations
│       ├── Inc/
│       │   ├── mago_protocol.h    # Framework
│       │   ├── mago_rtty.h
│       │   ├── mago_mfsk.h
│       │   ├── mago_aprs.h
│       │   └── mago_morse.h
│       └── Src/
│           ├── mago_protocol.c    # FEC, interleave, scramble, mappers
│           ├── mago_rtty.c
│           ├── mago_mfsk.c
│           ├── mago_aprs.c
│           └── mago_morse.c
├── startup/           # Vector tables, Reset_Handler, SystemInit
├── ld/                # Linker script template
├── tests/             # Unit tests (Unity)
├── CMakeLists.txt
└── build.sh
```

## Architecture Overview

### Core Components

1. **Event Bus** (`mago_eventbus`) - Lock-free SPSC ring buffer, ISR-safe posting, subscription model
2. **Component Registry** (`mago_component`) - Priority-sorted lifecycle, static factories
3. **Application** (`mago_app`) - State machine (BOOT→INIT→RUNNING→SLEEP→SHUTDOWN)
4. **HAL** (`mago_hal`) - GPIO/SPI/UART/ADC/Timer/I2C/RTC/Flash/PWR/WDG/RNG vtables

### Protocol Pipeline

```
GPS + Telemetry → encode() → [FEC] → [Interleave] → [Scramble] → Mapper → Radio TX
                                                          ↓
Radio RX → Demapper → [Descramble] → [Deinterleave] → [FEC Decode] → decode()
```

Each protocol declares its pipeline modules at registration.

### Configuration

```c
mago_system_config_t cfg = {
    .device_name = "MAGO41",
    .radio = { .freq_mhz = 434.714, .power_dbm = 13 },
    .gps = { .baud_operational = 38400, .dynamic_model = MAGO_GPS_DYN_AIRBORNE_1G },
    .power = { .type = MAGO_PWR_BATT_LI_SOCL2_2S, .v_min_mv = 3000 },
    .protocols = {
        { .id = MAGO_PROTO_RTTY, .enabled = true, .interval_ms = 30000 },
        { .id = MAGO_PROTO_HORUS_V2, .enabled = true, .interval_ms = 60000 }
    }
};
mago_config_save(&cfg);  // Persists to flash with CRC32
```

## Development

### Adding a Protocol

1. Define params struct in `mago_protocol.h`
2. Implement `encode()` producing bit stream
3. Select pipeline modules (FEC, interleaver, scrambler, mapper)
4. Create factory: `mago_protocol_create_myproto()`
5. Register at startup: `mago_protocols_register(&reg, myproto)`

### Custom HAL Backend

Implement `mago_hal_t` with all vtables, set as default in `mago_hal.c`.

## License

MIT - see LICENSE file.

## References

- Vaisala RS41 datasheet
- Silicon Labs SI4032 datasheet
- u-blox UBX protocol specification
- Horus binary protocol specification