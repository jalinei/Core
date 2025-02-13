/*
 * Copyright (c) 2021-present LAAS-CNRS
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1
 */

/**
 * @brief  This examples shows how to use SpinAPI to define fast and precise
 *         PWM signals.
 *
 * @author Clément Foucher <clement.foucher@laas.fr>
 * @author Luiz Villa <luiz.villa@laas.fr>
 * @author Ayoub Farah Hassan <ayoub.farah-hassan@laas.fr>
 */

/* --------------Zephyr---------------------------------------- */
#include <zephyr/console/console.h>

/* --------------OWNTECH APIs---------------------------------- */
#include "SpinAPI.h"
#include "TaskAPI.h"
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(vesc_usb, LOG_LEVEL_INF);

/* VESC Protocol Constants */
#define VESC_START_BYTE      0x02
#define VESC_END_BYTE        0x03
#define COMM_FW_VERSION      0x00
#define VESC_MAX_PAYLOAD     256
#define VESC_HEADER_SIZE     2  // START + LEN
#define VESC_FOOTER_SIZE     3  // CRC_H + CRC_L + END

// Hardware-specific definitions (from your configuration)
#define HW_NAME "A100S_V4"
#define FW_NAME ""
#define HW_TYPE_VESC 0
#define STM32_UUID_8 ((uint8_t*)0x1FFF7A10)  // STM32 unique ID address
#define COMM_FW_VERSION 0x00
#define VESC_START_BYTE 0x02
#define VESC_END_BYTE 0x03
#define HW_TYPE_CUSTOM_MODULE 2
#define FW_TEST_VERSION_NUMBER 0

/* USB CDC ACM Configuration */
static const struct device *cdc_acm_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_cdc_acm_uart));
static uint8_t uart_rx_buf[128];
static bool rx_throttled;

/* Application State */
static uint8_t app_rx_buffer[VESC_MAX_PAYLOAD + VESC_HEADER_SIZE + VESC_FOOTER_SIZE];
static volatile size_t rx_index;
static volatile bool msg_started;
static volatile uint8_t expected_len;

/* Ring Buffer for TX */
#define RING_BUF_SIZE 1024
static uint8_t ring_buffer[RING_BUF_SIZE];
static struct ring_buf tx_ringbuf;

