/*
 *
 * Copyright (c) 2021-2026 OWNTECH TECHNOLOGIES
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
 * @file boost.h
 * @brief Minimal cascaded controller for two parallel unidirectional boost legs.
 */
#ifndef BOOST_H
#define BOOST_H

#include "pid.h"

/** @brief Operating modes of the boost controller. */
typedef enum {
    IDLE = 0,    ///< Disable control and clear current reference and duty cycle.
    STARTUP = 1, ///< Ramp bus voltage using converter 1 only, with a startup current limit.
    BUSREG = 2,  ///< Regulate bus voltage with the voltage/current cascade.
    MPPT = 3     ///< Track maximum input power by adjusting the total input current reference.
} boost_mode;

/** @brief Duty commands for the two parallel boost legs. */
struct BoostDutyCycles {
    float32_t leg1 = 0.0F; ///< Leg 1 duty cycle, or zero when disabled.
    float32_t leg2 = 0.0F; ///< Leg 2 duty cycle, or zero when disabled.
};

/**
 * @class BoostConverter
 * @brief Cascaded voltage and current control for two parallel unidirectional boost legs.
 * @details Update measurements through the setters before calling the loops.
 * Call coldLoop() at the configured cold-loop period and hotLoop() at the
 * configured hot-loop period. STARTUP/BUSREG use a voltage PID to supply the current
 * reference. MPPT directly supplies the total current reference without voltage control.
 * In MPPT, the VSI is responsible for balancing DC-bus power to maintain bus voltage.
 * In BUSREG/MPPT, leg 1 receives up to leg1CurrentLimit; leg 2 receives the excess.
 * STARTUP uses converter 1 only, capped by startupCurrentMax. Both legs
 * share VLow and VBus and have independent input current measurements and PIDs.
 * Use isLeg1Enabled()/isLeg2Enabled() to start or stop the corresponding PWM;
 * a zero duty command alone does not disable the hardware. Optional bus-voltage
 * hysteresis pauses STARTUP/BUSREG at an upper boundary and resumes at a lower
 * boundary without changing mode. MPPT does not use these burst boundaries.
 */
class BoostConverter {
public:
    /** @brief Construct an idle controller; call init() before enabling control. */
    BoostConverter() = default;

    /**
     * @brief Initialize the voltage PID and both current PIDs and reset control and MPPT history.
     * @param mode Initial operating mode.
     * @param V_bus Initial bus measurement and bus voltage reference, in V; must be positive.
     * @param voltageParams Voltage PID tuning; Ts is the cold-loop period in s.
     * Output bounds define the allowed nonnegative total current reference in A,
     * including the MPPT search limits. MPPT bypasses the voltage PID itself.
     * @param currentParams Current PID tuning shared by both legs; Ts is the hot-loop period in s.
     * Output bounds define the duty-cycle range and must lie within [0, 1].
     * @param leg1CurrentLimit Positive current threshold in A. Leg 1 receives
     * min(total reference, threshold); leg 2 receives the remaining reference.
     * Set voltageParams.upper_bound to the permitted total current of both legs.
     * Initialization sets startupCurrentMax to min(leg1CurrentLimit, voltageParams.upper_bound).
     * @return 0 on success, or -EINVAL for invalid arguments or PID parameters.
     * @note Values must be finite. Failure leaves the controller disabled.
     * Both sampling periods must be positive, with voltageParams.Ts >= currentParams.Ts.
     * Tune the supplied gains for the converter hardware.
     */
    int8_t init(boost_mode mode, float32_t V_bus,
                const PidParams& voltageParams, const PidParams& currentParams,
                float32_t leg1CurrentLimit);

    /**
     * @brief Clear PID states, total and per-leg current references, both duties, and MPPT history.
     * @details Preserve the mode, measurements, target voltage references, and ramp rate.
     * Restart the startup ramp from the latest VBus on the next startup cold-loop call.
     * Preserve configured burst boundaries and an applicable burst pause.
     */
    void reset();

    /**
     * @brief Update and distribute the total input current reference.
     * @details STARTUP and BUSREG regulate VBus. MPPT only calls Mppt(), which
     * directly perturbs total current demand using measured input power; it runs
     * no voltage PID and does not use VBus or VBusRef.
     * STARTUP ramps its internal bus reference from measured VBus toward VBusRef
     * at startupRampRate V/s, advancing by startupRampRate * voltageParams.Ts per call.
     * Only converter 1 runs in STARTUP, limited to startupCurrentMax A. Converter 2
     * remains disabled. Once the target is reached, hold it in STARTUP until the caller changes mode.
     * @return Total current reference in A within voltageParams output bounds;
     * STARTUP additionally limits it to startupCurrentMax, overriding a higher lower bound.
     * Returns zero if idle, uninitialized, or burst-paused. The startup ramp and
     * voltage PID do not advance while burst-paused.
     */
    float32_t coldLoop();

