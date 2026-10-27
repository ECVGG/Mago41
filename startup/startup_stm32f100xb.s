/**
 * @file startup_stm32f100xb.s
 * @brief Mago41 - Startup Code for STM32F100xB (24MHz)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Vector table en C (no C++ constructors)
 * - Reset_Handler mínimo, llama a main()
 * - Handlers por defecto débiles (weak) para override
 * - Clock setup para 24MHz (HSE 8MHz * 3)
 */

.syntax unified
.cpu cortex-m3
.fpu softvfp
.thumb

/* ============================================================================
 * MEMORY LAYOUT (from linker script)
 * ============================================================================ */
.extern _estack
.extern _sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

/* ============================================================================
 * VECTOR TABLE
 * ============================================================================ */
.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
.size g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word _estack                    /* 0x00: Initial SP */
    .word Reset_Handler              /* 0x04: Reset */
    .word NMI_Handler                /* 0x08: NMI */
    .word HardFault_Handler          /* 0x0C: HardFault */
    .word MemManage_Handler          /* 0x10: MemManage */
    .word BusFault_Handler           /* 0x14: BusFault */
    .word UsageFault_Handler         /* 0x18: UsageFault */
    .word 0                          /* 0x1C: Reserved */
    .word 0                          /* 0x20: Reserved */
    .word 0                          /* 0x24: Reserved */
    .word 0                          /* 0x28: Reserved */
    .word SVC_Handler                /* 0x2C: SVC */
    .word DebugMon_Handler           /* 0x30: DebugMon */
    .word 0                          /* 0x34: Reserved */
    .word PendSV_Handler             /* 0x38: PendSV */
    .word SysTick_Handler            /* 0x3C: SysTick */

    /* External Interrupts (same as F103) */
    .word WWDG_IRQHandler            /* 0x40: Window Watchdog */
    .word PVD_IRQHandler             /* 0x44: PVD */
    .word TAMPER_IRQHandler          /* 0x48: Tamper */
    .word RTC_IRQHandler             /* 0x4C: RTC */
    .word FLASH_IRQHandler           /* 0x50: Flash */
    .word RCC_IRQHandler             /* 0x54: RCC */
    .word EXTI0_IRQHandler           /* 0x58: EXTI0 */
    .word EXTI1_IRQHandler           /* 0x5C: EXTI1 */
    .word EXTI2_IRQHandler           /* 0x60: EXTI2 */
    .word EXTI3_IRQHandler           /* 0x64: EXTI3 */
    .word EXTI4_IRQHandler           /* 0x68: EXTI4 */
    .word DMA1_Channel1_IRQHandler   /* 0x6C: DMA1 Ch1 */
    .word DMA1_Channel2_IRQHandler   /* 0x70: DMA1 Ch2 */
    .word DMA1_Channel3_IRQHandler   /* 0x74: DMA1 Ch3 */
    .word DMA1_Channel4_IRQHandler   /* 0x78: DMA1 Ch4 */
    .word DMA1_Channel5_IRQHandler   /* 0x7C: DMA1 Ch5 */
    .word DMA1_Channel6_IRQHandler   /* 0x80: DMA1 Ch6 */
    .word DMA1_Channel7_IRQHandler   /* 0x84: DMA1 Ch7 */
    .word ADC1_2_IRQHandler          /* 0x88: ADC1/2 */
    .word USB_HP_CAN1_TX_IRQHandler  /* 0x8C: USB HP / CAN1 TX */
    .word USB_LP_CAN1_RX0_IRQHandler /* 0x90: USB LP / CAN1 RX0 */
    .word CAN1_RX1_IRQHandler        /* 0x94: CAN1 RX1 */
    .word CAN1_SCE_IRQHandler        /* 0x98: CAN1 SCE */
    .word EXTI9_5_IRQHandler         /* 0x9C: EXTI9..5 */
    .word TIM1_BRK_IRQHandler        /* 0xA0: TIM1 Break */
    .word TIM1_UP_IRQHandler         /* 0xA4: TIM1 Update */
    .word TIM1_TRG_COM_IRQHandler    /* 0xA8: TIM1 Trig/Com */
    .word TIM1_CC_IRQHandler         /* 0xAC: TIM1 Capture Compare */
    .word TIM2_IRQHandler            /* 0xB0: TIM2 */
    .word TIM3_IRQHandler            /* 0xB4: TIM3 */
    .word TIM4_IRQHandler            /* 0xB8: TIM4 */
    .word I2C1_EV_IRQHandler         /* 0xBC: I2C1 Event */
    .word I2C1_ER_IRQHandler         /* 0xC0: I2C1 Error */
    .word I2C2_EV_IRQHandler         /* 0xC4: I2C2 Event */
    .word I2C2_ER_IRQHandler         /* 0xC8: I2C2 Error */
    .word SPI1_IRQHandler            /* 0xCC: SPI1 */
    .word SPI2_IRQHandler            /* 0xD0: SPI2 */
    .word USART1_IRQHandler          /* 0xD4: USART1 */
    .word USART2_IRQHandler          /* 0xD8: USART2 */
    .word USART3_IRQHandler          /* 0xDC: USART3 */
    .word EXTI15_10_IRQHandler       /* 0xE0: EXTI15..10 */
    .word RTCAlarm_IRQHandler        /* 0xE4: RTC Alarm */
    .word USBWakeUp_IRQHandler       /* 0xE8: USB Wakeup */
    .word TIM8_BRK_IRQHandler        /* 0xEC: TIM8 Break */
    .word TIM8_UP_IRQHandler         /* 0xF0: TIM8 Update */
    .word TIM8_TRG_COM_IRQHandler    /* 0xF4: TIM8 Trig/Com */
    .word TIM8_CC_IRQHandler         /* 0xF8: TIM8 Capture Compare */
    .word ADC3_IRQHandler            /* 0xFC: ADC3 */
    .word FSMC_IRQHandler            /* 0x100: FSMC */
    .word SDIO_IRQHandler            /* 0x104: SDIO */
    .word TIM5_IRQHandler            /* 0x108: TIM5 */
    .word SPI3_IRQHandler            /* 0x10C: SPI3 */
    .word UART4_IRQHandler           /* 0x110: UART4 */
    .word UART5_IRQHandler           /* 0x114: UART5 */
    .word TIM6_IRQHandler            /* 0x118: TIM6 */
    .word TIM7_IRQHandler            /* 0x11C: TIM7 */
    .word DMA2_Channel1_IRQHandler   /* 0x120: DMA2 Ch1 */
    .word DMA2_Channel2_IRQHandler   /* 0x124: DMA2 Ch2 */
    .word DMA2_Channel3_IRQHandler   /* 0x128: DMA2 Ch3 */
    .word DMA2_Channel4_5_IRQHandler /* 0x12C: DMA2 Ch4/5 */

