/**
 * @file mago_hal.h
 * @brief Mago41 - Hardware Abstraction Layer v2
 *
 * Diferencias clave vs RS41HUP_V2:
 * - Interface basado en function pointers (vtable), no herencia C++
 * - Múltiples backends: STM32 HAL, libopencm3, Zephyr, bare metal
 * - Configuración en runtime, no #defines
 * - DMA-first design para periféricos
 * - Power-aware (clock gating, sleep modes)
 * - Zero dynamic allocation
 */

#ifndef MAGO_HAL_H
#define MAGO_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mago_types.h"

/* ============================================================================
 * HAL BACKEND SELECTION
 * ============================================================================ */

typedef enum {
    MAGO_HAL_BACKEND_AUTO = 0,
    MAGO_HAL_BACKEND_STM32_HAL,
    MAGO_HAL_BACKEND_LIBOPENCM3,
    MAGO_HAL_BACKEND_ZEPHYR,
    MAGO_HAL_BACKEND_BAREMETAL,
} mago_hal_backend_t;

extern mago_hal_backend_t mago_hal_backend;

/* ============================================================================
 * GPIO - Pin-centric API
 * ============================================================================ */

typedef enum {
    MAGO_GPIO_INPUT = 0,
    MAGO_GPIO_OUTPUT_PP,
    MAGO_GPIO_OUTPUT_OD,
    MAGO_GPIO_AF_PP,
    MAGO_GPIO_AF_OD,
    MAGO_GPIO_ANALOG,
    MAGO_GPIO_IT_RISING,
    MAGO_GPIO_IT_FALLING,
    MAGO_GPIO_IT_BOTH,
} mago_gpio_mode_t;

typedef enum {
    MAGO_GPIO_PULL_NONE = 0,
    MAGO_GPIO_PULL_UP,
    MAGO_GPIO_PULL_DOWN,
} mago_gpio_pull_t;

typedef enum {
    MAGO_GPIO_SPEED_LOW = 0,
    MAGO_GPIO_SPEED_MED,
    MAGO_GPIO_SPEED_HIGH,
    MAGO_GPIO_SPEED_VHIGH,
} mago_gpio_speed_t;

typedef void (*mago_gpio_irq_cb_t)(void* ctx);

typedef struct {
    bool (*init)(const mago_pin_t* pin, mago_gpio_mode_t mode, mago_gpio_pull_t pull, mago_gpio_speed_t speed);
    void (*deinit)(const mago_pin_t* pin);
    void (*write)(const mago_pin_t* pin, bool value);
    bool (*read)(const mago_pin_t* pin);
    void (*toggle)(const mago_pin_t* pin);
    bool (*set_irq)(const mago_pin_t* pin, mago_gpio_mode_t irq_mode, mago_gpio_irq_cb_t cb, void* ctx);
    void (*clear_irq)(const mago_pin_t* pin);
} mago_gpio_ops_t;

/* ============================================================================
 * SPI - DMA-capable
 * ============================================================================ */

typedef enum {
    MAGO_SPI_MODE0 = 0,  // CPOL=0, CPHA=0
    MAGO_SPI_MODE1 = 1,  // CPOL=0, CPHA=1
    MAGO_SPI_MODE2 = 2,  // CPOL=1, CPHA=0
    MAGO_SPI_MODE3 = 3,  // CPOL=1, CPHA=1
} mago_spi_mode_t;

typedef struct {
    bool (*init)(uint32_t instance, uint32_t baudrate, mago_spi_mode_t mode, bool msb_first);
    void (*deinit)(uint32_t instance);
    mago_result_t (*transfer)(uint32_t instance, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms);
    mago_result_t (*transfer_dma)(uint32_t instance, const uint8_t* tx, uint8_t* rx, size_t len, void (*done_cb)(void* ctx), void* ctx);
    void (*cs_assert)(uint32_t instance);
    void (*cs_deassert)(uint32_t instance);
} mago_spi_ops_t;

/* ============================================================================
 * UART - Interrupt + DMA
 * ============================================================================ */

typedef enum {
    MAGO_UART_PARITY_NONE = 0,
    MAGO_UART_PARITY_EVEN,
    MAGO_UART_PARITY_ODD,
} mago_uart_parity_t;