    /**
     * @brief Run both current PIDs using their own references and measurements.
     * @return Per-leg duty cycles within currentParams output bounds when enabled.
     * Disabled legs return zero and have their current PID reset. Check bus burst
     * hysteresis before running either PID, independently of the cold-loop period.
     */
    BoostDutyCycles hotLoop();

    /**
     * @brief Update total input current demand using perturb-and-observe MPPT.
     * @details Compare VLow * (ILow1 + ILow2) with the previous power sample and reverse
     * the current perturbation direction when power decreases. Perturb by 0.1 A per call,
     * within voltageParams current bounds; reverse direction at either bound.
     * Distribute the resulting reference to both current loops. No voltage PID runs.
     * Has no effect outside MPPT mode, before initialization, or when VLow <= 0.
     * @note coldLoop() calls this method automatically in MPPT mode. Allow the
     * converter to settle between cold-loop calls before evaluating power changes.
     */
    void Mppt();

    /** @brief Get leg 1 input current. @return Measured current in A. */
    float32_t getILow1() const;
    /** @brief Get leg 2 input current. @return Measured current in A. */
    float32_t getILow2() const;
    /** @brief Get total input current. @return ILow1 + ILow2 in A. */
    float32_t getILowTotal() const;

    /**
     * @brief Get the stored input voltage measurement.
     * @return Low-side voltage in V.
     */
    float32_t getVLow() const;

    /**
     * @brief Get the stored bus voltage measurement.
     * @return Bus voltage in V.
     */
    float32_t getVBus() const;

    /**
     * @brief Update leg 1 input current measurement.
     * @param ILow1 Current in A; non-finite values are ignored.
     */
    void setILow1(float32_t ILow1);
    /**
     * @brief Update leg 2 input current measurement.
     * @param ILow2 Current in A; non-finite values are ignored.
     */
    void setILow2(float32_t ILow2);

    /**
     * @brief Update the input voltage measurement.
     * @param VLow Voltage in V; negative or non-finite values are ignored.
     */
    void setVLow(float32_t VLow);

    /**
     * @brief Update the bus voltage measurement.
     * @param VBus Voltage in V; negative or non-finite values are ignored.
     */
    void setVBus(float32_t VBus);

    /**
     * @brief Configure and enable bus-voltage burst hysteresis for STARTUP/BUSREG.
     * @param lower Lower bus boundary in V; switching may resume at VBus <= lower.
     * @param upper Upper bus boundary in V; pause switching at VBus >= upper.
     * @return 0 on success, -EINVAL unless values are finite and 0 <= lower < upper.
     * @details Disabled until configured. Preserve boundaries across reset(), init(),
     * and mode changes. Check immediately on configuration and in both loops.
     * Between boundaries, retain the previous state. MPPT is unaffected.
     * Pausing clears current references, duties, and all PID states, and freezes
     * the startup ramp. On release, the next cold loop regenerates current demand.
     * The caller must stop PWM when the leg enable flags are false.
     */
    int8_t setVBusBurstBounds(float32_t lower, float32_t upper);
    /** @brief Get the burst resume boundary. @return Lower boundary in V. */
    float32_t getVBusBurstMin() const;
    /** @brief Get the burst pause boundary. @return Upper boundary in V. */
    float32_t getVBusBurstMax() const;
    /** @brief Check whether burst boundaries were configured. @return true when configured. */
    bool isBurstEnabled() const;
    /** @brief Inspect the last evaluated burst state. @return true while switching is paused. */
    bool isBurstPaused() const;

    /**
     * @brief Get the total current reference distributed between both legs.
     * @return Total input current reference in A.
     */
    float32_t getILowRef() const;
    /** @brief Get leg 1 current reference. @return Reference in A. */
    float32_t getILow1Ref() const;
    /** @brief Get leg 2 current reference. @return Reference in A. */
    float32_t getILow2Ref() const;
    /** @brief Get the leg 1 current threshold. @return Threshold in A. */
    float32_t getLeg1CurrentLimit() const;
    /** @brief Check whether leg 1 PWM is needed. @return true for an active positive reference outside a burst pause. */
    bool isLeg1Enabled() const;
    /** @brief Check whether leg 2 PWM is needed. @return true when total demand exceeds the threshold outside a burst pause. */
    bool isLeg2Enabled() const;

    /**
     * @brief Get the target bus voltage reference for STARTUP and BUSREG modes.
     * @return Bus voltage reference in V.
     */
    float32_t getVBusRef() const;

    /**
     * @brief Set the startup voltage reference slew rate.
     * @param rate Positive finite rate in V/s; invalid values are ignored.
     * @details Default is 10 V/s. Changing the rate does not restart the ramp.
     */
    void setStartupRampRate(float32_t rate);
    /** @brief Get the startup ramp rate. @return Rate in V/s. */
    float32_t getStartupRampRate() const;
    /**
     * @brief Get the internal bus voltage reference used during STARTUP.
     * @return Ramp reference in V. Updated by startup cold-loop calls.
     */
    float32_t getStartupVBusRef() const;