.size g_pfnVectors, .-g_pfnVectors

/* ============================================================================
 * DEFAULT HANDLERS (weak) - Same as F103
 * ============================================================================ */
.section .text.Default_Handler,"ax",%progbits
.weak Default_Handler
.type Default_Handler, %function
Default_Handler:
    b Default_Handler
.size Default_Handler, .-Default_Handler

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler
.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler
.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
.weak WWDG_IRQHandler
.thumb_set WWDG_IRQHandler, Default_Handler
.weak PVD_IRQHandler
.thumb_set PVD_IRQHandler, Default_Handler
.weak TAMPER_IRQHandler
.thumb_set TAMPER_IRQHandler, Default_Handler
.weak RTC_IRQHandler
.thumb_set RTC_IRQHandler, Default_Handler
.weak FLASH_IRQHandler
.thumb_set FLASH_IRQHandler, Default_Handler
.weak RCC_IRQHandler
.thumb_set RCC_IRQHandler, Default_Handler
.weak EXTI0_IRQHandler
.thumb_set EXTI0_IRQHandler, Default_Handler
.weak EXTI1_IRQHandler
.thumb_set EXTI1_IRQHandler, Default_Handler
.weak EXTI2_IRQHandler
.thumb_set EXTI2_IRQHandler, Default_Handler
.weak EXTI3_IRQHandler
.thumb_set EXTI3_IRQHandler, Default_Handler
.weak EXTI4_IRQHandler
.thumb_set EXTI4_IRQHandler, Default_Handler
.weak DMA1_Channel1_IRQHandler
.thumb_set DMA1_Channel1_IRQHandler, Default_Handler
.weak DMA1_Channel2_IRQHandler
.thumb_set DMA1_Channel2_IRQHandler, Default_Handler
.weak DMA1_Channel3_IRQHandler
.thumb_set DMA1_Channel3_IRQHandler, Default_Handler
.weak DMA1_Channel4_IRQHandler
.thumb_set DMA1_Channel4_IRQHandler, Default_Handler
.weak DMA1_Channel5_IRQHandler
.thumb_set DMA1_Channel5_IRQHandler, Default_Handler
.weak DMA1_Channel6_IRQHandler
.thumb_set DMA1_Channel6_IRQHandler, Default_Handler
.weak DMA1_Channel7_IRQHandler
.thumb_set DMA1_Channel7_IRQHandler, Default_Handler
.weak ADC1_2_IRQHandler
.thumb_set ADC1_2_IRQHandler, Default_Handler
.weak USB_HP_CAN1_TX_IRQHandler
.thumb_set USB_HP_CAN1_TX_IRQHandler, Default_Handler
.weak USB_LP_CAN1_RX0_IRQHandler
.thumb_set USB_LP_CAN1_RX0_IRQHandler, Default_Handler
.weak CAN1_RX1_IRQHandler
.thumb_set CAN1_RX1_IRQHandler, Default_Handler
.weak CAN1_SCE_IRQHandler
.thumb_set CAN1_SCE_IRQHandler, Default_Handler
.weak EXTI9_5_IRQHandler
.thumb_set EXTI9_5_IRQHandler, Default_Handler
.weak TIM1_BRK_IRQHandler
.thumb_set TIM1_BRK_IRQHandler, Default_Handler
.weak TIM1_UP_IRQHandler
.thumb_set TIM1_UP_IRQHandler, Default_Handler
.weak TIM1_TRG_COM_IRQHandler
.thumb_set TIM1_TRG_COM_IRQHandler, Default_Handler
.weak TIM1_CC_IRQHandler
.thumb_set TIM1_CC_IRQHandler, Default_Handler
.weak TIM2_IRQHandler
.thumb_set TIM2_IRQHandler, Default_Handler
.weak TIM3_IRQHandler
.thumb_set TIM3_IRQHandler, Default_Handler
.weak TIM4_IRQHandler
.thumb_set TIM4_IRQHandler, Default_Handler
.weak I2C1_EV_IRQHandler
.thumb_set I2C1_EV_IRQHandler, Default_Handler
.weak I2C1_ER_IRQHandler
.thumb_set I2C1_ER_IRQHandler, Default_Handler
.weak I2C2_EV_IRQHandler
.thumb_set I2C2_EV_IRQHandler, Default_Handler
.weak I2C2_ER_IRQHandler
.thumb_set I2C2_ER_IRQHandler, Default_Handler
.weak SPI1_IRQHandler
.thumb_set SPI1_IRQHandler, Default_Handler
.weak SPI2_IRQHandler
.thumb_set SPI2_IRQHandler, Default_Handler
.weak USART1_IRQHandler
.thumb_set USART1_IRQHandler, Default_Handler
.weak USART2_IRQHandler
.thumb_set USART2_IRQHandler, Default_Handler
.weak USART3_IRQHandler
.thumb_set USART3_IRQHandler, Default_Handler
.weak EXTI15_10_IRQHandler
.thumb_set EXTI15_10_IRQHandler, Default_Handler
.weak RTCAlarm_IRQHandler
.thumb_set RTCAlarm_IRQHandler, Default_Handler
.weak USBWakeUp_IRQHandler
.thumb_set USBWakeUp_IRQHandler, Default_Handler
.weak TIM8_BRK_IRQHandler
.thumb_set TIM8_BRK_IRQHandler, Default_Handler
.weak TIM8_UP_IRQHandler
.thumb_set TIM8_UP_IRQHandler, Default_Handler
.weak TIM8_TRG_COM_IRQHandler
.thumb_set TIM8_TRG_COM_IRQHandler, Default_Handler
.weak TIM8_CC_IRQHandler
.thumb_set TIM8_CC_IRQHandler, Default_Handler
.weak ADC3_IRQHandler
.thumb_set ADC3_IRQHandler, Default_Handler
.weak FSMC_IRQHandler
.thumb_set FSMC_IRQHandler, Default_Handler
.weak SDIO_IRQHandler
.thumb_set SDIO_IRQHandler, Default_Handler
.weak TIM5_IRQHandler
.thumb_set TIM5_IRQHandler, Default_Handler
.weak SPI3_IRQHandler
.thumb_set SPI3_IRQHandler, Default_Handler
.weak UART4_IRQHandler
.thumb_set UART4_IRQHandler, Default_Handler
.weak UART5_IRQHandler
.thumb_set UART5_IRQHandler, Default_Handler
.weak TIM6_IRQHandler
.thumb_set TIM6_IRQHandler, Default_Handler
.weak TIM7_IRQHandler
.thumb_set TIM7_IRQHandler, Default_Handler
.weak DMA2_Channel1_IRQHandler
.thumb_set DMA2_Channel1_IRQHandler, Default_Handler
.weak DMA2_Channel2_IRQHandler
.thumb_set DMA2_Channel2_IRQHandler, Default_Handler
.weak DMA2_Channel3_IRQHandler
.thumb_set DMA2_Channel3_IRQHandler, Default_Handler
.weak DMA2_Channel4_5_IRQHandler
.thumb_set DMA2_Channel4_5_IRQHandler, Default_Handler

