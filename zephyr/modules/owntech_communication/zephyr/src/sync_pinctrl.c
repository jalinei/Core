/*
 * Copyright (c) 2023-2024 LAAS-CNRS
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
 * @date   2024
 *
 * @author Jean Alinei <jean.alinei@owntech.org>
 */


#include <zephyr/kernel.h>
#include <zephyr/drivers/pinctrl.h>
#include "sync_pinctrl.h"

#define SYNC_IN_NODE DT_NODELABEL(scin)
#define SYNC_OUT_NODE DT_NODELABEL(scout)

PINCTRL_DT_DEFINE(SYNC_IN_NODE);
PINCTRL_DT_DEFINE(SYNC_OUT_NODE);

const struct pinctrl_dev_config *pincfg_scin = PINCTRL_DT_DEV_CONFIG_GET(SYNC_IN_NODE);
const struct pinctrl_dev_config *pincfg_scout = PINCTRL_DT_DEV_CONFIG_GET(SYNC_OUT_NODE);

int __init_pin_master()
{
    return pinctrl_apply_state(pincfg_scout, PINCTRL_STATE_DEFAULT);
}

int __init_pin_slave()
{
    return pinctrl_apply_state(pincfg_scin, PINCTRL_STATE_DEFAULT);
}