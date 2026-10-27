/**
 * @file mago_types.h
 * @brief Mago41 - Tipos de datos fundamentales y utilidades
 *
 * Diferencias clave vs RS41HUP_V2:
 * - Sin clases C++, solo structs + function pointers
 * - Memory pools estáticos, zero malloc
 * - Message bus tipado para comunicación entre módulos
 * - Error handling con result types, no exceptions
 * - Time en microsegundos (uint64) no milisegundos
 */

#ifndef MAGO_TYPES_H
#define MAGO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ============================================================================
 * COMPILE-TIME UTILITIES
 * ============================================================================ */

#define MAGO_ARRAY_SIZE(arr)    (sizeof(arr) / sizeof((arr)[0]))
#define MAGO_OFFSET_OF(type, member)  ((size_t)&(((type*)0)->member))
#define MAGO_CONTAINER_OF(ptr, type, member) \
    ((type*)((char*)(ptr) - MAGO_OFFSET_OF(type, member)))

#define MAGO_ALIGN_UP(x, align)  (((x) + (align) - 1) & ~((align) - 1))
#define MAGO_ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

#define MAGO_MIN(a, b)  ((a) < (b) ? (a) : (b))
#define MAGO_MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MAGO_CLAMP(x, lo, hi)  MAGO_MAX(lo, MAGO_MIN(x, hi))

/* ============================================================================
 * TIME SYSTEM - Microsecond precision
 * ============================================================================ */

typedef uint64_t mago_time_us_t;
typedef uint32_t mago_time_ms_t;

#define MAGO_TIME_US_MAX  UINT64_MAX
#define MAGO_TIME_MS_MAX  UINT32_MAX

#define MAGO_MS_TO_US(ms)   ((mago_time_us_t)(ms) * 1000ULL)
#define MAGO_US_TO_MS(us)   ((mago_time_ms_t)((us) / 1000ULL))
#define MAGO_SEC_TO_US(s)   ((mago_time_us_t)(s) * 1000000ULL)
#define MAGO_HZ_TO_US(hz)   (1000000ULL / (hz))

/* ============================================================================
 * ERROR HANDLING - Result types (no exceptions)
 * ============================================================================ */

typedef enum {
    MAGO_OK = 0,
    MAGO_ERR_NOMEM = -1,
    MAGO_ERR_INVAL = -2,
    MAGO_ERR_BUSY = -3,
    MAGO_ERR_TIMEOUT = -4,
    MAGO_ERR_NOT_FOUND = -5,
    MAGO_ERR_PERM = -6,
    MAGO_ERR_IO = -7,
    MAGO_ERR_CRC = -8,
    MAGO_ERR_PROTO = -9,
    MAGO_ERR_STATE = -10,
    MAGO_ERR_HW = -11,
    MAGO_ERR_NOSUPPORT = -12,
} mago_result_t;

#define MAGO_SUCCEEDED(r)  ((r) == MAGO_OK)
#define MAGO_FAILED(r)     ((r) != MAGO_OK)

/* Result with value - for functions that return data */
typedef struct {
    mago_result_t err;
    union {
        uint32_t u32;
        int32_t i32;
        uint64_t u64;
        void* ptr;
    } value;
} mago_result_u32_t;

static inline mago_result_u32_t mago_ok_u32(uint32_t v) {
    return (mago_result_u32_t){.err = MAGO_OK, .value.u32 = v};
}
static inline mago_result_u32_t mago_err(mago_result_t e) {
    return (mago_result_u32_t){.err = e, .value.u32 = 0};
}

/* ============================================================================
 * MESSAGE BUS - Event-driven communication
 * ============================================================================ */

typedef enum {
    MAGO_EVT_NONE = 0,

    // System events
    MAGO_EVT_BOOT,
    MAGO_EVT_SHUTDOWN,
    MAGO_EVT_SLEEP_REQUEST,
    MAGO_EVT_WAKEUP,
    MAGO_EVT_WATCHDOG,
    MAGO_EVT_LOW_BATTERY,
    MAGO_EVT_CRITICAL_BATTERY,

    // GPS events
    MAGO_EVT_GPS_FIX,
    MAGO_EVT_GPS_LOST,
    MAGO_EVT_GPS_RAW,
    MAGO_EVT_GPS_TIME_SYNC,

    // Radio events
    MAGO_EVT_RADIO_TX_DONE,
    MAGO_EVT_RADIO_TX_FAILED,
    MAGO_EVT_RADIO_RX_DONE,
    MAGO_EVT_RADIO_ERROR,

    // Protocol events
    MAGO_EVT_TX_REQUEST,
    MAGO_EVT_TX_COMPLETE,
    MAGO_EVT_RX_PACKET,
    MAGO_EVT_PROTOCOL_ERROR,

    // Config events
    MAGO_EVT_CONFIG_CHANGED,
    MAGO_EVT_CONFIG_SAVE_REQUEST,

    // User events
    MAGO_EVT_BUTTON_PRESS,
    MAGO_EVT_BUTTON_LONG,

    MAGO_EVT_COUNT
} mago_event_id_t;

