/**
 * @file mago_hal.c
 * @brief Mago41 - HAL Implementation (libopencm3 backend)
 */

#include "mago_hal.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/wdt.h>
#include <libopencm3/stm32/rng.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <string.h>

/* ============================================================================
 * GPIO PORT/PIN HELPERS
 * ============================================================================ */

static uint32_t gpio_port(char port) {
    switch (port) {
        case 'A': case 'a': return GPIOA;
        case 'B': case 'b': return GPIOB;
        case 'C': case 'c': return GPIOC;
        case 'D': case 'd': return GPIOD;
        case 'E': case 'e': return GPIOE;
        default: return GPIOA;
    }
}

static uint16_t gpio_pin(uint8_t pin) {
    return 1u << pin;
}

static uint32_t gpio_mode(mago_gpio_mode_t mode) {
    switch (mode) {
        case MAGO_GPIO_INPUT: return GPIO_MODE_INPUT;
        case MAGO_GPIO_OUTPUT_PP: return GPIO_MODE_OUTPUT_50_MHZ | GPIO_CNF_OUTPUT_PUSHPULL;
        case MAGO_GPIO_OUTPUT_OD: return GPIO_MODE_OUTPUT_50_MHZ | GPIO_CNF_OUTPUT_OPENDRAIN;
        case MAGO_GPIO_AF_PP: return GPIO_MODE_OUTPUT_50_MHZ | GPIO_CNF_OUTPUT_ALTFN_PUSHPULL;
        case MAGO_GPIO_AF_OD: return GPIO_MODE_OUTPUT_50_MHZ | GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN;
        case MAGO_GPIO_ANALOG: return GPIO_MODE_INPUT | GPIO_CNF_INPUT_ANALOG;
        case MAGO_GPIO_IT_RISING: return GPIO_MODE_INPUT | GPIO_CNF_INPUT_FLOAT;
        case MAGO_GPIO_IT_FALLING: return GPIO_MODE_INPUT | GPIO_CNF_INPUT_FLOAT;
        case MAGO_GPIO_IT_BOTH: return GPIO_MODE_INPUT | GPIO_CNF_INPUT_FLOAT;
        default: return GPIO_MODE_INPUT | GPIO_CNF_INPUT_FLOAT;
    }
}

static uint32_t gpio_pull(mago_gpio_pull_t pull) {
    switch (pull) {
        case MAGO_GPIO_PULL_NONE: return 0;
        case MAGO_GPIO_PULL_UP: return GPIO_PUPD_PULLUP;
        case MAGO_GPIO_PULL_DOWN: return GPIO_PUPD_PULLDOWN;
        default: return 0;
    }
}

/* ============================================================================
 * GPIO OPS
 * ============================================================================ */

static mago_result_t hal_gpio_init(const mago_gpio_pin_t* pin) {
    if (!pin) return MAGO_ERR_INVAL;
    uint32_t port = gpio_port(pin->port);
    uint16_t gpio_pin_val = gpio_pin(pin->pin);
    rcc_periph_clock_enable(RCC_GPIOA + (port - GPIOA));
    gpio_set_mode(port, gpio_mode(pin->mode), gpio_pull(pin->pull), gpio_pin_val);
    return MAGO_OK;
}

static mago_result_t hal_gpio_deinit(const mago_gpio_pin_t* pin) {
    if (!pin) return MAGO_ERR_INVAL;
    uint32_t port = gpio_port(pin->port);
    uint16_t gpio_pin_val = gpio_pin(pin->pin);
    gpio_set_mode(port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, gpio_pin_val);
    return MAGO_OK;
}

static bool hal_gpio_read(const mago_gpio_pin_t* pin) {
    if (!pin) return false;
    return gpio_get(gpio_port(pin->port), gpio_pin(pin->pin));
}

static mago_result_t hal_gpio_write(const mago_gpio_pin_t* pin, bool value) {
    if (!pin) return MAGO_ERR_INVAL;
    if (value) gpio_set(gpio_port(pin->port), gpio_pin(pin->pin));
    else gpio_clear(gpio_port(pin->port), gpio_pin(pin->pin));
    return MAGO_OK;
}

static mago_result_t hal_gpio_toggle(const mago_gpio_pin_t* pin) {
    if (!pin) return MAGO_ERR_INVAL;
    gpio_toggle(gpio_port(pin->port), gpio_pin(pin->pin));
    return MAGO_OK;
}

