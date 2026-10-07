/*
 * Copyright (c) 2024-present LAAS-CNRS
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

/*
 * @date   2024
 *
 * @author Ayoub Farah Hassan <ayoub.farah-hassan@laas.fr>
 */

#ifndef RS485COMMUNICATION_H_
#define RS485COMMUNICATION_H_

#include <stdint.h>

#ifdef CONFIG_OWNTECH_COMMUNICATION_ENABLE_RS485


/**
 * @brief Defines the possible speeds for the RS485: 
 *        
 * - `SPEED_2M`: 2Mbits/s speed communication
 *        
 * - `SPEED_5M`: 5Mbits/s speed communication
 * 
 * - `SPEED_10M`: 10Mbits/s speed communication
 * 
 * - `SPEED_20M`: 20Mbits/s speed communication
 *
 */
typedef enum {
    SPEED_2M,  
    SPEED_5M,  
    SPEED_10M, 
    SPEED_20M, 
}rs485_speed_t;

/**
 * Static class definition
 */

class Rs485Communication
{
    public :
        /**
         * @brief Configuration for RS485 communication using a 10Mbit/s speed
         *
         * @param transmission_buffer Pointer to the transmitted buffer
         * @param reception_buffer Pointer to the received buffer
         * @param data_size Size of the sent and received data (in byte)
         * @param user_function Callback function called when we received data
         * @param data_speed Transmission speed (by default to 10Mbits/s)
         *                  `SPEED_2M`,`SPEED_5M`,`SPEED_10M`,`SPEED_20M`
         *
         * @warning The size of transmission_buffer and reception_buffer
         *          must be the same
         */
        void configure(uint8_t *transmission_buffer,
                       uint8_t *reception_buffer,
                       uint16_t data_size, void (*user_function)(),
                       rs485_speed_t data_speed = SPEED_10M);

        /**
         * @brief Custom configuration for RS485 communication
         *        to choose the communication speed.
         *
         * @param transmission_buffer Pointer to the transmitted buffer
         * @param reception_buffer Pointer to the received buffer
         * @param data_size Size of the sent and received data (in byte)
         * @param user_function Callback function called when we received data
         * @param baudrate Communication speed in bit/s
         * @param oversampling_8 True for oversampling
         *                       (and multiply communication speed by 2),
         *                       False if you want to keep
         *                       the normal speed communication
         *
         * @warning The size of transmission_buffer
         *          and reception_buffer must be the same
         */
        void configureCustom(uint8_t *transmission_buffer,
                             uint8_t *reception_buffer,
                             uint16_t data_size, void (*user_function)(void),
                             uint32_t baudrate, bool oversampling_8);

        /**
         * @brief Start a transmission i.e. you send
         *        what is contained in the transmission buffer
         */
        void startTransmission();

        /** @brief Configure TX delay after a sync/control boundary.
         * Call configure() and initSlave() or initMaster() first.
         * For master mode, create the HRTIM critical task before this call.
         * Its delay must be less than the control period minus 1 us.
         * Uses TIM2 CH1 and DMA2 channel 1. Returns 0 or a negative errno.
         */
        int configureSynchronizedTransmission(uint32_t delay_us);

        /** @brief Arm one message for the next TIM2 CH1 compare.
         * Returns -EBUSY while a trigger/TX is pending. The configured buffer
         * must remain unchanged until TX completes. Rearm for each message.
         * The TIM2 event starts DMA without a CPU interrupt; TX completion
         * still uses the existing DMA completion interrupt.
         */
        int prepareSynchronizedTransmission();

        /** @brief Cancel a pending TX trigger without aborting an active TX.
         * In master mode, SCOUT timing continues independently of TX arming.
         */
        void stopSynchronizedTransmission();

        /**
         * @brief Turn on the RS485 communication
         *
         * @warning The RS485 is automatically turned on
         *          when initializing with configureDefault or configure,
         *          no need to call this function
         */
        void turnOnCommunication();

        /**
         * @brief Turn off the RS485
         */
        void turnOffCommunication();
};

#endif /* CONFIG_OWNTECH_COMMUNICATION_ENABLE_RS485 */

#endif /* RS485COMMUNICATION_H_ */