    /**
     * @brief Set the maximum converter 1 current reference during STARTUP.
     * @param currentMax Nonnegative finite limit in A; invalid values are ignored.
     * @details Call after init(). Clamp to leg1CurrentLimit and the voltage PID upper
     * bound. Zero disables startup current. Apply immediately to the stored startup
     * reference; the voltage PID uses this bound at the next cold-loop call.
     * Preserve the limit across reset() and mode changes; init() restores its default.
     */
    void setStartupCurrentMax(float32_t currentMax);
    /** @brief Get the startup current limit. @return Effective maximum in A. */
    float32_t getStartupCurrentMax() const;

    /**
     * @brief Set and distribute the total current reference directly until the next cold-loop update.
     * @param ILowRef Total current reference in A, clamped to voltageParams output bounds.
     * STARTUP additionally clamps to startupCurrentMax and supplies converter 1 only.
     * In MPPT, restart the power comparison after a manual reference change.
     * A burst pause forces the reference to zero, including manual changes.
     * Non-finite values are ignored; before initialization the reference is zero.
     */
    void setILowRef(float32_t ILowRef);

    /**
     * @brief Set the target bus voltage.
     * @details In STARTUP the internal reference slews toward the new target without
     * restarting the ramp. In BUSREG the PID uses the new target directly.
     * @param VBusRef Voltage reference in V; negative or non-finite values are ignored.
     */
    void setVBusRef(float32_t VBusRef);

    /** @brief Get leg 1 duty. @return Last duty command, or zero when disabled/reset. */
    float32_t getDutyLeg1() const;
    /** @brief Get leg 2 duty. @return Last duty command, or zero when disabled/reset. */
    float32_t getDutyLeg2() const;

    /**
     * @brief Compute input power from the stored measurements.
     * @return VLow * (ILow1 + ILow2) in W.
     */
    float32_t getPower() const;

    /**
     * @brief Get the selected operating mode.
     * @return Current boost controller mode.
     */
    boost_mode getMode() const;

    /**
     * @brief Select a mode and reset controller and MPPT states.
     * @param mode Requested operating mode; invalid values select IDLE.
     * @details Preserve measurements and voltage references; clear the current
     * reference and duty cycle, even when selecting the current mode again.
     * Entering STARTUP restarts the ramp from measured VBus on the next cold-loop call.
     */
    void setMode(boost_mode mode);

private:
    /** @brief Evaluate burst hysteresis and clear PID states/outputs on pause entry. */
    void updateBurstState();
    /** @brief Clear all PID states, current references, and duties without resetting the ramp. */
    void clearControlOutputs();

    /**
     * @brief Run the voltage PID with mode-specific current bounds and anti-windup.
     * @param reference Voltage reference in V.
     * @param measurement Voltage measurement in V.
     * @return Bounded current reference in A.
     */
    float32_t calculateVoltageCurrent(float32_t reference, float32_t measurement);

    /** @brief Use converter 1 only in STARTUP; otherwise allocate excess demand to leg 2. */
    void splitCurrentReference();

    boost_mode _mode = IDLE;
    bool _initialized = false;
    float32_t _ILow1 = 0.0F;
    float32_t _ILow2 = 0.0F;
    float32_t _VLow = 0.0F;
    float32_t _VBus = 0.0F;
    float32_t _ILow_ref = 0.0F;
    float32_t _VBus_ref = 0.0F;
    float32_t _burst_VBus_min = 0.0F; ///< Resume boundary, in V.
    float32_t _burst_VBus_max = 0.0F; ///< Pause boundary, in V.
    bool _burst_enabled = false; ///< Bounds have been configured.
    bool _burst_paused = false; ///< Hysteresis currently inhibits switching.
    float32_t _TsCold = 0.0F; ///< Voltage PID sampling period, in s.
    float32_t _startup_current_max = 0.0F; ///< Converter 1 current reference ceiling in STARTUP, in A.
    float32_t _startup_ramp_rate = 10.0F; ///< Bus reference slew rate, in V/s.
    float32_t _startup_VBus_ref = 0.0F; ///< Internal startup bus reference, in V.
    bool _startup_ramp_pending = true; ///< Seed the ramp from the next VBus sample.
    float32_t _ILow1_ref = 0.0F;
    float32_t _ILow2_ref = 0.0F;
    float32_t _leg1_current_limit = 0.0F;
    BoostDutyCycles _duty;

    Pid _voltage_pid;
    Pid _current1_pid;
    Pid _current2_pid;
    float32_t _current_ref_min = 0.0F; ///< Voltage PID lower output bound, in A.
    float32_t _current_ref_max = 0.0F; ///< Voltage PID upper output bound, in A.
    static constexpr float32_t MPPT_CURRENT_STEP = 0.1F; ///< Total current step in A per MPPT call.
    float32_t _mppt_previous_power = 0.0F;
    float32_t _mppt_direction = 1.0F;
    bool _mppt_has_previous_power = false;
};

#endif // BOOST_H
