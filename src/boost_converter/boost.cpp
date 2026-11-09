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
 * @brief Cascaded voltage/current control for a boost converter, with current-driven
 *        burst on leg 1 (docs/adr/0003-boost-adopts-current-driven-burst.md).
 */
#include "boost.h"

#include <math.h>
#include <errno.h>

namespace {
float32_t clamp(float32_t value, float32_t lower, float32_t upper) {
    return value < lower ? lower : (value > upper ? upper : value);
}

bool validMode(boost_mode mode) {
    return mode == IDLE || mode == STARTUP || mode == BUSREG || mode == MPPT;
}

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

BoostConverter::~BoostConverter() {
    releaseRippleControl();
}

void BoostConverter::releaseRippleControl() {
    delete _voltage_notch;
    delete _current1_resonator;
    delete _current2_resonator;
    _voltage_notch = nullptr;
    _current1_resonator = _current2_resonator = nullptr;
}

int8_t BoostConverter::init(boost_mode mode, float32_t V_bus,
                            const PidParams& voltageParams,
                            const PidParams& currentParams,
                            float32_t leg1CurrentLimit) {
    _initialized = false;
    _ripple_configured = _notch_enabled = _resonant_enabled = false;
    releaseRippleControl();
    _mode = IDLE;
    _ILow_ref = 0.0F;
    _ILow1_ref = 0.0F;
    _ILow2_ref = 0.0F;
    _duty = {};
    _current_ref_min = 0.0F;
    _current_ref_max = 0.0F;
    _tripped = false;
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

    _leg1_current_limit = leg1CurrentLimit;
    _current_ref_min = voltageParams.lower_bound;
    _current_ref_max = voltageParams.upper_bound;
    _duty_min = currentParams.lower_bound;
    _duty_max = currentParams.upper_bound;
    _hot_period = currentParams.Ts;
    _startup_current_max = clamp(leg1CurrentLimit, 0.0F, _current_ref_max);
    _VBus = V_bus;
    _VBus_ref = V_bus;
    _mode = mode;
    _initialized = true;
    reset();
    return 0;
}

void BoostConverter::clearControlOutputs() {
    resetVoltageNotch();
    _filtered_vbus = _VBus;
    if (_current1_resonator) _current1_resonator->reset();
    if (_current2_resonator) _current2_resonator->reset();
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
    _burst_skip = _burst_period;  // an active mode starts with every period skipped
    _tripped = false;
    _mppt_previous_power = 0.0F;
    _mppt_direction = 1.0F;
    _mppt_has_previous_power = false;
}

float32_t BoostConverter::coldLoop() {
    if (!_initialized || _mode == IDLE) {
        reset();
        return _ILow_ref;
    }
    if (_tripped) return 0.0F;

    if (_mode == STARTUP && _VBus_ref > 0.0F && _VBus >= 0.95F * _VBus_ref) {
        setMode(BUSREG);
    }

    if (_mode == MPPT) {
        Mppt();
        return _ILow_ref;
    }
    scheduleVoltageGain();
    _ILow_ref = calculateVoltageCurrent(_VBus_ref, _notch_enabled ? _filtered_vbus : _VBus);
    splitCurrentReference();
    return _ILow_ref;
}

bool BoostConverter::checkProtection() {
    if (_tripped) return true;
    if (_protection_enabled &&
        (_VBus > _trip_V || _ILow1 > _trip_A || _ILow2 > _trip_A)) {
        _tripped = true;
        clearControlOutputs();
        _burst_skip = _burst_period;
    }
    return _tripped;
}

void BoostConverter::runLeg1() {
    const bool burst = _burst_period > 0U;
    if (burst && _ILow2_ref > 0.0F) _burst_skip = 0U;  // no burst while leg 2 has demand
    if (burst && _protection_enabled && _VBus > _full_burst_V) {
        _burst_skip = _burst_period;  // over-voltage: skip every period, PID held
        return;
    }
    if (!burst || _burst_skip == 0U) {
        _duty.leg1 = calculateCurrentDuty(_current1_pid, _current1_resonator, _ILow1_ref, _ILow1);
        // At the lower duty bound and still above the reference: start skipping periods.
        if (burst && _ILow2_ref <= 0.0F && _duty.leg1 <= _duty_min && _ILow1 > _ILow1_ref) {
            _burst_skip = 1U;
        }
        return;
    }
    // Skipping periods: the PID and the duty are held; the skip count follows the current error.
    if (_ILow1_ref > _ILow1) {
        --_burst_skip;
    } else if (_ILow1_ref < _ILow1 && _burst_skip < _burst_period) {
        ++_burst_skip;
    }
}

BoostDutyCycles BoostConverter::hotLoop() {
    if (!_initialized || _mode == IDLE) {
        reset();
        return _duty;
    }
    if (checkProtection()) return _duty;

    runLeg1();
    if (isLeg2Enabled()) {
        _duty.leg2 = calculateCurrentDuty(_current2_pid, _current2_resonator, _ILow2_ref, _ILow2);
    } else {
        _current2_pid.reset();
        if (_current2_resonator) _current2_resonator->reset();
        _duty.leg2 = 0.0F;
    }
    return _duty;
}

void BoostConverter::Mppt() {
    if (!_initialized || _mode != MPPT || _VLow <= 0.0F || _tripped) return;

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
    if (isfinite(VBus) && VBus >= 0.0F) {
        _VBus = VBus;
        _filtered_vbus = VBus;
        if (_notch_enabled) {
            // Preserve DC at startup while using the library's zero-state reset.
            if (!_notch_primed) { _notch_offset = VBus; _notch_primed = true; }
            _filtered_vbus = _notch_offset + _voltage_notch->calculateWithReturn(VBus - _notch_offset);
        }
    }
}

int8_t BoostConverter::configureRippleControl(float32_t frequency, float32_t bandwidth,
                                              float32_t resonantGain, float32_t phase) {
    if (!_initialized || !isfinite(frequency) || frequency <= 0.0F ||
        frequency * _hot_period >= 0.5F || !isfinite(bandwidth) || bandwidth <= 0.0F ||
        bandwidth * _hot_period >= 0.5F || !isfinite(resonantGain) || resonantGain <= 0.0F ||
        !isfinite(phase)) return -EINVAL;
    _ripple_configured = _notch_enabled = _resonant_enabled = false;
    _notch_primed = false;
    const PrParams params = {_hot_period, 0.0F, resonantGain, 2.0F * PI * frequency,
                             phase, -1.0F, 1.0F};
    releaseRippleControl();
    _voltage_notch = new NotchFilter;
    const int8_t notch_result = _voltage_notch->init(_hot_period, frequency, bandwidth);
    _current1_resonator = new OwnedPr;
    const int8_t current1_result = _current1_resonator->init(params);
    _current2_resonator = new OwnedPr;
    const int8_t current2_result = _current2_resonator->init(params);
    if (notch_result != 0 || current1_result != 0 || current2_result != 0) return -EINVAL;
    _filtered_vbus = _VBus;
    _ripple_configured = true;
    _notch_enabled = _resonant_enabled = false;
    return 0;
}

void BoostConverter::enableVoltageNotch(bool enabled) {
    enabled = enabled && _ripple_configured;
    if (enabled != _notch_enabled) {
        resetVoltageNotch();
        _filtered_vbus = _VBus;
        _notch_enabled = enabled;
    }
}

void BoostConverter::enableCurrentResonant(bool enabled) {
    enabled = enabled && _ripple_configured;
    if (enabled != _resonant_enabled) {
        if (_current1_resonator) _current1_resonator->reset();
        if (_current2_resonator) _current2_resonator->reset();
        _resonant_enabled = enabled;
    }
}

void BoostConverter::resetVoltageNotch() {
    if (_voltage_notch) _voltage_notch->reset();
    _notch_primed = false;
}

float32_t BoostConverter::calculateCurrentDuty(Pid& pid, Pr* resonator,
                                               float32_t reference, float32_t measurement) {
    const float32_t pi = pid.calculateWithReturn(reference, measurement);
    const float32_t correction = _resonant_enabled ? resonator->calculateWithReturn(reference, measurement) : 0.0F;
    return clamp(pi + correction, _duty_min, _duty_max);
}

int8_t BoostConverter::setBurst(uint16_t period) {
    if (period == 0U) return -EINVAL;
    _burst_period = period;
    if (_mode == IDLE || _burst_skip > period) _burst_skip = period;
    return 0;
}
bool BoostConverter::isBurstEnabled() const { return _burst_period > 0U; }
uint16_t BoostConverter::getBurstPeriod() const { return _burst_period; }
uint16_t BoostConverter::getBurstSkip() const { return _burst_skip; }
bool BoostConverter::isBurstActive() const { return _burst_skip > 0U; }

int8_t BoostConverter::setProtection(float32_t fullBurstV, float32_t tripV, float32_t tripA) {
    if (!isfinite(fullBurstV) || !isfinite(tripV) || !isfinite(tripA) ||
        fullBurstV <= 0.0F || tripV <= fullBurstV || tripA <= 0.0F) {
        return -EINVAL;
    }
    _full_burst_V = fullBurstV;
    _trip_V = tripV;
    _trip_A = tripA;
    _protection_enabled = true;
    return 0;
}
bool BoostConverter::isTripped() const { return _tripped; }

int8_t BoostConverter::setVoltageGainSchedule(float32_t gain, float32_t vinMin, float32_t vinMax,
                                              float32_t vbusMin, float32_t vbusMax) {
    if (!isfinite(gain) || !isfinite(vinMin) || !isfinite(vinMax) ||
        !isfinite(vbusMin) || !isfinite(vbusMax) || gain <= 0.0F ||
        vinMin <= 0.0F || vinMin > vinMax || vbusMin <= 0.0F || vbusMin > vbusMax) {
        return -EINVAL;
    }
    _gain = gain;
    _vin_min = vinMin;
    _vin_max = vinMax;
    _vbus_min = vbusMin;
    _vbus_max = vbusMax;
    _gain_schedule_enabled = true;
    return 0;
}
float32_t BoostConverter::getVoltageKp() const { return _voltage_pid.getKp(); }

void BoostConverter::scheduleVoltageGain() {
    if (!_gain_schedule_enabled) return;
    _voltage_pid.setKp(_gain * clamp(_VBus_ref, _vbus_min, _vbus_max) /
                       clamp(_VLow, _vin_min, _vin_max));
}

float32_t BoostConverter::getILowRef() const { return _ILow_ref; }
float32_t BoostConverter::getILow1Ref() const { return _ILow1_ref; }
float32_t BoostConverter::getILow2Ref() const { return _ILow2_ref; }
float32_t BoostConverter::getLeg1CurrentLimit() const { return _leg1_current_limit; }
bool BoostConverter::isLeg1Enabled() const {
    return _initialized && _mode != IDLE && !_tripped;
}
bool BoostConverter::isLeg2Enabled() const {
    return _initialized && _mode != IDLE && !_tripped && _ILow2_ref > 0.0F;
}
float32_t BoostConverter::getVBusRef() const { return _VBus_ref; }
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
    const boost_mode next = validMode(mode) ? mode : IDLE;
    const boost_mode previous = _mode;
    _mode = next;
    if (next == IDLE || previous == IDLE) {
        reset();
        return;
    }
    // Between active modes nothing is reset: only the new mode's bounds apply.
    if (next == MPPT && previous != MPPT) {
        _mppt_has_previous_power = false;
        _mppt_direction = 1.0F;
    }
    splitCurrentReference();
}

void BoostConverter::splitCurrentReference() {
    if (_mode == STARTUP) {
        _ILow_ref = clamp(_ILow_ref, 0.0F, _startup_current_max);
        _ILow1_ref = _ILow_ref;
        _ILow2_ref = 0.0F;
    } else {
        _ILow1_ref = clamp(_ILow_ref, 0.0F, _leg1_current_limit);
        _ILow2_ref = _ILow_ref - _ILow1_ref;
    }
    // Stop and clear leg 2 immediately when it has no demand. Leg 1 keeps its PID:
    // at zero reference it runs at minimum duty and burst skips its periods.
    if (_ILow2_ref <= 0.0F) {
        if (_initialized) _current2_pid.reset();
        if (_current2_resonator) _current2_resonator->reset();
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
