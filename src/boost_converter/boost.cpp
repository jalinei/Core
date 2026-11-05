/*
 *
 * Copyright (c) 2021-2024 LAAS-CNRS
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
 * @file boost.cpp
 * @brief Cascaded voltage/current control for a boost converter.
 */
#include "boost.h"

#include <math.h>
#include <errno.h>

namespace {
/**
 * @brief Limit a scalar to an inclusive interval.
 * @param value Value to constrain.
 * @param lower Minimum allowed value.
 * @param upper Maximum allowed value; must be at least lower.
 * @return Constrained value.
 */
float32_t clamp(float32_t value, float32_t lower, float32_t upper) {
    return value < lower ? lower : (value > upper ? upper : value);
}

/**
 * @brief Check whether a mode is supported by the boost controller.
 * @param mode Mode to validate.
 * @return true for IDLE, STARTUP, BUSREG, or MPPT; false otherwise.
 */
bool validMode(boost_mode mode) {
    return mode == IDLE || mode == STARTUP || mode == BUSREG || mode == MPPT;
}

/**
 * @brief Validate finite PID parameters before initializing either controller.
 * @param params PID tuning, sampling period, and output bounds.
 * @return true when parameters satisfy the PID implementation requirements.
 */
bool validPidParams(const PidParams& params) {
    return isfinite(params.Ts) && params.Ts > 0.0F &&
           isfinite(params.Kp) && params.Kp != 0.0F &&
           isfinite(params.Ti) && params.Ti != 0.0F &&
           isfinite(params.Td) && isfinite(params.N) &&
           (params.N == 0.0F || params.Td / params.N >= 0.0F) &&
           isfinite(params.lower_bound) && isfinite(params.upper_bound) &&
           params.lower_bound <= params.upper_bound;
}
}

int8_t BoostConverter::init(boost_mode mode, float32_t V_bus,
                            const PidParams& voltageParams,
                            const PidParams& currentParams,
                            float32_t leg1CurrentLimit) {
    _initialized = false;
    _burst_paused = false;
    _mode = IDLE;
    _ILow_ref = 0.0F;
    _ILow1_ref = 0.0F;
    _ILow2_ref = 0.0F;
    _duty = {};
    _current_ref_min = 0.0F;
    _current_ref_max = 0.0F;
    if (!validMode(mode) || !isfinite(V_bus) || V_bus <= 0.0F ||
        !validPidParams(voltageParams) || !validPidParams(currentParams) ||
        voltageParams.Ts < currentParams.Ts || voltageParams.lower_bound < 0.0F ||
        currentParams.lower_bound < 0.0F || currentParams.upper_bound > 1.0F ||
        !isfinite(leg1CurrentLimit) || leg1CurrentLimit <= 0.0F) {
        return -EINVAL;
    }

    int8_t result = _voltage_pid.init(voltageParams);
    if (result != 0) return result;
    result = _current1_pid.init(currentParams);
    if (result != 0) return result;
    result = _current2_pid.init(currentParams);
    if (result != 0) return result;

    _TsCold = voltageParams.Ts;
    _leg1_current_limit = leg1CurrentLimit;
    _current_ref_min = voltageParams.lower_bound;
    _current_ref_max = voltageParams.upper_bound;
    _startup_current_max = clamp(leg1CurrentLimit, 0.0F, _current_ref_max);
    _VBus = V_bus;
    _VBus_ref = V_bus;
    _mode = mode;
    _initialized = true;
    reset();
    return 0;
}

void BoostConverter::clearControlOutputs() {
    if (_initialized) {
        _voltage_pid.reset();
        _current1_pid.reset();
        _current2_pid.reset();
    }
    _ILow_ref = 0.0F;
    _ILow1_ref = 0.0F;
    _ILow2_ref = 0.0F;
    _duty = {};
}

void BoostConverter::reset() {
    clearControlOutputs();
    _startup_VBus_ref = _VBus;
    _startup_ramp_pending = true;
    _mppt_previous_power = 0.0F;
    _mppt_direction = 1.0F;
    _mppt_has_previous_power = false;
    updateBurstState();
}

