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
 * @brief  Boost converter and single-phase inverter control on the uSolarVerter shield.
 *
 * @author Clément Foucher <clement.foucher@laas.fr>
 * @author Luiz Villa <luiz.villa@laas.fr>
 * @author Ayoub Farah Hassan <ayoub.farah-hassan@laas.fr>
 */

/*--------------Zephyr---------------------------------------- */
#include <zephyr/console/console.h>

/*--------------CMSIS----------------------------------------- */
#include <arm_math.h>

/*--------------OWNTECH APIs---------------------------------- */
#include "SpinAPI.h"
#include "ShieldAPI.h"
#include "TaskAPI.h"

/*--------------OWNTECH Libraries----------------------------- */
#include "boost_converter/boost.h"
#include "singlePhaseInverterFollowing.h"
#include "grid_observer/gridObserver.h"
#include <math.h>
#include "ScopeMimicry.h"

/*--------------SETUP FUNCTIONS DECLARATION------------------- */
/* Setups the hardware and software of the system */
void setup_routine();

/*--------------LOOP FUNCTIONS DECLARATION-------------------- */
/* Code to be executed in the slow communication task */
void loop_communication_task();
/* Code to be executed in the background task */
void loop_application_task();
/* Code to be executed in real time in the critical task */
void loop_critical_task();

/*--------------USER VARIABLES DECLARATIONS------------------- */

/* [us] period of the control task */
static constexpr uint32_t control_task_period = 100;
/* Run the boost voltage/startup or MPPT cold loop every ten critical ticks. */
static constexpr uint8_t cold_loop_divider = 10U;
static uint8_t cold_loop_tick;

uint8_t received_serial_char;

/* Measure variables */

static float32_t v_low_value;
static float32_t v_ac_value;
static float32_t v_ac_rms_value;
static float32_t v_dc_bus_value;
static float32_t i_low_1_value;
static float32_t i_low_2_value;
static float32_t i_ac_value;

/* Temporary storage fore measured value (ctrl task) */
static float meas_data;

static constexpr float32_t duty_cycle_min = 0.05F;
static BoostDutyCycles boost_duty;
static uint16_t leg1_low_deadtime_ns = 100;
static uint16_t leg2_low_deadtime_ns = 100;
static constexpr uint16_t deadtime_step_ns = 10;
static constexpr uint16_t deadtime_min_ns = 40;

/* Controller timing and initial tuning; validate gains/current limits on hardware. */
static constexpr float32_t hot_loop_period = control_task_period * 1e-6F;
static constexpr float32_t cold_loop_period = hot_loop_period * cold_loop_divider;
/* Shared boost output capacitor feeding the DC/AC stage (VDCBus). */
static float32_t voltage_reference = 400.0F;
static constexpr float32_t v_dc_bus_burst_resume = 395.0F;
static constexpr float32_t v_dc_bus_burst_pause = 405.0F;
static constexpr float32_t startup_ramp_rate = 10.0F; // V/s.
static constexpr float32_t startup_current_max = 1.0F; // A, converter 1 only.
static constexpr float32_t leg1_current_limit = 1.0F;
static const PidParams boost_voltage_params = {
    cold_loop_period, 0.05F, 0.1F, 0.0F, 0.0F, 0.0F, 2.0F
};
static const PidParams boost_current_params = {
    hot_loop_period, 0.000215F, 7.5175e-5F, 0.0F, 0.0F, duty_cycle_min, 0.95F
};
static BoostConverter boost_converter;
static singlePhaseInverterFollowing inverter;
static GridObserver grid_observer;

/* Neutral-to-waveform handover window, and settling periods before relay closure.
 * A settling count of 0 holds PWM_SETTLE indefinitely.
 * Used when a connection is requested; the current application is sync-only. */
static constexpr float32_t inverter_pwm_start_duty_tolerance = 0.01F;
static constexpr uint16_t inverter_pwm_settle_cycles = 10U;
static constexpr float32_t grid_frequency_hz = 50.0F;
static constexpr float32_t grid_angular_frequency = 2.0F * PI * grid_frequency_hz;
static constexpr float32_t grid_voltage_peak = 230.0F * 1.41421356F;
/* Initial engineering settings, not a grid-code protection profile. Voltage and
 * frequency windows match the VSI's existing synchronization tolerances.
 * Validate thresholds, delays and SOGI/PLL response before grid operation.
 */