void vesc_send_fw_version(uint8_t major, uint8_t minor) {
    uint8_t payload[64];
    int payload_ind = 0;

    // 1. Command ID
    payload[payload_ind++] = COMM_FW_VERSION;

    // 2. Firmware version
    payload[payload_ind++] = major;
    payload[payload_ind++] = minor;

    // 3. Hardware name (null-terminated)
    strcpy((char*)payload + payload_ind, HW_NAME);
    payload_ind += strlen(HW_NAME) + 1;

    // 4. STM32 UUID (12 bytes from hardware address)
    memcpy(payload + payload_ind, STM32_UUID_8, 12);
    payload_ind += 12;

    // Optional: Adjust UUID for second motor if needed
    // if (mc_interface_get_motor_thread() == 2) {
    //     payload[payload_ind - 1]++;
    // }

    // 5. Pairing done (0 for minimal FW)
    payload[payload_ind++] = true;

    // 6. Test version number
    payload[payload_ind++] = FW_TEST_VERSION_NUMBER;

    // 7. Hardware type
    payload[payload_ind++] = HW_TYPE_VESC;

    // 8. Custom configuration number (0)
    payload[payload_ind++] = 0; // Replace if conf_custom_cfg_num() is available

    // 9. Phase filters
#ifdef HW_HAS_PHASE_FILTERS
    payload[payload_ind++] = 1;
#else
    payload[payload_ind++] = 0;
#endif

    // 10. QMLUI HW flags
#ifdef QMLUI_SOURCE_HW
#ifdef QMLUI_HW_FULLSCREEN
    payload[payload_ind++] = 2;
#else
    payload[payload_ind++] = 1;
#endif
#else
    payload[payload_ind++] = 0;
#endif

    // 11. QMLUI APP flags
#ifdef QMLUI_SOURCE_APP
#ifdef QMLUI_APP_FULLSCREEN
    payload[payload_ind++] = 2;
#else
    payload[payload_ind++] = 1;
#endif
#else
    payload[payload_ind++] = 0; // Assuming no flash helper code
#endif

    // 12. NRF flags (0)
    payload[payload_ind++] = 0;

    // 13. Firmware name (null-terminated)
    strcpy((char*)payload + payload_ind, FW_NAME);
    payload_ind += strlen(FW_NAME) + 1;

    // 14. 32-bit HW CRC (replace with actual calculation if available)
    uint32_t hw_crc = 0; // Use main_calc_hw_crc() if implemented
    payload[payload_ind++] = (hw_crc >> 24) & 0xFF;
    payload[payload_ind++] = (hw_crc >> 16) & 0xFF;
    payload[payload_ind++] = (hw_crc >> 8) & 0xFF;
    payload[payload_ind++] = hw_crc & 0xFF;

    // Build packet (VESC protocol framing)
    uint8_t packet[64 + 5];  // Payload + header/CRC/stop
    int packet_ind = 0;

    // Start byte and length
    packet[packet_ind++] = VESC_START_BYTE;
    packet[packet_ind++] = payload_ind;  // Single byte length

    // Copy payload
    memcpy(packet + packet_ind, payload, payload_ind);
    packet_ind += payload_ind;

    // Calculate CRC16-CCITT (polynomial 0x1021)
    uint16_t crc = crc16_ccitt(0, payload, payload_ind);
    packet[packet_ind++] = (crc >> 8) & 0xFF;
    packet[packet_ind++] = crc & 0xFF;
    packet[packet_ind++] = VESC_END_BYTE;

    // Send via UART
    int rb_len = ring_buf_put(&tx_ringbuf, packet, packet_ind);
    if (rb_len < packet_ind) {
        LOG_ERR("TX buffer overflow");
    }

    if (rb_len > 0) {
        uart_irq_tx_enable(cdc_acm_dev);
    }
}

static void process_vesc_message(void)
{
    /* Verify end byte */
    if (app_rx_buffer[rx_index-1] != VESC_END_BYTE) {
        LOG_ERR("Invalid end byte");
        return;
    }

    /* Verify CRC */
    uint16_t received_crc = (app_rx_buffer[rx_index-3] << 8) | app_rx_buffer[rx_index-2];
    uint16_t calculated_crc = crc16_ccitt(0, &app_rx_buffer[2], expected_len);

    if (received_crc != calculated_crc) {
        LOG_ERR("CRC mismatch");
        return;
    }

    /* Handle command */
    uint8_t command = app_rx_buffer[2];
    if (command == COMM_FW_VERSION) {
        LOG_INF("Valid FW version request");
        vesc_send_fw_version(6, 5);
    }
}

static void uart_interrupt_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t buffer[64];
            int recv_len;

            /* Read from UART FIFO */
            recv_len = uart_fifo_read(dev, buffer, sizeof(buffer));
            if (recv_len <= 0) continue;

            /* Process received bytes */
            for (int i = 0; i < recv_len; i++) {
                uint8_t c = buffer[i];

                if (!msg_started && c == VESC_START_BYTE) {
                    msg_started = true;
                    rx_index = 0;
                    app_rx_buffer[rx_index++] = c;
                    continue;
                }

                if (msg_started) {
                    app_rx_buffer[rx_index++] = c;

                    /* Get payload length */
                    if (rx_index == 2) {
                        expected_len = app_rx_buffer[1];
                        if (expected_len > VESC_MAX_PAYLOAD) {
                            msg_started = false;
                            LOG_ERR("Invalid length");
                        }
                    }

                    /* Check message completeness */
                    if (rx_index >= (VESC_HEADER_SIZE + expected_len + VESC_FOOTER_SIZE)) {
                        process_vesc_message();
                        msg_started = false;
                    }

                    /* Overflow protection */
                    if (rx_index >= sizeof(app_rx_buffer)) {
                        msg_started = false;
                        LOG_ERR("Buffer overflow");
                    }
                }
            }
        }

        if (uart_irq_tx_ready(dev)) {
            uint8_t buffer[64];
            int rb_len, send_len;

            /* Get data from ring buffer */
            rb_len = ring_buf_get(&tx_ringbuf, buffer, sizeof(buffer));
            if (rb_len > 0) {
                send_len = uart_fifo_fill(dev, buffer, rb_len);
                if (send_len < rb_len) {
                    LOG_ERR("TX data loss: %d", rb_len - send_len);
                }
            }

            /* Disable TX if buffer empty */
            if (ring_buf_is_empty(&tx_ringbuf)) {
                uart_irq_tx_disable(dev);
            }
        }
    }
}


