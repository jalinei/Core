/*
 *
 * Copyright (c) 2026 OWNTECH TECHNOLOGIES
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
 * @file test_grid_observer.cpp
 * @brief L0-GOB: open-loop tests of the grid observer (src/grid_observer).
 *
 * Test cases are defined in docs/testing/test-procedure.md §6.2; how to run
 * them is in the README next to this file.
 *
 * | Test case | Behaviour |
 * |---|---|
 * | L0-GOB-01 | Not operable before init; invalid configurations rejected |
 * | L0-GOB-02 | Qualification dwell; no nuisance trip when the grid is absent |
 * | L0-GOB-03 | RMS voltage and frequency estimates |
 * | L0-GOB-04 | UV, OV, UF, OF trips after their delay |
 * | L0-GOB-05 | Trip latch and clearing conditions |
 * | L0-GOB-06 | Invalid measurements trip at once |
 *
 * Binary-exact periods (multiples of 1/128 s) allow checks at the exact dwell
 * boundary: 3 ticks of voltage delay, 4 of frequency delay, 8 of qualification.
 * The application uses 0.1 s, 0.1 s and 1 s at 10 kHz; the timing logic is the
 * same.
 */

#include "gridObserver.h"
#include "check.h"

#include <cmath>
#include <limits>
#include <string>

namespace {
/** @brief Test configuration: application window (207–253 V, 47.5–52.5 Hz) with 1/128 s timing. */
constexpr GridObserverConfig config{1.0F / 128.0F, 207.0F, 253.0F,
                                    47.5F, 52.5F, 3.0F / 128.0F,
                                    4.0F / 128.0F, 8.0F / 128.0F};
constexpr float pi = 3.14159265358979323846F;

/**
 * @brief Feeds one sample of a balanced alpha/beta grid voltage to the observer.
 * @param observer Observer under test.
 * @param rms Grid RMS voltage in V.
 * @param hz Grid frequency in Hz.
 * @param phase Grid angle in rad.
 * @return The value returned by GridObserver::update(), i.e. whether the observer allows operation.
 */
bool tick(GridObserver &observer, float rms = 230.0F, float hz = 50.0F, float phase = 0.0F) {
    return observer.update(rms * std::sqrt(2.0F) * std::cos(phase),
                           rms * std::sqrt(2.0F) * std::sin(phase), 2.0F * pi * hz);
}

/**
 * @brief Runs a full qualification dwell on a healthy grid and checks its length.
 * @param c Criterion that records the observations.
 * @param observer Observer under test, freshly initialized or cleared.
 */
void qualify(tc::Criterion &c, GridObserver &observer) {
    for (int i = 0; i < 7; ++i) c.check("not operable during dwell", !tick(observer));
    c.check("operable after full dwell", tick(observer));
}
}  // namespace

/**
 * @brief Runs every L0-GOB criterion in order.
 * @return 0 when all criteria pass, 1 otherwise (see tc::finish()).
 */