static const GridObserverConfig grid_observer_config = {
    hot_loop_period,
    230.0F * 0.9F, 230.0F * 1.1F,
    grid_frequency_hz * 0.95F, grid_frequency_hz * 1.05F,
    0.1F, 0.1F, 1.0F
};
/* Invalidate the observer if no new VAC sample arrives for 20 ms. */
static constexpr uint32_t grid_voltage_stale_ticks = 200U;
static uint32_t grid_voltage_age_ticks = grid_voltage_stale_ticks;
/* One nominal 50 Hz period at the 10 kHz critical task rate. */
static constexpr uint32_t v_ac_rms_window_sample_count = 200U;
static float32_t grid_angle;
static float32_t v_ac_rms_square_sum;
static uint32_t v_ac_rms_sample_count;

/* ScopeMimicry */
static constexpr uint16_t scope_buffer_sample_count = 1024U;
static constexpr uint16_t scope_channel_count = 2U;
static ScopeMimicry scope(scope_buffer_sample_count, scope_channel_count);
static bool is_scope_downloading;

/*--------------------------------------------------------------- */

/* Mode requests are applied to the controllers by the critical task. */
static boost_mode mode = IDLE;
static bool inverter_sync_requested;

bool scope_trigger()
{
    return true;
}

void dump_scope_datas(ScopeMimicry &scope)
{
    printk("begin record\n");
    scope.reset_dump();
    while (scope.get_dump_state() != finished)
    {
        printk("%s", scope.dump_datas());
        task.suspendBackgroundUs(200);
    }
    printk("end record\n");
}

/*--------------SETUP FUNCTIONS------------------------------- */

/**
 * This is the setup routine.
 * Here the setup :
 *  - Initializes the boost legs and D/E inverter
 *  - Initializes the power shield sensors
 *  - Initializes the boost and inverter controllers and scope
 *  - Spawns three tasks.
 */
void setup_routine()
{
    /* DC/DC boost legs: A and C. */
    shield.power.initBoost(LEG1_LOW);
    shield.power.initBoost(LEG2_LOW);
    shield.power.setDeadTime(LEG1_LOW, leg2_low_deadtime_ns, leg2_low_deadtime_ns);
    shield.power.setDeadTime(LEG2_LOW, leg2_low_deadtime_ns, leg2_low_deadtime_ns);
    /* Unipolar DC/AC bridge: channels D and E, initially at neutral duty. */
    if (shield.power.initInverterUnipolar(LEG1_HIGH, LEG2_HIGH) != 0)
    {
        printk("inverter initialization failed\n");
        return;
    }
    shield.power.setDutyCycleMin(ALL, duty_cycle_min);
    shield.sensors.enableSensor(VLow, ADC_1);
    shield.sensors.enableSensor(VAC, ADC_1);
    shield.sensors.enableSensor(VDCBus, ADC_1);
    shield.sensors.enableSensor(ILow1, ADC_2);
    shield.sensors.enableSensor(ILow2, ADC_2);
    shield.sensors.enableSensor(IAC, ADC_2);

    spin.gpio.configurePin(22, OUTPUT);
    if (boost_converter.init(IDLE, voltage_reference, boost_voltage_params,
                   boost_current_params, leg1_current_limit) != 0 ||
        inverter.init(voltage_reference, grid_voltage_peak,
                      grid_angular_frequency, hot_loop_period) != 0 ||
        !grid_observer.init(grid_observer_config))
    {
        printk("controller initialization failed\n");
        return;
    }
    inverter.setPwmStartDutyTolerance(inverter_pwm_start_duty_tolerance);
    inverter.setPwmSettleCycles(inverter_pwm_settle_cycles);
    inverter.setGridQuality(grid_observer.canOperate());
    boost_converter.setStartupRampRate(startup_ramp_rate);
    boost_converter.setStartupCurrentMax(startup_current_max);
    boost_converter.setVBusRef(voltage_reference);
    if (boost_converter.setVBusBurstBounds(v_dc_bus_burst_resume, v_dc_bus_burst_pause) != 0)
    {
        printk("invalid VDCBus burst boundaries\n");
        return;
    }
    scope.connectChannel(v_ac_value, "VAC");
    scope.connectChannel(grid_angle, "grid_angle");
    scope.set_trigger(&scope_trigger);
    scope.set_delay(0.0F);
    scope.start();

    /* Then declare tasks */
    uint32_t app_task_number = task.createBackground(loop_application_task);
    uint32_t com_task_number = task.createBackground(loop_communication_task);
    task.createCritical(loop_critical_task, control_task_period);

    /* Finally, start tasks */
    task.startBackground(app_task_number);
    task.startBackground(com_task_number);
    task.startCritical();
}

