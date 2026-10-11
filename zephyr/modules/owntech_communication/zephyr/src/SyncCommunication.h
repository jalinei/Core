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
 * @author Luiz Villa <luiz.villa@laas.fr>
 * @author Ayoub Farah Hassan <ayoub.farah-hassan@laas.fr>
 */

#ifndef SYNCCOMMUNICATION_H_
#define SYNCCOMMUNICATION_H_

#ifdef CONFIG_OWNTECH_COMMUNICATION_ENABLE_SYNC

#include <stdint.h>

/* OWNTECH API */
#include "SpinAPI.h"


/**
 *  Static class definition
 */

class SyncCommunication
{

public:

	/**
	 * @brief Initialization synchronization as `MASTER`,
	 * 		  the master send the synchronization pulse.
	 * Initialize PWM Timer A first: it must reset on the master period event.
	 * Then create/start an HRTIM critical task. TIM1 CH4 opens SCOUT and ITR10
	 * 3 us before each boundary and closes them one tick after reset from the
	 * same ZLI. The first repetition ISR
	 * arms hardware phase acquisition with SCOUT closed and TX deferred; the
	 * next ISR ends acquisition. CH4 uses a zero-latency ISR and can preempt
	 * the control task. It must execute within its 3 us lead; other zero-latency
	 * ISRs can still delay it. A missed trigger reacquires phase.
	 */
	static void initMaster();

	/**
	 * @brief Initialization synchronization as `SLAVE`,
	 * 		  the slave receive the synchronization pulse.
	 * Initialize PWM Timer A first at zero phase, in continuous mode. Slave
	 * initialization replaces its MASTER_PER reset trigger with reset-on-SCIN. The HRTIM master waits for SCIN
	 * and resets on each pulse. Timer A also resets on SCIN and relays that event
	 * through SYNCOUT to ITR10, with PB1 high impedance. Then create/start an HRTIM
	 * critical task: TIM1 keeps counting, opens ITR10 from a ZLI 3 us before
	 * each control boundary, and closes it one tick after reset from the same
	 * ZLI. The first SCIN callback arms hardware acquisition;
	 * a subsequent callback with an accepted ITR10 trigger ends acquisition.
	 * Other ZLIs may delay CH4; a missed trigger causes reacquisition.
	 */
	static void initSlave();
};

#endif /* CONFIG_OWNTECH_COMMUNICATION_ENABLE_SYNC */

#endif /* SYNCCOMMUNICATION_H_ */