static mago_result_t hal_gpio_irq_enable(const mago_gpio_pin_t* pin, mago_gpio_irq_cb_t cb, void* ctx) {
    if (!pin) return MAGO_ERR_INVAL;
    uint32_t port = gpio_port(pin->port);
    uint16_t gpio_pin_val = gpio_pin(pin->pin);
    uint8_t exti_line = pin->pin;

    // Enable AFIO for EXTI
    rcc_periph_clock_enable(RCC_AFIO);

    // Map EXTI line to GPIO port
    uint8_t port_source = (port - GPIOA) / 0x400;
    AFIO_EXTICR(exti_line / 4) = (AFIO_EXTICR(exti_line / 4) & ~(0xF << ((exti_line % 4) * 4)))
                                  | (port_source << ((exti_line % 4) * 4));

    // Configure EXTI
    exti_select_source(exti_line, port_source);
    exti_set_trigger(exti_line, EXTI_TRIGGER_BOTH);
    exti_enable_request(exti_line);

    nvic_enable_irq(NVIC_EXTI0_IRQ + exti_line);
    return MAGO_OK;
}

static mago_result_t hal_gpio_irq_disable(const mago_gpio_pin_t* pin) {
    if (!pin) return MAGO_ERR_INVAL;
    exti_disable_request(pin->pin);
    nvic_disable_irq(NVIC_EXTI0_IRQ + pin->pin);
    return MAGO_OK;
}

const mago_gpio_ops_t mago_hal_gpio_ops = {
    .init = hal_gpio_init,
    .deinit = hal_gpio_deinit,
    .read = hal_gpio_read,
    .write = hal_gpio_write,
    .toggle = hal_gpio_toggle,
    .irq_enable = hal_gpio_irq_enable,
    .irq_disable = hal_gpio_irq_disable,
};

/* ============================================================================
 * SPI OPS (SI4032 on SPI2)
 * ============================================================================ */

static mago_result_t hal_spi_init(const mago_spi_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;
    rcc_periph_clock_enable(RCC_SPI2);
    spi_init_master(SPI2,
        cfg->baudrate_prescaler,
        cfg->cpol ? SPI_CR1_CPOL : 0,
        cfg->cpha ? SPI_CR1_CPHA : 0,
        cfg->msb_first ? 0 : SPI_CR1_LSBFIRST,
        SPI_CR1_DFF_8BIT,
        SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI);
    spi_enable(SPI2);
    return MAGO_OK;
}

static mago_result_t hal_spi_deinit(void) {
    spi_disable(SPI2);
    rcc_periph_clock_disable(RCC_SPI2);
    return MAGO_OK;
}

static mago_result_t hal_spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t data = tx ? tx[i] : 0xFF;
        spi_send(SPI2, data);
        while (!(SPI_SR(SPI2) & SPI_SR_RXNE));
        if (rx) rx[i] = spi_read(SPI2);
        else (void)spi_read(SPI2);
    }
    return MAGO_OK;
}

static mago_result_t hal_spi_write(const uint8_t* tx, size_t len) {
    return hal_spi_transfer(tx, NULL, len);
}

static mago_result_t hal_spi_read(uint8_t* rx, size_t len) {
    return hal_spi_transfer(NULL, rx, len);
}

const mago_spi_ops_t mago_hal_spi_ops = {
    .init = hal_spi_init,
    .deinit = hal_spi_deinit,
    .transfer = hal_spi_transfer,
    .write = hal_spi_write,
    .read = hal_spi_read,
};

/* ============================================================================
 * UART OPS (GPS on USART1, Debug on USART2)
 * ============================================================================ */

static mago_result_t hal_uart_init(uint8_t instance, const mago_uart_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;
    uint32_t usart = (instance == 1) ? USART1 : USART2;
    rcc_periph_clock_enable((instance == 1) ? RCC_USART1 : RCC_USART2);

    usart_set_baudrate(usart, cfg->baudrate);
    usart_set_databits(usart, cfg->databits);
    usart_set_stopbits(usart, cfg->stopbits);
    usart_set_mode(usart, USART_MODE_TX_RX);
    usart_set_parity(usart, cfg->parity ? USART_PARITY_EVEN : USART_PARITY_NONE);
    usart_set_flow_control(usart, USART_FLOWCONTROL_NONE);
    usart_enable(usart);
    return MAGO_OK;
}