typedef enum {
    MAGO_UART_STOP_1 = 0,
    MAGO_UART_STOP_2,
} mago_uart_stop_t;

typedef void (*mago_uart_rx_cb_t)(uint8_t byte, void* ctx);
typedef void (*mago_uart_tx_done_cb_t)(void* ctx);
typedef void (*mago_uart_error_cb_t)(uint32_t error, void* ctx);

typedef struct {
    bool (*init)(uint32_t instance, uint32_t baudrate, uint8_t databits, mago_uart_parity_t parity, mago_uart_stop_t stopbits);
    void (*deinit)(uint32_t instance);
    mago_result_t (*write)(uint32_t instance, const uint8_t* data, size_t len, uint32_t timeout_ms);
    mago_result_t (*write_dma)(uint32_t instance, const uint8_t* data, size_t len, mago_uart_tx_done_cb_t cb, void* ctx);
    void (*set_rx_callback)(uint32_t instance, mago_uart_rx_cb_t cb, void* ctx);
    void (*set_error_callback)(uint32_t instance, mago_uart_error_cb_t cb, void* ctx);
    bool (*is_tx_idle)(uint32_t instance);
    void (*flush)(uint32_t instance);
} mago_uart_ops_t;

/* ============================================================================
 * ADC - Multi-channel, DMA
 * ============================================================================ */

typedef struct {
    bool (*init)(const uint8_t* channels, size_t count, uint32_t sample_rate_hz);
    void (*deinit)(void);
    mago_result_t (*read)(uint8_t channel, uint16_t* value);
    mago_result_t (*read_dma)(uint16_t* buffer, size_t count, void (*done_cb)(void* ctx), void* ctx);
    void (*set_callback)(void (*done_cb)(void* ctx), void* ctx);
    float (*to_voltage)(uint16_t raw, uint8_t channel);
} mago_adc_ops_t;

/* ============================================================================
 * TIMER - Microsecond precision
 * ============================================================================ */

typedef void (*mago_timer_cb_t)(void* ctx);

typedef struct {
    bool (*init)(uint32_t instance, uint32_t freq_hz);
    void (*deinit)(uint32_t instance);
    void (*start)(uint32_t instance);
    void (*stop)(uint32_t instance);
    void (*set_period)(uint32_t instance, uint32_t period_us);
    void (*set_callback)(uint32_t instance, mago_timer_cb_t cb, void* ctx);
    uint32_t (*get_counter)(uint32_t instance);
    mago_time_us_t (*get_time_us)(void);  // Global timebase
    void (*delay_us)(uint32_t us);
    void (*delay_ms)(uint32_t ms);
} mago_timer_ops_t;

/* ============================================================================
 * I2C - Master only
 * ============================================================================ */

typedef struct {
    bool (*init)(uint32_t instance, uint32_t speed_hz);
    void (*deinit)(uint32_t instance);
    mago_result_t (*write)(uint32_t instance, uint16_t addr, const uint8_t* data, size_t len, uint32_t timeout_ms);
    mago_result_t (*read)(uint32_t instance, uint16_t addr, uint8_t* data, size_t len, uint32_t timeout_ms);
    mago_result_t (*mem_write)(uint32_t instance, uint16_t addr, uint16_t mem_addr, uint8_t mem_addr_size, const uint8_t* data, size_t len, uint32_t timeout_ms);
    mago_result_t (*mem_read)(uint32_t instance, uint16_t addr, uint16_t mem_addr, uint8_t mem_addr_size, uint8_t* data, size_t len, uint32_t timeout_ms);
    bool (*is_device_ready)(uint32_t instance, uint16_t addr, uint32_t timeout_ms);
} mago_i2c_ops_t;

/* ============================================================================
 * RTC - Timekeeping
 * ============================================================================ */

typedef struct {
    bool (*init)(void);
    void (*deinit)(void);
    mago_result_t (*set_time)(uint32_t unix_time);
    uint32_t (*get_time)(void);
    mago_result_t (*set_alarm)(uint32_t unix_time, void (*cb)(void* ctx), void* ctx);
    void (*clear_alarm)(void);
    mago_time_us_t (*get_time_us)(void);  // High-res if available
} mago_rtc_ops_t;

/* ============================================================================
 * FLASH - Non-volatile storage
 * ============================================================================ */