/*--------------LOOP FUNCTIONS-------------------------------- */

/**
 * This tasks implements a minimalistic USB serial interface to control
 * the boost converter and inverter.
 */
void loop_communication_task()
{
    received_serial_char = console_getchar();
    switch (received_serial_char)
    {
    case 'h':
        /*----------SERIAL INTERFACE MENU----------------------- */
        printk(" ________________________________________ \n"
               "|     ---- MENU microinverter ----       |\n"
               "|     press i : idle mode                |\n"
               "|     press p : startup voltage ramp     |\n"
               "|     press b : bus regulation mode      |\n"
               "|     press m : MPPT mode                |\n"
               "|     press c : inverter sync only       |\n"
               "|     press u : voltage reference UP     |\n"
               "|     press d : voltage reference DOWN   |\n"
               "|     press w : deadtime UP              |\n"
               "|     press s : deadtime DOWN            |\n"
               "|     press r : retrieve scope data      |\n"
               "|     press q : restart scope acquisition|\n"
               "|________________________________________|\n\n");
        /*------------------------------------------------------ */
        break;
    case 'i':
        printk("idle mode\n");
        mode = IDLE;
        inverter_sync_requested = false;
        break;
    case 'p':
        printk("startup: converter 1 voltage ramp\n");
        mode = STARTUP;
        inverter_sync_requested = false;
        break;
    case 'b':
        printk("VDCBus regulation mode\n");
        mode = BUSREG;
        inverter_sync_requested = false;
        break;
    case 'm':
        printk("MPPT current mode\n");
        mode = MPPT;
        inverter_sync_requested = false;
        break;
    case 'c':
        printk("inverter synchronization only requested\n");
        inverter_sync_requested = true;
        break;
    case 'u':
        // Keep the bus target below the configured burst cutoff.
        if (voltage_reference + 1.0F < v_dc_bus_burst_pause) voltage_reference += 1.0F;
        break;
    case 'd':
        voltage_reference -= 1.0F;
        if (voltage_reference < 1.0F) voltage_reference = 1.0F;
        break;
    case 'w':
        leg2_low_deadtime_ns += deadtime_step_ns;
        shield.power.setDeadTime(LEG2_LOW, leg2_low_deadtime_ns, leg2_low_deadtime_ns);
        printk("LEG2_LOW deadtime: %u ns\n", leg2_low_deadtime_ns);
        break;
    case 's':
        if (leg2_low_deadtime_ns >= deadtime_step_ns)
        {
            leg2_low_deadtime_ns -= deadtime_step_ns;
        }
        else
        {
            leg2_low_deadtime_ns = deadtime_min_ns;
        }
        shield.power.setDeadTime(LEG2_LOW, leg2_low_deadtime_ns, leg2_low_deadtime_ns);
        printk("LEG2_LOW deadtime: %u ns\n", leg2_low_deadtime_ns);
        break;
    case 'r':
        is_scope_downloading = true;
        break;
    case 'q':
        scope.start();
        printk("scope acquisition restarted\n");
        break;
    case 't':
        spin.gpio.resetPin(22);
        break;
    default:
        break;
    }
}

/**
 * This is the code loop of the background task
 * This task mostly logs back measurements to the USB serial interface.
 */
void loop_application_task()
{
    if (is_scope_downloading)
    {
        dump_scope_datas(scope);
        is_scope_downloading = false;
        task.suspendBackgroundMs(100);
        return;
    }

    if (mode == IDLE && !inverter_sync_requested) spin.led.turnOff();
    else spin.led.turnOn();

    /* ILow1, ILow2, VLow, duty1, duty2, VAC, VACrms, angle, IAC, VDCBus, mode, burst. */
    printk("%7.3f:", (double)i_low_1_value);
    printk("%7.3f:", (double)i_low_2_value);
    printk("%7.3f:", (double)v_low_value);
    printk("%7.3f:", (double)boost_duty.leg1);
    printk("%7.3f:", (double)boost_duty.leg2);
    printk("%7.3f:", (double)v_ac_value);
    printk("%7.3f:", (double)v_ac_rms_value);
    printk("%7.3f:", (double)grid_angle);
    printk("%7.3f:", (double)i_ac_value);
    printk("%7.3f:", (double)v_dc_bus_value);
    printk("%u:%u\n", static_cast<unsigned int>(mode),
           static_cast<unsigned int>(boost_converter.isBurstPaused()));
    task.suspendBackgroundMs(100);
}

