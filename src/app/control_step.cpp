/*
 *
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
 * @file control_step.cpp
 * @brief Hardware-free control logic of the microinverter critical task.
 *
 * Moved unchanged from loop_critical_task() and setup_routine() in main.cpp;
 * see docs/testing/worklog.md.
 */
#include "app/control_step.h"

#include <math.h>

int8_t controlInit(ControlState &state)
{
    if (state.boost.init(IDLE, app::initial_voltage_reference, app::boost_voltage_params,
                         app::boost_current_params, app::leg1_current_limit) != 0 ||
        state.inverter.init(app::initial_voltage_reference, app::grid_voltage_peak,
                            app::grid_angular_frequency, app::hot_loop_period) != 0 ||
        !state.grid_observer.init(app::grid_observer_config))
    {
        return -1;
    }
    state.inverter.setGridQuality(state.grid_observer.canOperate());
    /* Zero dwell holds synchronized PWM indefinitely without requesting relay closure. */
    state.inverter.setPwmSettleCycles(0);
    state.boost.setStartupCurrentMax(app::startup_current_max);
    state.boost.setVBusRef(app::initial_voltage_reference);
    if (state.boost.configureRippleControl(app::boost_ripple_frequency, app::boost_notch_bandwidth,
                                          app::boost_resonant_gain, app::boost_resonant_phase) != 0 ||
        state.boost.setBurst(app::boost_burst_period) != 0 ||
        state.boost.setProtection(app::boost_full_burst_voltage, app::boost_trip_voltage,
                                  app::boost_trip_current) != 0 ||
        state.boost.setVoltageGainSchedule(app::boost_voltage_gain,
                                           app::boost_schedule_vin_min, app::boost_schedule_vin_max,
                                           app::boost_schedule_vbus_min, app::boost_schedule_vbus_max) != 0)
    {
        return -2;
    }
    return 0;
}

ControlOutputs controlStep(ControlState &state, const Measurements &meas,
                           const OperatorCommands &cmd)
{
    ControlOutputs out;

    if (state.grid_voltage_age_ticks < app::grid_voltage_stale_ticks) ++state.grid_voltage_age_ticks;
    if (meas.v_ac_updated) state.grid_voltage_age_ticks = 0U;

    state.boost.enableVoltageNotch(cmd.voltage_notch_enabled);
    state.boost.enableCurrentResonant(cmd.current_resonant_enabled);
    state.boost.setILow1(meas.i_low_1);
    state.boost.setILow2(meas.i_low_2);
    state.boost.setVLow(meas.v_low);
    state.boost.setVBus(meas.v_dc_bus);
    state.boost.setVBusRef(cmd.v_bus_ref);

    const boost_mode requested_mode = cmd.mode;
    const bool pwm_settle_requested = cmd.sync_requested;
    if (requested_mode != state.previous_mode)
    {
        out.boost_mode_changed = true;
        state.boost.setMode(requested_mode);
        // Run the new mode's cold loop on this tick, using the measurements above.
        state.cold_loop_tick = app::cold_loop_divider - 1U;
        state.previous_mode = requested_mode;
    }

    if (++state.cold_loop_tick >= app::cold_loop_divider)
    {
        state.cold_loop_tick = 0U;
        state.boost.coldLoop();
    }
    out.boost_duty = state.boost.hotLoop();

    /* Keep the PLL running in idle, using a positive bus value for duty scaling. */
    if (pwm_settle_requested || (isfinite(meas.v_dc_bus) && meas.v_dc_bus > 0.0F))
    {
        state.inverter.setVBus(meas.v_dc_bus);
    }
    if (pwm_settle_requested) state.inverter.requestConnect();
    else state.inverter.requestDisconnect();
    /* PWM start-up acknowledgement (single_phase_inverter PWM_WAIT/PWM_SETTLE): report
     * whether the hardware loop actually applied bridge PWM last period. */
    state.inverter.setPwmEnabled(state.bridge_pwm_applied);
    out.bridge_duty = state.inverter.calculateDuty(meas.v_ac, meas.i_ac);
    const clarke_t grid_voltage_ab = state.inverter.getVab();
    state.grid_observer.update(grid_voltage_ab.alpha, grid_voltage_ab.beta, state.inverter.getw(),
                               state.grid_voltage_age_ticks < app::grid_voltage_stale_ticks);
    /* Apply this tick's quality verdict before accepting any PWM request. */
    state.inverter.setGridQuality(state.grid_observer.canOperate());
    out.grid_angle = state.inverter.getTheta();

    out.boost_pwm_update = requested_mode != IDLE;
    out.leg1_enable = state.boost.isLeg1Enabled();
    out.leg2_enable = state.boost.isLeg2Enabled();
    out.burst_skip = state.boost.getBurstSkip();
    out.burst_period = state.boost.getBurstPeriod();
    out.boost_trip = state.boost.isTripped();
    /* PWM_SETTLE has zero dwell: PWM runs, but relay closure stays inhibited. */
    out.bridge_enable = state.inverter.isPwmRequested();
    return out;
}
