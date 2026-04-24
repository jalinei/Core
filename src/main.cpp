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
 * @brief  This example demonstrates how to deploy a Buck converter with
 *         voltage mode control on the Twist power shield.
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
#include "pid.h"
#include "filters.h"
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
static uint32_t control_task_period = 100;
/* [bool] state of the PWM (ctrl task) */
static bool pwm_enable = false;

uint8_t received_serial_char;

/* Measure variables */

static float32_t v_low_value;
static float32_t v_ac_value;
static float32_t v_ac_rms_value;
static float32_t v_dc_bus_value;
static float32_t i_low_1_value;
static float32_t i_low_2_value;
static float32_t i_ac_value;
static float32_t temp_sensor_value;

/* Temporary storage fore measured value (ctrl task) */
static float meas_data;

float32_t duty_cycle = 0.04;
static uint16_t leg2_low_deadtime_ns = 100;
static constexpr uint16_t deadtime_step_ns = 10;
static constexpr uint16_t deadtime_min_ns = 40;

/* Voltage reference */
static float32_t voltage_reference = 15;

/* PID coefficients for a 8.6ms step response*/
static float32_t kp = 0.000215;
static float32_t Ti = 7.5175e-5;
static float32_t Td = 0.0;
static float32_t N = 0.0;
static float32_t upper_bound = 1.0F;
static float32_t lower_bound = 0.0F;
static float32_t Ts = control_task_period * 1e-6;
static PidParams pid_params(Ts, kp, Ti, Td, N, lower_bound, upper_bound);
static Pid pid;

/* Grid PLL */
static constexpr float32_t grid_voltage_rms = 230.0F;
static constexpr float32_t grid_frequency_hz = 50.0F;
static constexpr float32_t grid_voltage_amplitude =
    grid_voltage_rms * 1.41421356237F;
static constexpr float32_t grid_pll_rise_time = 50e-3F;
/* One nominal 50 Hz period at the 10 kHz critical task rate. */
static constexpr uint32_t v_ac_rms_window_sample_count = 200U;
static PllSinus grid_pll;
static PllDatas grid_pll_datas;
static float32_t grid_angle;
static float32_t v_ac_rms_square_sum;
static uint32_t v_ac_rms_sample_count;

/* ScopeMimicry */
static constexpr uint16_t scope_buffer_sample_count = 1024U;
static constexpr uint16_t scope_channel_count = 2U;
static ScopeMimicry scope(scope_buffer_sample_count, scope_channel_count);
static bool is_scope_downloading;

/*--------------------------------------------------------------- */

/* LIST OF POSSIBLE MODES FOR THE OWNTECH CONVERTER */
enum serial_interface_menu_mode
{
    IDLEMODE = 0,
    POWERMODE
};

uint8_t mode = IDLEMODE;

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
 *  - Initializes the power shield in Buck mode
 *  - Initializes the power shield sensors
 *  - Initializes the PID controller, grid PLL, and scope
 *  - Spawns three tasks.
 */
void setup_routine()
{
    /* Buck voltage mode */
    shield.power.initBoost(LEG1_LOW);
    shield.power.initBoost(LEG2_LOW);
    shield.power.setDeadTime(LEG2_LOW, leg2_low_deadtime_ns, leg2_low_deadtime_ns);
    shield.power.setDutyCycleMin(ALL, 0.0);
    shield.sensors.enableSensor(VLow, ADC_1);
    shield.sensors.enableSensor(VAC, ADC_1);
    shield.sensors.enableSensor(VDCBus, ADC_1);
    shield.sensors.enableSensor(ILow1, ADC_2);
    shield.sensors.enableSensor(ILow2, ADC_2);
    shield.sensors.enableSensor(IAC, ADC_2);

    spin.gpio.configurePin(22, OUTPUT);
    pid.init(pid_params);
    grid_pll.init(Ts, grid_voltage_amplitude, grid_frequency_hz, grid_pll_rise_time);
    grid_pll.reset(grid_frequency_hz);
    scope.connectChannel(v_ac_value, "VAC");
    scope.connectChannel(grid_angle, "grid_angle");
    scope.set_trigger(&scope_trigger);
    scope.set_delay(0.0F);
    scope.start();

    /* Then declare tasks */
    uint32_t app_task_number = task.createBackground(loop_application_task);
    uint32_t com_task_number = task.createBackground(loop_communication_task);
    task.createCritical(loop_critical_task, 100);

    /* Finally, start tasks */
    task.startBackground(app_task_number);
    task.startBackground(com_task_number);
    task.startCritical();
}