/**
 * This is the code loop of the critical task
 * This task runs at 10kHz.
 *  - It retrieves sensors values
 *  - It runs inverter control and the boost hot loop every tick
 *  - It runs the boost voltage/ramp or MPPT cold loop every tenth tick
 *  - Boost enable flags stop the corresponding PWM during a VDCBus burst pause
 *  - It updates the PWM signals
 */
void loop_critical_task()
{
    meas_data = shield.sensors.getLatestValue(ILow1);
    if (meas_data != NO_VALUE) i_low_1_value = meas_data;

    meas_data = shield.sensors.getLatestValue(VLow);
    if (meas_data != NO_VALUE) v_low_value = meas_data;

    if (grid_voltage_age_ticks < grid_voltage_stale_ticks) ++grid_voltage_age_ticks;
    meas_data = shield.sensors.getLatestValue(VAC);
    if (meas_data != NO_VALUE)
    {
        grid_voltage_age_ticks = 0U;
        v_ac_value = meas_data;
    }

    meas_data = shield.sensors.getLatestValue(ILow2);
    if (meas_data != NO_VALUE) i_low_2_value = meas_data;

    meas_data = shield.sensors.getLatestValue(IAC);
    if (meas_data != NO_VALUE) i_ac_value = meas_data;

    meas_data = shield.sensors.getLatestValue(VDCBus);
    if (meas_data != NO_VALUE)
    {
        v_dc_bus_value = meas_data;
    }

    boost_converter.setILow1(i_low_1_value);
    boost_converter.setILow2(i_low_2_value);
    boost_converter.setVLow(v_low_value);
    boost_converter.setVBus(v_dc_bus_value);
    boost_converter.setVBusRef(voltage_reference);

    const boost_mode requested_mode = mode;
    const bool sync_only = inverter_sync_requested;
    static boost_mode previous_mode = IDLE;
    if (requested_mode != previous_mode)
    {
        shield.power.stop(LEG1_LOW);
        shield.power.stop(LEG2_LOW);
        boost_converter.setMode(requested_mode);
        // Run the new mode's cold loop on this tick, using the measurements above.
        cold_loop_tick = cold_loop_divider - 1U;
        previous_mode = requested_mode;
    }

    if (++cold_loop_tick >= cold_loop_divider)
    {
        cold_loop_tick = 0U;
        boost_converter.coldLoop();
    }
    boost_duty = boost_converter.hotLoop();

    /* Keep the PLL running in idle, using a positive bus value for duty scaling. */
    if (isfinite(v_dc_bus_value) && v_dc_bus_value > 0.0F)
    {
        inverter.setVBus(v_dc_bus_value);
    }
    if (sync_only) inverter.requestSync();
    else inverter.requestDisconnect();
    inverter.calculateDuty(v_ac_value, i_ac_value);
    const clarke_t grid_voltage_ab = inverter.getVab();
    grid_observer.update(grid_voltage_ab.alpha, grid_voltage_ab.beta, inverter.getw(),
                         grid_voltage_age_ticks < grid_voltage_stale_ticks);
    /* Grid quality qualifies synchronization; this application never connects. */
    inverter.setGridQuality(grid_observer.canOperate());
    grid_angle = inverter.getTheta();
    if (!is_scope_downloading)
    {
        scope.acquire();
    }

    if (requested_mode != IDLE)
    {
        shield.power.setDutyCycle(LEG1_LOW, boost_duty.leg1);
        shield.power.setDutyCycle(LEG2_LOW, boost_duty.leg2);
        if (boost_converter.isLeg1Enabled()) shield.power.start(LEG1_LOW);
        else shield.power.stop(LEG1_LOW);
        if (boost_converter.isLeg2Enabled()) shield.power.start(LEG2_LOW);
        else shield.power.stop(LEG2_LOW);
    }

    /* Synchronization only: keep the inverter bridge disabled. */
    shield.power.stop(LEG1_HIGH);
    shield.power.stop(LEG2_HIGH);
}

/**
 * This is the main function of this example
 * This function is generic and does not need editing.
 */
int main(void)
{
    setup_routine();

    return 0;
}