float32_t BoostConverter::coldLoop() {
    if (!_initialized || _mode == IDLE) {
        reset();
        return _ILow_ref;
    }

    updateBurstState();
    if (_burst_paused) return 0.0F;

    if (_mode == MPPT) {
        Mppt();
        return _ILow_ref;
    } else if (_mode == STARTUP) {
        if (_startup_ramp_pending) {
            _startup_VBus_ref = _VBus;
            _startup_ramp_pending = false;
        }
        const float32_t step = _startup_ramp_rate * _TsCold;
        if (_startup_VBus_ref < _VBus_ref) {
            _startup_VBus_ref += clamp(_VBus_ref - _startup_VBus_ref, 0.0F, step);
        } else {
            _startup_VBus_ref -= clamp(_startup_VBus_ref - _VBus_ref, 0.0F, step);
        }
        _ILow_ref = calculateVoltageCurrent(_startup_VBus_ref, _VBus);
    } else {
        _ILow_ref = calculateVoltageCurrent(_VBus_ref, _VBus);
    }
    splitCurrentReference();
    return _ILow_ref;
}

BoostDutyCycles BoostConverter::hotLoop() {
    if (!_initialized || _mode == IDLE) {
        reset();
        return _duty;
    }
    updateBurstState();
    if (_burst_paused) return _duty;

    if (isLeg1Enabled()) {
        _duty.leg1 = _current1_pid.calculateWithReturn(_ILow1_ref, _ILow1);
    } else {
        _current1_pid.reset();
        _duty.leg1 = 0.0F;
    }
    if (isLeg2Enabled()) {
        _duty.leg2 = _current2_pid.calculateWithReturn(_ILow2_ref, _ILow2);
    } else {
        _current2_pid.reset();
        _duty.leg2 = 0.0F;
    }
    return _duty;
}

void BoostConverter::Mppt() {
    if (!_initialized || _mode != MPPT || _VLow <= 0.0F) return;

    const float32_t power = getPower();
    if (!isfinite(power)) return;

    if (_mppt_has_previous_power && power < _mppt_previous_power) {
        _mppt_direction = -_mppt_direction;
    }
    _mppt_has_previous_power = true;
    _mppt_previous_power = power;

    const float32_t current = clamp(_ILow_ref, _current_ref_min, _current_ref_max);
    // Turn back into the allowed range instead of repeatedly perturbing a limit.
    if (_mppt_direction > 0.0F && current >= _current_ref_max) {
        _mppt_direction = -1.0F;
    } else if (_mppt_direction < 0.0F && current <= _current_ref_min) {
        _mppt_direction = 1.0F;
    }
    _ILow_ref = clamp(current + _mppt_direction * MPPT_CURRENT_STEP,
                      _current_ref_min, _current_ref_max);
    splitCurrentReference();
}

float32_t BoostConverter::getILow1() const { return _ILow1; }
float32_t BoostConverter::getILow2() const { return _ILow2; }
float32_t BoostConverter::getILowTotal() const { return _ILow1 + _ILow2; }
float32_t BoostConverter::getVLow() const { return _VLow; }
float32_t BoostConverter::getVBus() const { return _VBus; }
void BoostConverter::setILow1(float32_t ILow1) {
    if (isfinite(ILow1)) _ILow1 = ILow1;
}
void BoostConverter::setILow2(float32_t ILow2) {
    if (isfinite(ILow2)) _ILow2 = ILow2;
}
void BoostConverter::setVLow(float32_t VLow) {
    if (isfinite(VLow) && VLow >= 0.0F) _VLow = VLow;
}
void BoostConverter::setVBus(float32_t VBus) {
    if (isfinite(VBus) && VBus >= 0.0F) _VBus = VBus;
}