/* --------------SETUP FUNCTIONS DECLARATION------------------- */
/* Setups the hardware and software of the system */
void setup_routine();

/* --------------LOOP FUNCTIONS DECLARATION-------------------- */
/* Code to be executed in the slow communication task */
void loop_communication_task();
/* Code to be executed in the background task */
void loop_application_task();
/* Code to be executed in real time in the critical task  */
void loop_critical_task();

/* --------------USER VARIABLES DECLARATIONS------------------- */
uint8_t received_serial_char;

float32_t duty_cycle = 0.3;

/* --------------------------------------------------------------- */

/* List of possible modes for the OwnTech Board. */
enum serial_interface_menu_mode
{
    IDLEMODE = 0,
    POWERMODE
};

uint8_t mode = IDLEMODE;

/* --------------SETUP FUNCTIONS------------------------------- */

/**
 * This is the setup routine.
 * Here we define a simple PWM signal on PWMA and we spawn three tasks.
 */
void setup_routine()
{
    /* Set frequency of PWM */
    spin.pwm.initFixedFrequency(200000);
    /* Timer initialization */
    spin.pwm.initUnit(PWMA);

    /* Start PWM */
    spin.pwm.startDualOutput(PWMA);

    /* Then we declare tasks */
    uint32_t app_task_number = task.createBackground(loop_application_task);
    uint32_t com_task_number = task.createBackground(loop_communication_task);
    task.createCritical(loop_critical_task, 100);

    /* Finally, we start tasks */
    task.startBackground(app_task_number);
    task.startBackground(com_task_number);
    task.startCritical();
}

/* --------------LOOP FUNCTIONS-------------------------------- */

/**
 * Here we implement a minimalistic USB serial interface.
 * Duty cycle can be controlled by pressing U and D keys.
 * This will respectively Increase or Decrease PWM duty cycle.
 */
void loop_communication_task()
{
    task.suspendBackgroundMs(1000);
}

/**
 * This is the code loop of the background task
 * Here the task is sending the duty cycle that have been set
 * on the USB serial console every second.
 */
void loop_application_task()
{
    /* Task content */
    // printk("%f\n", (double)duty_cycle);

    /* Pause between two runs of the task */
    task.suspendBackgroundMs(1000);

}

/**
 * This is the code loop of the critical task
 * In this task than runs periodically in Real Time at 10kHz, we simply
 * update the duty cycle provided through the serial communication.
 */
void loop_critical_task()
{
    spin.pwm.setDutyCycle(PWMA, duty_cycle);
}

/**
 * This is the main function of this example
 * This function is generic and does not need editing.
 */
int main(void)
{
    if (!device_is_ready(cdc_acm_dev)) {
        LOG_ERR("CDC ACM device not ready");
    }

    /* Initialize ring buffer */
    ring_buf_init(&tx_ringbuf, sizeof(ring_buffer), ring_buffer);

    setup_routine();
    // Configure UART callback and enable RX
    /* Setup UART interrupt handler */
    uart_irq_callback_set(cdc_acm_dev, uart_interrupt_handler);
    uart_irq_rx_enable(cdc_acm_dev);

    return 0;
}
