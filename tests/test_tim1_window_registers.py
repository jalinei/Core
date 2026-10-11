#!/usr/bin/env python3
"""Run TIM1 window register checks on Linux using the installed STM32 LL headers.

Loads the window functions from the driver rather than duplicating them. RAM
stands in for TIM1 registers; W0C flag clearing and the ARM barrier are emulated.
This verifies configuration and arming policy, not physical pin timing.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PIO = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
HAL = PIO / "packages/framework-zephyr/_pio/modules/hal"
STM = HAL / "stm32/stm32cube/stm32g4xx"
INCLUDES = [STM / "drivers/include", STM / "soc", HAL / "cmsis/CMSIS/Core/Include"]

PREFIX = r"""#define STM32G474xx
#include <stm32g4xx_ll_tim.h>
#include <stm32g4xx_ll_dma.h>
#define __LDREXW(p) (*(p))
#define __STREXW(v, p) ((*(p) = (v)), 0U)
#include <stm32g4xx_ll_usart.h>
#include <stm32g4xx_ll_gpio.h>
#include <stm32g4xx_ll_hrtim.h>
#include <stm32g4xx_ll_bus.h>
#define CONFIG_SHIELD_TWIST_V1_4_1 1
void timer1_sync_stop(void) {}
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#undef __DSB
#define __DSB() ((void)0)
#define LL_TIM_ClearFlag_CC4(t) ((t)->SR &= ~TIM_SR_CC4IF)
#define LL_TIM_ClearFlag_TRIG(t) ((t)->SR &= ~TIM_SR_TIF)
#define LL_TIM_ClearFlag_CC1(t) ((t)->SR &= ~TIM_SR_CC1IF)
#define LL_TIM_ClearFlag_CC3(t) ((t)->SR &= ~TIM_SR_CC3IF)
#undef __DMB
#define __DMB() ((void)0)
#define LL_DMA_ClearFlag_GI8(t) ((t)->ISR &= ~(DMA_ISR_GIF8 | DMA_ISR_TCIF8 | DMA_ISR_HTIF8 | DMA_ISR_TEIF8))
#define LL_DMA_ClearFlag_GI6(t) ((t)->ISR &= ~(DMA_ISR_GIF6 | DMA_ISR_TCIF6 | DMA_ISR_HTIF6 | DMA_ISR_TEIF6))
#define LL_DMA_ClearFlag_TC6(t) ((t)->ISR &= ~DMA_ISR_TCIF6)
#define LL_DMA_ClearFlag_HT6(t) ((t)->ISR &= ~DMA_ISR_HTIF6)
#define DMA_USART DMA1
#define LL_DMA_CHANNEL_TX LL_DMA_CHANNEL_6
#define RS485_ERROR_TX_DEADLINE (1U << 7)
static bool synchronous_mode = true, sync_tx_configured = true, sync_tx_pending;
static uint32_t sync_tx_errors, sync_tx_enable;
static uint8_t buffer[4], *tx_usart_val = buffer;
static uint16_t dma_buffer_size = sizeof(buffer);
#define timer_start(t) ((void)0)
void serial_tx_poll(void);
void serial_sync_tx_stop(void);
static uint32_t timer1_window_start, timer1_window_end, timer1_sync_open_ticks;
static bool timer1_window_pending, timer1_window_armed, timer1_window_finished;
static bool timer1_sync_acquiring, timer1_sync_dma_armed, timer1_sync_irq_armed;
static bool synchronized, timer1_master_mode, timer1_sync_gate_closing;
bool timer1_sync_enabled(void) { return synchronized; }
"""
CHECKS = r"""int main(void) {
    void *base = mmap((void *)0x40012000, 0x2000, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    assert(base != MAP_FAILED);
    assert(mmap((void *)0x40020000, 0x2000, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) != MAP_FAILED);
    assert(mmap((void *)0x40004000, 0x1000, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) != MAP_FAILED);
    assert(mmap((void *)0x48000000, 0x2000, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) != MAP_FAILED);
    assert(mmap((void *)0x40016000, 0x2000, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) != MAP_FAILED);
    synchronized = false;
    assert(timer1_tx_window_configure(10, 20) == -EAGAIN);
    synchronized = true; timer1_sync_open_ticks = 4970;
    assert(timer1_tx_window_configure(20, 10) == -EINVAL);
    assert(timer1_tx_window_configure(10, 498) == -EINVAL);
    assert(timer1_tx_window_configure(UINT32_MAX, 20) == -EINVAL);
    assert(timer1_tx_window_configure(10, 20) == 0);
    assert(TIM1->CCR1 == 200 && TIM1->CCR2 == 100 && TIM1->CCR3 == 100);
    assert((TIM1->CCER & TIM_CCER_CC2NE) && !(TIM1->CCER & TIM_CCER_CC2E));
    assert(!(TIM1->CCER & TIM_CCER_CC2NP));
    assert(LL_TIM_OC_GetMode(TIM1, LL_TIM_CHANNEL_CH2) == LL_TIM_OCMODE_COMBINED_PWM2);
    assert(LL_TIM_OC_GetMode(TIM1, LL_TIM_CHANNEL_CH1) == LL_TIM_OCMODE_COMBINED_PWM1);
    assert(!(TIM1->BDTR & TIM_BDTR_MOE));
    TIM1->CR1 |= TIM_CR1_CEN;
    TIM1->CNT = 90;
    assert(timer1_tx_window_arm() == -ETIME);
    assert(!(TIM1->BDTR & TIM_BDTR_MOE));
    TIM1->CNT = 10;
    assert(timer1_tx_window_arm() == 0);
    assert((TIM1->BDTR & TIM_BDTR_MOE) && (TIM1->DIER & TIM_DIER_CC3DE));
    assert(!timer1_tx_window_expired());
    TIM1->SR |= TIM_SR_CC1IF;
    assert(timer1_tx_window_expired());
    timer1_compare_disarm();
    assert(!(TIM1->BDTR & TIM_BDTR_MOE));
    assert(!(TIM1->DIER & TIM_DIER_CC3DE));
    timer1_sync_acquiring = true;
    assert(timer1_tx_window_arm() == 0);
    assert(timer1_window_pending && timer1_sync_dma_armed);
    assert(!(TIM1->BDTR & TIM_BDTR_MOE));
    timer1_compare_disarm();
    assert(!timer1_window_pending && !timer1_sync_dma_armed);
    assert(timer1_tx_window_configure(10, 0) == 0);
    assert(TIM1->CCR1 == 4970);
    /* A delayed first-byte transfer must be cancelled after the window.
     * An unfinished USART character must block rearming into the next slot.
     */
    timer1_sync_acquiring = false;
    TIM1->CNT = 10;
    USART3->ISR = USART_ISR_TC;
    assert(serial_sync_tx_prepare() == 0);
    assert(sync_tx_pending && LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_8));
    TIM1->SR |= TIM_SR_CC1IF;
    USART3->ISR = 0;
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_6);
    serial_tx_poll();
    assert(sync_tx_errors & RS485_ERROR_TX_DEADLINE);
    assert(!LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_8));
    assert(!LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_6));
    assert(!(TIM1->BDTR & TIM_BDTR_MOE));
    assert(serial_sync_tx_prepare() == -EBUSY);
    USART3->ISR = USART_ISR_TC;
    TIM1->CNT = 10;
    assert(serial_sync_tx_prepare() == 0);
    DMA1->ISR |= DMA_ISR_TCIF8 | DMA_ISR_TCIF6;
    serial_tx_poll();
    sync_tx_errors = 0;
    TIM1->SR |= TIM_SR_CC1IF;
    serial_tx_poll();
    assert(sync_tx_errors == 0); /* A completed frame is not a deadline error. */
    /* The lead opens the gate; the first post-reset tick closes it even
     * when no normal HRTIM callback has run. No second ZLI is installed.
     */
    timer1_master_mode = true;
    timer1_sync_open_ticks = 1970;
    TIM1->CCR4 = 1970;
    TIM1->CNT = 1970;
    LL_TIM_EnableIT_CC4(TIM1);
    timer1_sync_gate_event();
    assert(timer1_sync_gate_closing && TIM1->CCR4 == 1);
    assert((TIM1->SMCR & TIM_SMCR_SMS) == LL_TIM_SLAVEMODE_COMBINED_RESETTRIGGER);
    assert(LL_GPIO_GetPinMode(GPIOB, LL_GPIO_PIN_1) == LL_GPIO_MODE_ALTERNATE);
    TIM1->CNT = 1;
    TIM1->SR |= TIM_SR_TIF | TIM_SR_CC4IF;
    timer1_sync_gate_event();
    assert(!timer1_sync_gate_closing && TIM1->CCR4 == 1970);
    assert((TIM1->SMCR & TIM_SMCR_SMS) == LL_TIM_SLAVEMODE_DISABLED);
    assert(LL_GPIO_GetPinMode(GPIOB, LL_GPIO_PIN_1) == LL_GPIO_MODE_OUTPUT);
    assert(TIM1->SR & TIM_SR_TIF);
    assert(LL_TIM_IsEnabledIT_CC4(TIM1));
    timer1_master_mode = false;
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_1, LL_GPIO_MODE_INPUT);
    timer1_sync_gate_event();
    TIM1->CNT = 1;
    timer1_sync_gate_event();
    assert(LL_GPIO_GetPinMode(GPIOB, LL_GPIO_PIN_1) == LL_GPIO_MODE_INPUT);
    check_slave_init();
    assert(LL_HRTIM_GetSyncInSrc(HRTIM1) == LL_HRTIM_SYNCIN_SRC_EXTERNAL_EVENT);
    assert(LL_HRTIM_GetSyncOutSrc(HRTIM1) == LL_HRTIM_SYNCOUT_SRC_TIMA_START);
    assert(LL_HRTIM_TIM_IsEnabledResetOnSync(HRTIM1, LL_HRTIM_TIMER_MASTER));
    assert(LL_HRTIM_TIM_IsEnabledStartOnSync(HRTIM1, LL_HRTIM_TIMER_MASTER));
    assert(LL_HRTIM_TIM_GetResetTrig(HRTIM1, LL_HRTIM_TIMER_A) == LL_HRTIM_RESETTRIG_NONE);
    assert(LL_HRTIM_TIM_IsEnabledResetOnSync(HRTIM1, LL_HRTIM_TIMER_A));
    assert(LL_HRTIM_TIM_IsEnabledStartOnSync(HRTIM1, LL_HRTIM_TIMER_A));
    assert(LL_GPIO_GetPinMode(GPIOB, LL_GPIO_PIN_1) == LL_GPIO_MODE_INPUT);
    assert(LL_GPIO_GetPinMode(GPIOB, LL_GPIO_PIN_2) == LL_GPIO_MODE_ALTERNATE);
    puts("TIM1 window, sync gate and slave trigger register checks passed");
}
"""

def main():
    source = (ROOT / "zephyr/modules/owntech_timer_driver/zephyr/src/stm32_timer_driver.c").read_text()
    start = source.index("static void timer1_window_close(")
    end = source.index("\nbool timer1_sync_enabled", start)
    functions = source[start:end]
    start = source.index("void timer1_compare_disarm(")
    end = source.index("\nstatic void timer1_sync_event", start)
    functions += source[start:end]
    start = source.index("static void timer1_sync_gate_event(")
    end = source.index("\nstatic void timer_stm32_callback", start)
    functions += source[start:end]
    sync = (ROOT / "zephyr/modules/owntech_communication/zephyr/src/SyncCommunication.cpp").read_text()
    functions += sync[sync.index("void SyncCommunication::initSlave()") :].replace("void SyncCommunication::initSlave()", "void check_slave_init(void)")
    rs485 = (ROOT / "zephyr/modules/owntech_communication/zephyr/src/Rs485.cpp").read_text()
    for signature in ("int serial_sync_tx_prepare()", "void serial_sync_tx_stop()", "void serial_tx_poll()"):
        start = rs485.index(signature)
        opening = rs485.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (rs485[end] == "{") - (rs485[end] == "}")
            end += 1
        functions += rs485[start:end] + "\n"
    with tempfile.TemporaryDirectory(prefix="tim1-register-test-") as directory:
        c_file = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        c_file.write_text(PREFIX + functions + CHECKS)
        command = [os.environ.get("CC", "gcc"), "-D_GNU_SOURCE", "-Wno-overflow",
                   "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast"]
        command += ["-I" + str(path) for path in INCLUDES]
        subprocess.run(command + [str(c_file), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    main()