float32_t BoostConverter::getILowRef() const { return _ILow_ref; }
float32_t BoostConverter::getILow1Ref() const { return _ILow1_ref; }
float32_t BoostConverter::getILow2Ref() const { return _ILow2_ref; }
float32_t BoostConverter::getLeg1CurrentLimit() const { return _leg1_current_limit; }
bool BoostConverter::isLeg1Enabled() const {
    return _initialized && _mode != IDLE && !_burst_paused && _ILow1_ref > 0.0F;
}
bool BoostConverter::isLeg2Enabled() const {
    return _initialized && _mode != IDLE && !_burst_paused && _ILow2_ref > 0.0F;
}
float32_t BoostConverter::getVBusRef() const { return _VBus_ref; }
void BoostConverter::setStartupRampRate(float32_t rate) {
    if (isfinite(rate) && rate > 0.0F) _startup_ramp_rate = rate;
}
float32_t BoostConverter::getStartupRampRate() const { return _startup_ramp_rate; }
float32_t BoostConverter::getStartupVBusRef() const { return _startup_VBus_ref; }
void BoostConverter::setStartupCurrentMax(float32_t currentMax) {
    if (!_initialized || !isfinite(currentMax) || currentMax < 0.0F) return;
    _startup_current_max = clamp(currentMax, 0.0F,
                                 clamp(_leg1_current_limit, 0.0F, _current_ref_max));
    if (_mode == STARTUP) splitCurrentReference();
}
float32_t BoostConverter::getStartupCurrentMax() const { return _startup_current_max; }
void BoostConverter::setILowRef(float32_t ILowRef) {
    if (isfinite(ILowRef)) {
        _ILow_ref = clamp(ILowRef, _current_ref_min, _current_ref_max);
        if (_mode == MPPT) {
            // A manual reference change invalidates the previous MPPT comparison.
            _mppt_has_previous_power = false;
            _mppt_direction = 1.0F;
        }
        splitCurrentReference();
    }
}
void BoostConverter::setVBusRef(float32_t VBusRef) {
    if (isfinite(VBusRef) && VBusRef >= 0.0F) _VBus_ref = VBusRef;
}
float32_t BoostConverter::getDutyLeg1() const { return _duty.leg1; }
float32_t BoostConverter::getDutyLeg2() const { return _duty.leg2; }
float32_t BoostConverter::getPower() const { return _VLow * getILowTotal(); }
boost_mode BoostConverter::getMode() const { return _mode; }
void BoostConverter::setMode(boost_mode mode) {
    _mode = validMode(mode) ? mode : IDLE;
    reset();
}

void BoostConverter::splitCurrentReference() {
    if (_burst_paused) {
        clearControlOutputs();
        return;
    }
    if (_mode == STARTUP) {
        _ILow_ref = clamp(_ILow_ref, 0.0F, _startup_current_max);
        _ILow1_ref = _ILow_ref;
        _ILow2_ref = 0.0F;
    } else {
        _ILow1_ref = clamp(_ILow_ref, 0.0F, _leg1_current_limit);
        _ILow2_ref = _ILow_ref - _ILow1_ref;
    }
    // Stop and clear an unused leg immediately, including its integral state.
    if (_ILow1_ref <= 0.0F) {
        if (_initialized) _current1_pid.reset();
        _duty.leg1 = 0.0F;
    }
    if (_ILow2_ref <= 0.0F) {
        if (_initialized) _current2_pid.reset();
        _duty.leg2 = 0.0F;
    }
}

float32_t BoostConverter::calculateVoltageCurrent(float32_t reference, float32_t measurement) {
    const float32_t upper = _mode == STARTUP ? _startup_current_max : _current_ref_max;
    const float32_t lower = clamp(_current_ref_min, 0.0F, upper);
    if (lower == upper) {
        // Pid::setBounds requires a nonempty interval; handle a fixed output here.
        _voltage_pid.reset();
        return upper;
    }
    _voltage_pid.setBounds(lower, upper);
    return _voltage_pid.calculateWithReturn(reference, measurement);
}

int8_t BoostConverter::setVBusBurstBounds(float32_t lower, float32_t upper) {
    if (!isfinite(lower) || !isfinite(upper) || lower < 0.0F || lower >= upper) {
        return -EINVAL;
    }
    _burst_VBus_min = lower;
    _burst_VBus_max = upper;
    _burst_enabled = true;
    updateBurstState();
    return 0;
}

float32_t BoostConverter::getVBusBurstMin() const { return _burst_VBus_min; }
float32_t BoostConverter::getVBusBurstMax() const { return _burst_VBus_max; }
bool BoostConverter::isBurstEnabled() const { return _burst_enabled; }
bool BoostConverter::isBurstPaused() const { return _burst_paused; }

void BoostConverter::updateBurstState() {
    if (!_initialized || !_burst_enabled || (_mode != STARTUP && _mode != BUSREG)) {
        _burst_paused = false;
        return;
    }
    if (_VBus >= _burst_VBus_max) {
        if (!_burst_paused) {
            _burst_paused = true;
            clearControlOutputs();
        }
    } else if (_VBus <= _burst_VBus_min) {
        _burst_paused = false;
    }
}