typedef struct {
    mago_event_id_t id;
    mago_time_us_t timestamp;
    uint16_t data_len;
    union {
        struct { float lat, lon, alt; uint8_t sats; } gps_fix;
        struct { uint8_t protocol; uint16_t len; } tx_done;
        struct { uint8_t* data; uint16_t len; int8_t rssi; } rx_packet;
        struct { uint16_t mv; int16_t temp; } battery;
        struct { uint32_t code; const char* msg; } error;
        uint8_t raw[32];
    } payload;
} mago_event_t;

/* Event subscription */
typedef void (*mago_event_cb_t)(const mago_event_t* evt, void* ctx);

typedef struct {
    mago_event_id_t id;
    mago_event_cb_t cb;
    void* ctx;
    bool active;
} mago_event_sub_t;

/* ============================================================================
 * MEMORY POOLS - Static allocation only
 * ============================================================================ */

typedef struct {
    uint8_t* buffer;
    size_t block_size;
    size_t block_count;
    uint8_t* free_list;
    size_t free_count;
    bool initialized;
} mago_pool_t;

void mago_pool_init(mago_pool_t* pool, void* buffer, size_t block_size, size_t block_count);
void* mago_pool_alloc(mago_pool_t* pool);
void mago_pool_free(mago_pool_t* pool, void* ptr);
size_t mago_pool_free_count(const mago_pool_t* pool);

/* ============================================================================
 * CIRCULAR BUFFER - Lock-free SPSC
 * ============================================================================ */

typedef struct {
    uint8_t* buffer;
    size_t capacity;
    volatile size_t head;
    volatile size_t tail;
} mago_ring_t;

bool mago_ring_init(mago_ring_t* r, void* buf, size_t cap);
bool mago_ring_push(mago_ring_t* r, const void* data, size_t len);
bool mago_ring_pop(mago_ring_t* r, void* data, size_t len);
size_t mago_ring_used(const mago_ring_t* r);
size_t mago_ring_free(const mago_ring_t* r);
void mago_ring_clear(mago_ring_t* r);

/* ============================================================================
 * GPS DATA - Packed for telemetry
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    int32_t lat_deg7;       // *10^7 degrees
    int32_t lon_deg7;       // *10^7 degrees
    int32_t alt_mm;         // millimeters
    int32_t height_mm;      // height above ellipsoid
    int32_t vel_n_mm_s;     // NED velocity mm/s
    int32_t vel_e_mm_s;
    int32_t vel_d_mm_s;
    uint32_t g_speed_mm_s;  // Ground speed
    uint32_t heading_deg5;  // *10^5 degrees
    uint16_t pdop;          // *100
    uint16_t hdop;
    uint16_t vdop;
    uint8_t fix_type;       // 0=none, 1=dead, 2=2D, 3=3D
    uint8_t flags;          // bit0=gnss, bit1=diff, bit2=rel
    uint8_t sats_used;
    uint8_t sats_tracked;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint32_t itow_ms;       // GPS time of week
    mago_time_us_t local_us; // Local timestamp
} mago_gps_nav_t;

/* ============================================================================
 * TELEMETRY FRAME - Binary, versioned
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint16_t magic;         // 0x4D47 'MG'
    uint8_t version;        // Protocol version
    uint8_t type;           // Frame type
    uint32_t seq;           // Sequence number
    mago_time_us_t timestamp;

    // GPS
    int32_t lat, lon, alt;
    int32_t vel_n, vel_e, vel_d;
    uint16_t hdop;
    uint8_t sats;
    uint8_t fix;

    // Sensors
    uint16_t batt_mv;
    int16_t temp_c;
    uint16_t solar_mv;
    uint16_t pressure_pa;   // *10 Pa

    // Status
    uint16_t error_flags;
    uint8_t tx_power;
    uint8_t rssi;
    uint8_t reserved[3];

    uint16_t crc16;
} mago_telemetry_frame_t;

/* ============================================================================
 * HORUS PACKETS - Binary formats
 * ============================================================================ */

typedef struct __attribute__((packed)) {
    uint16_t callsign_id;
    uint8_t seq;
    int32_t lat, lon;
    int16_t alt;
    uint8_t speed;
    uint8_t dir;
    int8_t temp;
    uint8_t batt;      // *20mV
    uint8_t solar;     // *50mV
    uint8_t flags;
} mago_horus_v1_t;

typedef struct __attribute__((packed)) {
    uint16_t callsign_id;
    uint8_t seq;
    uint8_t pkt_type;
    uint8_t payload[48];
    uint8_t payload_len;
    uint16_t crc;
} mago_horus_v2_t;

/* ============================================================================
 * COMPONENT INTERFACE - Plugin architecture
 * ============================================================================ */