static mago_result_t hal_uart_deinit(uint8_t instance) {
    uint32_t usart = (instance == 1) ? USART1 : USART2;
    usart_disable(usart);
    rcc_periph_clock_disable((instance == 1) ? RCC_USART1 : RCC_USART2);
    return MAGO_OK;
}

static mago_result_t hal_uart_write(uint8_t instance, const uint8_t* data, size_t len) {
    uint32_t usart = (instance == 1) ? USART1 : USART2;
    for (size_t i = 0; i < len; i++) {
        usart_send_blocking(usart, data[i]);
    }
    return MAGO_OK;
}

static mago_result_t hal_uart_read(uint8_t instance, uint8_t* data, size_t len, uint32_t timeout_ms) {
    uint32_t usart = (instance == 1) ? USART1 : USART2;
    uint32_t start = mago_hal_timer_now_ms();
    size_t count = 0;

    while (count < len && (mago_hal_timer_now_ms() - start) < timeout_ms) {
        if (usart_get_flag(usart, USART_SR_RXNE)) {
            data[count++] = usart_recv(usart);
        }
    }
    return (count == len) ? MAGO_OK : MAGO_ERR_TIMEOUT;
}

static bool hal_uart_rx_ready(uint8_t instance) {
    uint32_t usart = (instance == 1) ? USART1 : USART2;
    return usart_get_flag(usart, USART_SR_RXNE);
}

const mago_uart_ops_t mago_hal_uart_ops = {
    .init = hal_uart_init,
    .deinit = hal_uart_deinit,
    .write = hal_uart_write,
    .read = hal_uart_read,
    .rx_ready = hal_uart_rx_ready,
};

/* ============================================================================
 * ADC OPS (Battery + Temp)
 * ============================================================================ */

static mago_result_t hal_adc_init(const mago_adc_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;
    rcc_periph_clock_enable(RCC_ADC1);
    rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV6);
    adc_power_on(ADC1);
    adc_set_sample_time(ADC1, cfg->channel, cfg->sample_time);
    adc_set_regular_sequence(ADC1, 1, &cfg->channel);
    adc_enable_temperature_sensor();
    adc_calibrate(ADC1);
    return MAGO_OK;
}

static mago_result_t hal_adc_deinit(void) {
    adc_power_off(ADC1);
    rcc_periph_clock_disable(RCC_ADC1);
    return MAGO_OK;
}

static mago_result_t hal_adc_read_channel(uint8_t channel, uint16_t* value) {
    if (!value) return MAGO_ERR_INVAL;
    adc_set_regular_sequence(ADC1, 1, &channel);
    adc_start_conversion_direct(ADC1);
    while (!adc_eoc(ADC1));
    *value = adc_read_regular(ADC1);
    return MAGO_OK;
}

const mago_adc_ops_t mago_hal_adc_ops = {
    .init = hal_adc_init,
    .deinit = hal_adc_deinit,
    .read_channel = hal_adc_read_channel,
};

/* ============================================================================
 * TIMER OPS (TIM3 for system tick, TIM2 for delays)
 * ============================================================================ */

static mago_result_t hal_timer_init(const mago_timer_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;
    uint32_t timer = (cfg->instance == 2) ? TIM2 : TIM3;
    rcc_periph_clock_enable((cfg->instance == 2) ? RCC_TIM2 : RCC_TIM3);
    timer_set_prescaler(timer, cfg->prescaler - 1);
    timer_set_period(timer, cfg->period - 1);
    timer_set_counter(timer, 0);
    timer_enable_counter(timer);
    return MAGO_OK;
}

static mago_result_t hal_timer_deinit(uint8_t instance) {
    uint32_t timer = (instance == 2) ? TIM2 : TIM3;
    timer_disable_counter(timer);
    rcc_periph_clock_disable((instance == 2) ? RCC_TIM2 : RCC_TIM3);
    return MAGO_OK;
}

static mago_result_t hal_timer_start(uint8_t instance) {
    timer_enable_counter((instance == 2) ? TIM2 : TIM3);
    return MAGO_OK;
}

static mago_result_t hal_timer_stop(uint8_t instance) {
    timer_disable_counter((instance == 2) ? TIM2 : TIM3);
    return MAGO_OK;
}

static uint32_t hal_timer_get_counter(uint8_t instance) {
    return timer_get_counter((instance == 2) ? TIM2 : TIM3);
}

static mago_result_t hal_timer_set_compare(uint8_t instance, uint32_t value) {
    timer_set_oc_value((instance == 2) ? TIM2 : TIM3, TIM_OC1, value);
    return MAGO_OK;
}