typedef struct {
    bool (*init)(void);
    void (*deinit)(void);
    mago_result_t (*erase)(uint32_t addr, size_t len);
    mago_result_t (*write)(uint32_t addr, const uint8_t* data, size_t len);
    mago_result_t (*read)(uint32_t addr, uint8_t* data, size_t len);
    uint32_t (*get_page_size)(void);
    uint32_t (*get_total_size)(void);
} mago_flash_ops_t;

/* ============================================================================
 * POWER MANAGEMENT
 * ============================================================================ */

typedef enum {
    MAGO_SLEEP_MODE_RUN = 0,
    MAGO_SLEEP_MODE_SLEEP,      // WFI
    MAGO_SLEEP_MODE_STOP,       // Stop mode, RAM retained
    MAGO_SLEEP_MODE_STANDBY,    // Standby, backup domain only
    MAGO_SLEEP_MODE_SHUTDOWN,   // Power down
} mago_sleep_mode_t;

typedef struct {
    void (*enter_sleep)(mago_sleep_mode_t mode);
    void (*configure_wakeup_pin)(const mago_pin_t* pin, bool rising);
    uint32_t (*get_reset_cause)(void);
    void (*clear_reset_flags)(void);
    void (*enable_backup_reg)(bool enable);
    void (*write_backup_reg)(uint32_t idx, uint32_t val);
    uint32_t (*read_backup_reg)(uint32_t idx);
} mago_pwr_ops_t;

/* ============================================================================
 * WATCHDOG
 * ============================================================================ */

typedef struct {
    bool (*init)(uint32_t timeout_ms);
    void (*refresh)(void);
    void (*deinit)(void);
} mago_wdg_ops_t;

/* ============================================================================
 * RANDOM NUMBER GENERATOR
 * ============================================================================ */

typedef struct {
    bool (*init)(void);
    uint32_t (*get32)(void);
    void (*get_bytes)(uint8_t* buf, size_t len);
} mago_rng_ops_t;

/* ============================================================================
 * MASTER HAL INTERFACE
 * ============================================================================ */

typedef struct {
    const char* name;
    mago_hal_backend_t backend;

    // Subsystem ops
    const mago_gpio_ops_t* gpio;
    const mago_spi_ops_t* spi;
    const mago_uart_ops_t* uart;
    const mago_adc_ops_t* adc;
    const mago_timer_ops_t* timer;
    const mago_i2c_ops_t* i2c;
    const mago_rtc_ops_t* rtc;
    const mago_flash_ops_t* flash;
    const mago_pwr_ops_t* pwr;
    const mago_wdg_ops_t* wdg;
    const mago_rng_ops_t* rng;

    // Global init/deinit
    bool (*early_init)(void);     // Before C runtime
    bool (*late_init)(void);      // After C runtime
    void (*deinit)(void);
} mago_hal_interface_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Backend registration
bool mago_hal_register_backend(const mago_hal_interface_t* iface);
bool mago_hal_select_backend(mago_hal_backend_t backend);
const mago_hal_interface_t* mago_hal_get(void);

// Convenience macros (use backend ops)
#define MAGO_GPIO_INIT(pin, mode, pull, speed)   mago_hal_get()->gpio->init(pin, mode, pull, speed)
#define MAGO_GPIO_WRITE(pin, val)                mago_hal_get()->gpio->write(pin, val)
#define MAGO_GPIO_READ(pin)                      mago_hal_get()->gpio->read(pin)
#define MAGO_GPIO_TOGGLE(pin)                    mago_hal_get()->gpio->toggle(pin)

#define MAGO_SPI_TRANSFER(inst, tx, rx, len, to) mago_hal_get()->spi->transfer(inst, tx, rx, len, to)
#define MAGO_UART_WRITE(inst, data, len, to)     mago_hal_get()->uart->write(inst, data, len, to)
#define MAGO_ADC_READ(ch, val)                   mago_hal_get()->adc->read(ch, val)
#define MAGO_TIMER_GET_US()                      mago_hal_get()->timer->get_time_us()
#define MAGO_DELAY_US(us)                        mago_hal_get()->timer->delay_us(us)
#define MAGO_DELAY_MS(ms)                        mago_hal_get()->timer->delay_ms(ms)

#endif // MAGO_HAL_H