typedef struct mago_component mago_component_t;

typedef mago_result_t (*mago_comp_init_fn)(mago_component_t* c, const void* cfg);
typedef mago_result_t (*mago_comp_start_fn)(mago_component_t* c);
typedef mago_result_t (*mago_comp_stop_fn)(mago_component_t* c);
typedef mago_result_t (*mago_comp_tick_fn)(mago_component_t* c, mago_time_us_t now);
typedef mago_result_t (*mago_comp_event_fn)(mago_component_t* c, const mago_event_t* evt);
typedef void (*mago_comp_deinit_fn)(mago_component_t* c);

struct mago_component {
    const char* name;
    uint8_t priority;           // Init order
    void* private_data;         // Opaque
    size_t private_size;        // For static allocation

    // Lifecycle
    mago_comp_init_fn init;
    mago_comp_start_fn start;
    mago_comp_tick_fn tick;
    mago_comp_event_fn event;
    mago_comp_stop_fn stop;
    mago_comp_deinit_fn deinit;

    // Runtime state
    bool initialized;
    bool running;
    mago_result_t last_error;
};

/* ============================================================================
 * PROTOCOL VTABLE - Truly pluggable
 * ============================================================================ */

typedef struct mago_protocol mago_protocol_t;

typedef mago_result_t (*mago_proto_encode_fn)(const mago_protocol_t* p,
    const mago_gps_nav_t* gps, const mago_telemetry_frame_t* telem,
    uint8_t* out, size_t* out_len);
typedef mago_result_t (*mago_proto_decode_fn)(const mago_protocol_t* p,
    const uint8_t* data, size_t len, mago_telemetry_frame_t* out);
typedef mago_result_t (*mago_proto_tx_fn)(const mago_protocol_t* p,
    const uint8_t* data, size_t len);
typedef void (*mago_proto_rx_fn)(const mago_protocol_t* p, uint8_t symbol);

struct mago_protocol {
    mago_protocol_id_t id;
    const char* name;
    const void* params;         // Protocol-specific config

    // Encoding/Decoding
    mago_proto_encode_fn encode;
    mago_proto_decode_fn decode;

    // Radio interface
    mago_proto_tx_fn transmit;
    mago_proto_rx_fn receive_symbol;

    // Timing
    uint32_t symbol_period_us;
    uint32_t preamble_us;
    uint32_t guard_us;

    // State
    bool tx_active;
    uint32_t tx_bytes_remaining;
};

/* ============================================================================
 * RADIO ABSTRACTION
 * ============================================================================ */

typedef enum {
    MAGO_RADIO_MODE_SLEEP = 0,
    MAGO_RADIO_MODE_STANDBY,
    MAGO_RADIO_MODE_SYNTH,
    MAGO_RADIO_MODE_RX,
    MAGO_RADIO_MODE_TX,
} mago_radio_mode_t;

typedef struct {
    void (*set_mode)(mago_radio_mode_t mode);
    void (*set_freq)(float mhz);
    void (*set_power)(int8_t dbm);
    void (*set_modulation)(uint8_t type, uint16_t deviation_hz, uint32_t baud);
    void (*write_fifo)(const uint8_t* data, size_t len);
    size_t (*read_fifo)(uint8_t* data, size_t max_len);
    void (*start_tx)(void);
    void (*start_rx)(void);
    int16_t (*get_rssi)(void);
    bool (*is_tx_done)(void);
    bool (*is_rx_ready)(void);
} mago_radio_ops_t;

/* ============================================================================
 * UTILITIES
 * ============================================================================ */

// CRC16-CCITT (XModem)
uint16_t mago_crc16_ccitt(const uint8_t* data, size_t len, uint16_t init);

// CRC32 (Ethernet polynomial)
uint32_t mago_crc32(const uint8_t* data, size_t len, uint32_t init);

// FNV-1a hash
uint32_t mago_fnv1a(const uint8_t* data, size_t len);

// Endian helpers
static inline uint16_t mago_htobe16(uint16_t v) { return ((v >> 8) & 0xFF) | ((v << 8) & 0xFF00); }
static inline uint32_t mago_htobe32(uint32_t v) { return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000); }
static inline uint16_t mago_beto16(uint16_t v) { return mago_htobe16(v); }
static inline uint32_t mago_beto32(uint32_t v) { return mago_htobe32(v); }

// Bit manipulation
static inline bool mago_bit_test(uint32_t v, uint8_t b) { return (v >> b) & 1; }
static inline uint32_t mago_bit_set(uint32_t v, uint8_t b) { return v | (1u << b); }
static inline uint32_t mago_bit_clear(uint32_t v, uint8_t b) { return v & ~(1u << b); }
static inline uint32_t mago_bit_toggle(uint32_t v, uint8_t b) { return v ^ (1u << b); }

#endif // MAGO_TYPES_H