static uint32_t hal_timer_now_ms(void) {
    return timer_get_counter(TIM3) / 1000;  // Assuming 1MHz timer
}

static uint32_t hal_timer_now_us(void) {
    return timer_get_counter(TIM3);
}

static void hal_delay_ms(uint32_t ms) {
    uint32_t start = hal_timer_now_ms();
    while ((hal_timer_now_ms() - start) < ms);
}

static void hal_delay_us(uint32_t us) {
    uint32_t start = hal_timer_now_us();
    while ((hal_timer_now_us() - start) < us);
}

const mago_timer_ops_t mago_hal_timer_ops = {
    .init = hal_timer_init,
    .deinit = hal_timer_deinit,
    .start = hal_timer_start,
    .stop = hal_timer_stop,
    .get_counter = hal_timer_get_counter,
    .set_compare = hal_timer_set_compare,
    .now_ms = hal_timer_now_ms,
    .now_us = hal_timer_now_us,
    .delay_ms = hal_delay_ms,
    .delay_us = hal_delay_us,
};

/* ============================================================================
 * I2C OPS (for future sensors)
 * ============================================================================ */

static mago_result_t hal_i2c_init(const mago_i2c_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;
    rcc_periph_clock_enable(RCC_I2C1);
    i2c_peripheral_disable(I2C1);
    i2c_set_speed(I2C1, i2c_speed_sm_100k, 36);
    i2c_set_own_7bit_slave_address(I2C1, 0);
    i2c_peripheral_enable(I2C1);
    return MAGO_OK;
}

static mago_result_t hal_i2c_deinit(void) {
    i2c_peripheral_disable(I2C1);
    rcc_periph_clock_disable(RCC_I2C1);
    return MAGO_OK;
}

static mago_result_t hal_i2c_write(uint8_t addr, const uint8_t* data, size_t len) {
    i2c_transfer7(I2C1, addr, data, len, NULL, 0);
    return MAGO_OK;
}

static mago_result_t hal_i2c_read(uint8_t addr, uint8_t* data, size_t len) {
    i2c_transfer7(I2C1, addr, NULL, 0, data, len);
    return MAGO_OK;
}

static mago_result_t hal_i2c_write_read(uint8_t addr, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
    i2c_transfer7(I2C1, addr, tx, tx_len, rx, rx_len);
    return MAGO_OK;
}

const mago_i2c_ops_t mago_hal_i2c_ops = {
    .init = hal_i2c_init,
    .deinit = hal_i2c_deinit,
    .write = hal_i2c_write,
    .read = hal_i2c_read,
    .write_read = hal_i2c_write_read,
};

/* ============================================================================
 * RTC OPS
 * ============================================================================ */

static mago_result_t hal_rtc_init(void) {
    rcc_periph_clock_enable(RCC_BKP | RCC_PWR);
    pwr_enable_bkp_write();
    rcc_osc_on(RCC_LSE);
    rcc_wait_for_osc_ready(RCC_LSE);
    rcc_set_rtc_clock_source(RCC_CSR_RTCSEL_LSE);
    rcc_enable_rtc_clock();
    rtc_auto_awake(RCC_LSE, 32768);
    return MAGO_OK;
}

static mago_result_t hal_rtc_deinit(void) {
    rcc_disable_rtc_clock();
    return MAGO_OK;
}

static mago_result_t hal_rtc_get_time(uint32_t* epoch, uint32_t* subsec) {
    if (epoch) *epoch = rtc_get_counter_val();
    if (subsec) *subsec = 0;
    return MAGO_OK;
}

static mago_result_t hal_rtc_set_time(uint32_t epoch) {
    rtc_set_counter_val(epoch);
    return MAGO_OK;
}

static mago_result_t hal_rtc_set_alarm(uint32_t epoch) {
    rtc_set_alarm(epoch);
    rtc_interrupt_enable(RTC_ALR);
    nvic_enable_irq(NVIC_RTC_IRQ);
    return MAGO_OK;
}

static mago_result_t hal_rtc_disable_alarm(void) {
    rtc_interrupt_disable(RTC_ALR);
    return MAGO_OK;
}

const mago_rtc_ops_t mago_hal_rtc_ops = {
    .init = hal_rtc_init,
    .deinit = hal_rtc_deinit,
    .get_time = hal_rtc_get_time,
    .set_time = hal_rtc_set_time,
    .set_alarm = hal_rtc_set_alarm,
    .disable_alarm = hal_rtc_disable_alarm,
};

