/**
 * @file main.c
 * @brief Mago41 - Entry Point
 *
 * MÍNIMO - solo inicializa HAL y arranca app
 * Toda la lógica está en componentes y message bus
 */

#include "mago_app.h"
#include "mago_hal.h"
#include "mago_config.h"
#include <stdio.h>

/* ============================================================================
 * BOARD-SPECIFIC OVERRIDES
 * ============================================================================ */

// Override pins for specific board (RS41)
mago_board_pins_t g_mago_pins = {
    .led_status   = {.port = 'C', .pin = 13, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .led_error    = {.port = 'C', .pin = 14, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .radio_cs     = {.port = 'B', .pin = 12, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .radio_sck    = {.port = 'B', .pin = 13, .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH, .af = 5},
    .radio_mosi   = {.port = 'B', .pin = 15, .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH, .af = 5},
    .radio_miso   = {.port = 'B', .pin = 14, .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH, .af = 5},
    .radio_sdn    = {.port = 'B', .pin = 0,  .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .radio_nirq   = {.port = 'B', .pin = 1,  .mode = MAGO_GPIO_IT_BOTH, .speed = MAGO_GPIO_SPEED_HIGH},
    .gps_tx       = {.port = 'A', .pin = 9,  .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH, .af = 7},
    .gps_rx       = {.port = 'A', .pin = 10, .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH, .af = 7},
    .gps_rst      = {.port = 'A', .pin = 15, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .debug_tx     = {.port = 'A', .pin = 2,  .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH, .af = 7},
    .debug_rx     = {.port = 'A', .pin = 3,  .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH, .af = 7},
    .adc_batt     = {.port = 'A', .pin = 0,  .mode = MAGO_GPIO_ANALOG},
    .adc_temp     = {.port = 'A', .pin = 1,  .mode = MAGO_GPIO_ANALOG},
    .btn_user     = {.port = 'C', .pin = 13, .mode = MAGO_GPIO_IT_BOTH, .speed = MAGO_GPIO_SPEED_LOW},
};

/* ============================================================================
 * BOARD CALLBACKS
 * ============================================================================ */

void mago_app_on_fatal_error(uint32_t error_code, const char* msg) {
    // Pattern: 3 fast blinks = fatal
    for (int i = 0; i < 3; i++) {
        MAGO_GPIO_WRITE(&g_mago_pins.led_error, true);
        MAGO_DELAY_MS(100);
        MAGO_GPIO_WRITE(&g_mago_pins.led_error, false);
        MAGO_DELAY_MS(100);
    }
}

void mago_app_on_state_change(mago_app_state_t old_state, mago_app_state_t new_state) {
    // Status LED pattern per state
    static const uint16_t patterns[] = {
        [MAGO_APP_STATE_BOOT]       = 0b0001,
        [MAGO_APP_STATE_INIT]       = 0b0011,
        [MAGO_APP_STATE_RUNNING]    = 0b0101,
        [MAGO_APP_STATE_SLEEP_PREP] = 0b1001,
        [MAGO_APP_STATE_SLEEPING]   = 0b1000,
        [MAGO_APP_STATE_WAKING]     = 0b1100,
        [MAGO_APP_STATE_ERROR]      = 0b1111,
    };

    if (new_state < MAGO_ARRAY_SIZE(patterns)) {
        // Could drive LED pattern here
    }
}

void mago_app_on_status_report(const mago_app_t* app) {
    mago_time_us_t uptime = MAGO_TIMER_GET_US() - app->boot_time;
    printf("[MAGO41] Up:%lus TX:%lu/%lu GPS:%lu Err:%lu State:%d\n",
           (uint32_t)(uptime / 1000000),
           app->tx_success, app->tx_attempts,
           app->gps_fixes, app->errors,
           app->state);
}

void mago_app_on_pre_sleep(uint16_t minutes) {
    MAGO_GPIO_WRITE(&g_mago_pins.led_status, false);
    printf("[MAGO41] Sleep %u min\n", minutes);
}

void mago_app_on_wake(void) {
    MAGO_GPIO_WRITE(&g_mago_pins.led_status, true);
    printf("[MAGO41] Wake\n");
}

/* ============================================================================
 * INTERRUPT HANDLERS
 * ============================================================================ */

void SysTick_Handler(void) {
    // HAL tick
}

void EXTI15_10_IRQHandler(void) {
    // Button on PC13
    if (MAGO_HAL_GET()->gpio->read(&g_mago_pins.btn_user) == 0) {
        mago_app_handle_button_isr(false);
    }
    // Clear EXTI
}

void USART1_IRQHandler(void) {
    // GPS UART
    // mago_hal_get()->uart->handle_irq(USART1);
}

void USART2_IRQHandler(void) {
    // Debug UART
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    // 1. Early HAL init (clocks, power)
    if (!MAGO_HAL_GET()->early_init) return 1;

    // 2. Late HAL init (peripherals)
    if (!MAGO_HAL_GET()->late_init) return 1;

    // 3. Load config from flash
    mago_system_config_t config;
    if (!mago_config_load(&config)) {
        mago_config_defaults(&config);
    }

    // 4. Initialize app
    mago_app_t app;
    if (!mago_app_init(&app, &config)) {
        return 1;
    }

    // 5. Run forever (event-driven)
    mago_app_run(&app);

    // 6. Cleanup (never reached unless shutdown requested)
    MAGO_HAL_GET()->deinit();

    return 0;
}