/*--------------LOOP FUNCTIONS-------------------------------- */

/**
 * This tasks implements a minimalistic USB serial interface to control
 * the buck converter.
 */
void loop_communication_task()
{
    received_serial_char = console_getchar();
    switch (received_serial_char)
    {
    case 'h':
        /*----------SERIAL INTERFACE MENU----------------------- */
        printk(" ________________________________________ \n"
               "|     ---- MENU buck voltage mode ----   |\n"
               "|     press i : idle mode                |\n"
               "|     press p : power mode               |\n"
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
        mode = IDLEMODE;
        break;
    case 'p':
        printk("power mode\n");
        mode = POWERMODE;
        break;
    case 'u':
        duty_cycle += 0.001;
        break;
    case 'd':
        duty_cycle -= 0.001;
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

    if (mode == IDLEMODE)
    {
        spin.led.turnOff();
        printk("%7.3f:", (double)i_low_1_value);
        printk("%7.3f:", (double)i_low_2_value);
        printk("%7.3f:", (double)v_low_value);
        printk("%7.3f:", (double)duty_cycle);
        printk("%7.3f:", (double)v_ac_value);
        printk("%7.3f:", (double)v_ac_rms_value);
        printk("%7.3f:", (double)grid_angle);
        printk("%7.3f:", (double)i_ac_value);
        printk("%7.3f:", (double)v_dc_bus_value);
        printk("\n");
    }
    else if (mode == POWERMODE)
    {
        spin.led.turnOn();

        printk("%7.3f:", (double)i_low_1_value);
        printk("%7.3f:", (double)i_low_2_value);
        printk("%7.3f:", (double)v_low_value);
        printk("%7.3f:", (double)duty_cycle);
        printk("%7.3f:", (double)v_ac_value);
        printk("%7.3f:", (double)v_ac_rms_value);
        printk("%7.3f:", (double)grid_angle);
        printk("%7.3f:", (double)i_ac_value);
        printk("%7.3f:", (double)v_dc_bus_value);
        printk("\n");
    }
    task.suspendBackgroundMs(100);
}

/**
 * This is the code loop of the critical task
 * This task runs at 10kHz.
 *  - It retrieves sensors values
 *  - It runs the grid PLL
 *  - It update the PWM signals
 */
void loop_critical_task()
{
    meas_data = shield.sensors.getLatestValue(ILow1);
    if (meas_data != NO_VALUE) i_low_1_value = meas_data;

    meas_data = shield.sensors.getLatestValue(VLow);
    if (meas_data != NO_VALUE) v_low_value = meas_data;

    meas_data = shield.sensors.getLatestValue(VAC);
    if (meas_data != NO_VALUE)
    {
        v_ac_value = meas_data;
        grid_pll_datas = grid_pll.calculateWithReturn(v_ac_value);
        grid_angle = grid_pll_datas.angle;
        if (!is_scope_downloading)
        {
            scope.acquire();
        }

        v_ac_rms_square_sum += v_ac_value * v_ac_value;
        v_ac_rms_sample_count++;
        if (v_ac_rms_sample_count >= v_ac_rms_window_sample_count)
        {
            arm_sqrt_f32(v_ac_rms_square_sum / v_ac_rms_window_sample_count,
                         &v_ac_rms_value);
            v_ac_rms_square_sum = 0.0F;
            v_ac_rms_sample_count = 0U;
        }
    }

    meas_data = shield.sensors.getLatestValue(ILow2);
    if (meas_data != NO_VALUE) i_low_2_value = meas_data;

    meas_data = shield.sensors.getLatestValue(IAC);
    if (meas_data != NO_VALUE) i_ac_value = meas_data;

    meas_data = shield.sensors.getLatestValue(VDCBus);
    if (meas_data != NO_VALUE) v_dc_bus_value = meas_data;


    if (mode == IDLEMODE)
    {
        if (pwm_enable == true)
        {
            shield.power.stop(ALL);
        }
        pwm_enable = false;
    }
    else if (mode == POWERMODE)
    {
        shield.power.setDutyCycle(LEG1_LOW,duty_cycle);
        shield.power.setDutyCycle(LEG2_LOW,duty_cycle);

        /* Set POWER ON */
        if (!pwm_enable)
        {
            pwm_enable = true;
            shield.power.start(ALL);
        }
    }

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