/* ============================================================================
 * FLASH OPS
 * ============================================================================ */

static mago_result_t hal_flash_erase(uint32_t addr, size_t len) {
    flash_unlock();
    for (uint32_t a = addr; a < addr + len; a += 1024) {
        flash_erase_page(a);
    }
    flash_lock();
    return MAGO_OK;
}

static mago_result_t hal_flash_write(uint32_t addr, const uint8_t* data, size_t len) {
    flash_unlock();
    for (size_t i = 0; i < len; i += 2) {
        uint16_t val = data[i];
        if (i + 1 < len) val |= (data[i+1] << 8);
        flash_program_half_word(addr + i, val);
    }
    flash_lock();
    return MAGO_OK;
}

static mago_result_t hal_flash_read(uint32_t addr, uint8_t* data, size_t len) {
    memcpy(data, (const void*)addr, len);
    return MAGO_OK;
}

const mago_flash_ops_t mago_hal_flash_ops = {
    .erase = hal_flash_erase,
    .write = hal_flash_write,
    .read = hal_flash_read,
};

/* ============================================================================
 * POWER OPS
 * ============================================================================ */

static mago_result_t hal_pwr_sleep(void) {
    __WFI();
    return MAGO_OK;
}

static mago_result_t hal_pwr_stop(void) {
    pwr_set_stop_mode(PWR_CR_PDDS_STOP, PWR_CR_LPDS_LOWPOWER);
    __WFI();
    return MAGO_OK;
}

static mago_result_t hal_pwr_standby(void) {
    pwr_set_standby_mode();
    __WFI();
    return MAGO_OK;
}

static mago_result_t hal_pwr_wakeup_pin(uint8_t pin, bool rising) {
    // Configure wakeup pin
    return MAGO_OK;
}

const mago_pwr_ops_t mago_hal_pwr_ops = {
    .sleep = hal_pwr_sleep,
    .stop = hal_pwr_stop,
    .standby = hal_pwr_standby,
    .wakeup_pin = hal_pwr_wakeup_pin,
};

/* ============================================================================
 * WATCHDOG OPS
 * ============================================================================ */

static mago_result_t hal_wdg_init(uint32_t timeout_ms) {
    // IWDG: LSI/64 * (PR+1) * (RLR+1) = timeout
    // LSI = 40kHz, /64 = 625Hz
    uint32_t reload = (timeout_ms * 625) / 1000;
    iwdg_set_period_ms(timeout_ms);
    iwdg_start();
    return MAGO_OK;
}

static mago_result_t hal_wdg_refresh(void) {
    iwdg_reset();
    return MAGO_OK;
}

static mago_result_t hal_wdg_deinit(void) {
    // IWDG cannot be stopped once started
    return MAGO_OK;
}

const mago_wdg_ops_t mago_hal_wdg_ops = {
    .init = hal_wdg_init,
    .refresh = hal_wdg_refresh,
    .deinit = hal_wdg_deinit,
};

/* ============================================================================
 * RNG OPS
 * ============================================================================ */

static mago_result_t hal_rng_init(void) {
    rcc_periph_clock_enable(RCC_RNG);
    rng_enable();
    return MAGO_OK;
}

static mago_result_t hal_rng_deinit(void) {
    rng_disable();
    rcc_periph_clock_disable(RCC_RNG);
    return MAGO_OK;
}

static mago_result_t hal_rng_get(uint32_t* out) {
    if (!out) return MAGO_ERR_INVAL;
    *out = rng_get_random_blocking();
    return MAGO_OK;
}

static mago_result_t hal_rng_get_bytes(uint8_t* out, size_t len) {
    if (!out) return MAGO_ERR_INVAL;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = rng_get_random_blocking();
        size_t to_copy = (len - i >= 4) ? 4 : len - i;
        memcpy(&out[i], &r, to_copy);
    }
    return MAGO_OK;
}

const mago_rng_ops_t mago_hal_rng_ops = {
    .init = hal_rng_init,
    .deinit = hal_rng_deinit,
    .get = hal_rng_get,
    .get_bytes = hal_rng_get_bytes,
};

/* ============================================================================
 * SYSTEM OPS
 * ============================================================================ */