int main() {
    GridObserver observer;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    {
        /** @test L0-GOB-01.a: Not operable before init */
        tc::Criterion c("L0-GOB-01.a", "Not operable before init");
        c.check("canOperate() false", !observer.canOperate());
        c.check("update() false", !tick(observer));
        c.check("clearTrip() refused", !observer.clearTrip(false));
        c.check("init accepts the config", observer.init(config));
    }

    {
        /** @test L0-GOB-02.a: Missing grid at startup blocks operation without a nuisance trip */
        tc::Criterion c("L0-GOB-02.a", "Missing grid at startup blocks operation without a nuisance trip");
        for (int i = 0; i < 100; ++i) c.check("0 V: not operable", !tick(observer, 0.0F));
        c.check("not tripped", !observer.isTripped());
    }

    {
        /** @test L0-GOB-02.b: Qualification dwell restarts on a dropout */
        tc::Criterion c("L0-GOB-02.b", "Qualification dwell restarts on a dropout");
        for (int i = 0; i < 5; ++i) c.check("dwell in progress", !tick(observer));
        c.check("dropout: not operable", !tick(observer, 0.0F));
        qualify(c, observer);
    }

    {
        /** @test L0-GOB-03.a: RMS voltage and frequency estimates on a clean 230 V / 50 Hz grid */
        tc::Criterion c("L0-GOB-03.a", "RMS voltage and frequency estimates on a clean 230 V / 50 Hz grid");
        for (int i = 0; i < 100; ++i) {
            c.check("operable", tick(observer, 230.0F, 50.0F, 2.0F * pi * i / 100.0F));
            c.near("RMS voltage (V)", observer.getVoltageRms(), 230.0, 0.001);
            c.near("frequency (Hz)", observer.getFrequencyHz(), 50.0, 0.001);
        }
    }

    const GridTripReason reasons[] = {GridTripReason::UnderVoltage, GridTripReason::OverVoltage,
                                      GridTripReason::UnderFrequency, GridTripReason::OverFrequency};
    const char *names[] = {"UV (200 V)", "OV (260 V)", "UF (45 Hz)", "OF (55 Hz)"};
    const char *letters[] = {"a", "b", "c", "d"};
    for (unsigned int limit = 0; limit < 4; ++limit) {
        const float rms = limit == 0 ? 200.0F : (limit == 1 ? 260.0F : 230.0F);
        const float hz = limit == 2 ? 45.0F : (limit == 3 ? 55.0F : 50.0F);
        const int delay_ticks = limit < 2 ? 3 : 4;
        {
            /** @test L0-GOB-04.a–d: UV (200 V), OV (260 V), UF (45 Hz), OF (55 Hz) trip after their delay; a shorter event does not */
            tc::Criterion c(std::string("L0-GOB-04.") + letters[limit],
                            std::string(names[limit]) + " trips after its delay; a shorter event does not");
            c.check("re-init", observer.init(config));
            qualify(c, observer);
            // A short event resets when the grid returns to normal.
            for (int i = 1; i < delay_ticks; ++i) c.check("short event: still operable", tick(observer, rms, hz));
            c.check("back to normal", tick(observer));
            for (int i = 1; i < delay_ticks; ++i) c.check("before delay: still operable", tick(observer, rms, hz));
            c.check("at delay: trips", !tick(observer, rms, hz));
            c.check("tripped", observer.isTripped());
            c.check("trip reason", observer.getTripReason() == reasons[limit]);
        }
        {
            /** @test L0-GOB-05.a–d: each trip latches; clearing needs a healthy interval and an open relay */
            tc::Criterion c(std::string("L0-GOB-05.") + letters[limit],
                            std::string(names[limit]) + " trip latches; clearing needs a healthy interval and open relay");
            c.check("clear refused during fault", !observer.clearTrip(false));
            for (int i = 0; i < 8; ++i) c.check("latched while healthy", !tick(observer));
            c.check("clear refused with relay closed", !observer.clearTrip(true));
            c.check("clear accepted with relay open", observer.clearTrip(false));
            c.check("operable after clear", observer.canOperate());
        }
    }

    {
        /** @test L0-GOB-04.e: Opposite voltage excursions do not accumulate into one timer */
        tc::Criterion c("L0-GOB-04.e", "Opposite voltage excursions do not accumulate into one timer");
        for (int i = 0; i < 10; ++i) {
            c.check("200 V tick: operable", tick(observer, 200.0F));
            c.check("260 V tick: operable", tick(observer, 260.0F));
        }
    }

    const char *inputs[] = {"NaN alpha", "infinite beta", "NaN frequency", "validity flag false"};
    for (int input = 0; input < 4; ++input) {
        /** @test L0-GOB-06.a–d: NaN alpha, infinite beta, NaN frequency or a false validity flag trip at once */
        tc::Criterion c(std::string("L0-GOB-06.") + letters[input],
                        std::string("Invalid measurement (") + inputs[input] +
                            ") trips at once; recovery qualification restarts on a dropout");
        c.check("re-init", observer.init(config));
        qualify(c, observer);
        c.check("invalid sample: not operable",
                !observer.update(input == 0 ? nan : 325.0F, input == 1 ? inf : 0.0F,
                                 input == 2 ? nan : 2.0F * pi * 50.0F, input != 3));
        c.check("reason InvalidMeasurement", observer.getTripReason() == GridTripReason::InvalidMeasurement);
        for (int i = 0; i < 8; ++i) c.check("latched while healthy", !tick(observer));
        c.check("dropout during recovery", !tick(observer, 0.0F));
        c.check("clear refused: qualification lost", !observer.clearTrip(false));
        observer.update(nan, nan, nan);
        c.check("reason unchanged", observer.getTripReason() == GridTripReason::InvalidMeasurement);
    }

    {
        /** @test L0-GOB-04.f: Negative frequency trips as under-frequency; the first cause is kept */
        tc::Criterion c("L0-GOB-04.f", "Negative frequency trips as under-frequency; the first cause is kept");
        c.check("re-init", observer.init(config));
        qualify(c, observer);
        for (int i = 0; i < 4; ++i) tick(observer, 230.0F, -50.0F);
        c.check("reason UnderFrequency", observer.getTripReason() == GridTripReason::UnderFrequency);
        observer.update(nan, nan, nan);
        c.check("first cause kept", observer.getTripReason() == GridTripReason::UnderFrequency);
    }

    {
        /** @test L0-GOB-04.g: Zero qualification time and zero delay act on the first sample */
        tc::Criterion c("L0-GOB-04.g", "Zero qualification time and zero delay act on the first sample");
        GridObserverConfig immediate = config;
        immediate.qualification_time_s = 0.0F;
        immediate.voltage_trip_delay_s = 0.0F;
        c.check("init", observer.init(immediate));
        c.check("operable on first healthy sample", tick(observer));
        c.check("trips on first bad sample", !tick(observer, 0.0F));
    }

    {
        /** @test L0-GOB-01.b: Invalid configurations are rejected and leave the observer inoperable */
        tc::Criterion c("L0-GOB-01.b", "Invalid configurations are rejected and leave the observer inoperable");
        GridObserverConfig invalid[] = {config, config, config, config, config, config};
        invalid[0].sample_period_s = 0.0F;
        invalid[1].voltage_max_rms = invalid[1].voltage_min_rms;
        invalid[2].frequency_min_hz = -1.0F;
        invalid[3].voltage_trip_delay_s = -1.0F;
        invalid[4].qualification_time_s = inf;
        invalid[5].frequency_max_hz = nan;
        for (const auto &settings : invalid) {
            c.check("init rejects", !observer.init(settings));
            c.check("update() false", !tick(observer));
            c.check("not operable", !observer.canOperate());
        }
    }

    return tc::finish();
}