/* ============================================================================
 * RESET HANDLER - Same as F103
 * ============================================================================ */
.section .text.Reset_Handler,"ax",%progbits
.type Reset_Handler, %function
Reset_Handler:
    /* Initialize data section */
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
    cmp r0, r1
    beq 2f
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r1, r2
    bne 1b
2:
    /* Zero init bss section */
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
    cmp r1, r2
    beq 3f
4:
    str r3, [r1], #4
    cmp r1, r2
    bne 4b
3:
    /* Call SystemInit (clock setup) */
    bl SystemInit

    /* Call main */
    bl main

    /* Should never return */
    b Default_Handler

.size Reset_Handler, .-Reset_Handler

/* ============================================================================
 * SYSTEM INIT - Clock Configuration for STM32F100xB (24MHz)
 * ============================================================================ */
.section .text.SystemInit,"ax",%progbits
.type SystemInit, %function
SystemInit:
    /* Enable HSE (8MHz crystal on RS41) */
    ldr r0, =0x40021000  @ RCC base
    ldr r1, [r0, #0x00]  @ RCC_CR
    orr r1, r1, #0x00010000  @ HSEON
    str r1, [r0, #0x00]

    /* Wait for HSERDY */
5:
    ldr r1, [r0, #0x00]
    tst r1, #0x00020000  @ HSERDY
    beq 5b

    /* Configure Flash: 0 wait states (24MHz), prefetch enable */
    ldr r1, =0x40022000  @ FLASH base
    ldr r2, [r1, #0x00]  @ FLASH_ACR
    orr r2, r2, #0x00000010  @ PRFTBE | LATENCY_0
    str r2, [r1, #0x00]

    /* HCLK = SYSCLK (24MHz) */
    ldr r2, [r0, #0x04]  @ RCC_CFGR
    bic r2, r2, #0x000000F0  @ HPRE
    str r2, [r0, #0x04]

    /* PCLK2 = HCLK (24MHz) */
    bic r2, r2, #0x00003800  @ PPRE2
    str r2, [r0, #0x04]

    /* PCLK1 = HCLK (24MHz) */
    bic r2, r2, #0x00000700  @ PPRE1
    str r2, [r0, #0x04]

    /* PLL: HSE * 3 = 24MHz */
    /* PLLMUL = 3 (0x08000), PLLSRC = HSE (0x00010000) */
    ldr r2, [r0, #0x04]  @ RCC_CFGR
    bic r2, r2, #0x003D0000  @ PLLMUL | PLLSRC | PLLXTPRE
    orr r2, r2, #0x00090000  @ PLLSRC_HSE | PLLMUL3
    str r2, [r0, #0x04]

    /* Enable PLL */
    ldr r1, [r0, #0x00]  @ RCC_CR
    orr r1, r1, #0x01000000  @ PLLON
    str r1, [r0, #0x00]

    /* Wait for PLLRDY */
6:
    ldr r1, [r0, #0x00]
    tst r1, #0x02000000  @ PLLRDY
    beq 6b

    /* Switch SYSCLK to PLL */
    ldr r2, [r0, #0x04]  @ RCC_CFGR
    bic r2, r2, #0x00000003  @ SW
    orr r2, r2, #0x00000002  @ SW_PLL
    str r2, [r0, #0x04]

    /* Wait for SWS = PLL */
7:
    ldr r2, [r0, #0x04]
    tst r2, #0x0000000C  @ SWS
    beq 7b

    /* Enable peripheral clocks needed early */
    /* GPIOA, GPIOB, GPIOC, GPIOD, AFIO */
    ldr r1, [r0, #0x18]  @ RCC_APB2ENR
    orr r1, r1, #0x0000011C  @ IOPAEN | IOPBEN | IOPCEN | IOPDEN | AFIOEN
    str r1, [r0, #0x18]

    bx lr

.size SystemInit, .-SystemInit