static mago_result_t hal_early_init(void) {
    // Clock setup: HSE 8MHz -> PLL -> 72MHz (F103) or 24MHz (F100)
    #ifdef STM32F103xB
    rcc_clock_setup_in_hse_8mhz_out_72mhz();
    #else
    rcc_clock_setup_in_hse_8mhz_out_24mhz();
    #endif

    // Enable clocks for all peripherals we need
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOD);

    // SysTick for 1ms tick (used by HAL delay)
    systick_set_reload(SYSTEM_CORE_CLOCK / 1000 - 1);
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_counter_enable();
    systick_interrupt_enable();

    return MAGO_OK;
}

static mago_result_t hal_late_init(void) {
    // Initialize all peripherals
    mago_hal_gpio_ops.init(&g_mago_pins.led_status);
    mago_hal_gpio_ops.init(&g_mago_pins.led_error);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_cs);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_sck);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_mosi);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_miso);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_sdn);
    mago_hal_gpio_ops.init(&g_mago_pins.radio_nirq);
    mago_hal_gpio_ops.init(&g_mago_pins.gps_tx);
    mago_hal_gpio_ops.init(&g_mago_pins.gps_rx);
    mago_hal_gpio_ops.init(&g_mago_pins.gps_rst);
    mago_hal_gpio_ops.init(&g_mago_pins.debug_tx);
    mago_hal_gpio_ops.init(&g_mago_pins.debug_rx);
    mago_hal_gpio_ops.init(&g_mago_pins.adc_batt);
    mago_hal_gpio_ops.init(&g_mago_pins.adc_temp);
    mago_hal_gpio_ops.init(&g_mago_pins.btn_user);

    mago_hal_spi_ops.init(&(mago_spi_cfg_t){.baudrate_prescaler = 0x04, .cpol = 0, .cpha = 0, .msb_first = true});
    mago_hal_uart_ops.init(1, &(mago_uart_cfg_t){.baudrate = 9600, .databits = 8, .stopbits = 1, .parity = false});
    mago_hal_uart_ops.init(2, &(mago_uart_cfg_t){.baudrate = 115200, .databits = 8, .stopbits = 1, .parity = false});
    mago_hal_adc_ops.init(&(mago_adc_cfg_t){.channel = 0, .sample_time = ADC_SMPR_SMP_239DOT5CYC});
    mago_hal_timer_init(&(mago_timer_cfg_t){.instance = 3, .prescaler = 72, .period = 0xFFFF, .mode = 0}); // 1MHz
    mago_hal_timer_init(&(mago_timer_cfg_t){.instance = 2, .prescaler = 72, .period = 0xFFFF, .mode = 0});
    mago_hal_rtc_ops.init();
    mago_hal_rng_ops.init();

    return MAGO_OK;
}

static mago_result_t hal_deinit(void) {
    mago_hal_spi_ops.deinit();
    mago_hal_uart_ops.deinit(1);
    mago_hal_uart_ops.deinit(2);
    mago_hal_adc_ops.deinit();
    mago_hal_timer_ops.deinit(2);
    mago_hal_timer_ops.deinit(3);
    mago_hal_rtc_ops.deinit();
    mago_hal_rng_ops.deinit();
    mago_hal_wdg_ops.deinit();
    return MAGO_OK;
}

static void hal_reset(void) {
    scb_reset_system();
}

static void hal_nop(void) {
    __asm__("nop");
}

const mago_sys_ops_t mago_hal_sys_ops = {
    .early_init = hal_early_init,
    .late_init = hal_late_init,
    .deinit = hal_deinit,
    .reset = hal_reset,
    .nop = hal_nop,
};

/* ============================================================================
 * HAL INSTANCE
 * ============================================================================ */

const mago_hal_t mago_hal_instance = {
    .backend = MAGO_HAL_LIBOPENCM3,
    .gpio = &mago_hal_gpio_ops,
    .spi = &mago_hal_spi_ops,
    .uart = &mago_hal_uart_ops,
    .adc = &mago_hal_adc_ops,
    .timer = &mago_hal_timer_ops,
    .i2c = &mago_hal_i2c_ops,
    .rtc = &mago_hal_rtc_ops,
    .flash = &mago_hal_flash_ops,
    .pwr = &mago_hal_pwr_ops,
    .wdg = &mago_hal_wdg_ops,
    .rng = &mago_hal_rng_ops,
    .sys = &mago_hal_sys_ops,
};

const mago_hal_t* mago_hal_get(void) {
    return &mago_hal_instance;
}