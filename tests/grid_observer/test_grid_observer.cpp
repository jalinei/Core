#include "gridObserver.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
// Binary-exact periods allow assertions at the exact dwell boundary.
constexpr GridObserverConfig config{1.0F / 128.0F, 207.0F, 253.0F,
                                    47.5F, 52.5F, 3.0F / 128.0F,
                                    4.0F / 128.0F, 8.0F / 128.0F};
constexpr float pi = 3.14159265358979323846F;
bool tick(GridObserver &observer, float rms = 230.0F, float hz = 50.0F,
          float phase = 0.0F) {
    return observer.update(rms * std::sqrt(2.0F) * std::cos(phase),
                           rms * std::sqrt(2.0F) * std::sin(phase), 2.0F * pi * hz);
}
void qualify(GridObserver &observer) {
    for (int i = 0; i < 7; ++i) assert(!tick(observer));
    assert(tick(observer));
}
}

int main() {
    GridObserver observer;
    assert(!observer.canOperate());
    assert(!tick(observer));
    assert(!observer.clearTrip(false));
    assert(observer.init(config));
    // Missing grid at startup blocks connection without a nuisance trip.
    for (int i = 0; i < 100; ++i) assert(!tick(observer, 0.0F));
    assert(!observer.isTripped());
    for (int i = 0; i < 5; ++i) assert(!tick(observer));
    assert(!tick(observer, 0.0F)); // Restart the healthy dwell.
    qualify(observer);
    for (int i = 0; i < 100; ++i) {
        assert(tick(observer, 230.0F, 50.0F, 2.0F * pi * i / 100.0F));
        assert(std::fabs(observer.getVoltageRms() - 230.0F) < 0.001F);
        assert(std::fabs(observer.getFrequencyHz() - 50.0F) < 0.001F);
    }

    const GridTripReason reasons[] = {GridTripReason::UnderVoltage,
        GridTripReason::OverVoltage, GridTripReason::UnderFrequency,
        GridTripReason::OverFrequency};
    for (unsigned int limit = 0; limit < 4; ++limit) {
        assert(observer.init(config));
        qualify(observer);
        const float rms = limit == 0 ? 200.0F : (limit == 1 ? 260.0F : 230.0F);
        const float hz = limit == 2 ? 45.0F : (limit == 3 ? 55.0F : 50.0F);
        const int delay_ticks = limit < 2 ? 3 : 4;
        // A short event resets when the grid returns to normal.
        for (int i = 1; i < delay_ticks; ++i) assert(tick(observer, rms, hz));
        assert(tick(observer));
        for (int i = 1; i < delay_ticks; ++i) assert(tick(observer, rms, hz));
        assert(!tick(observer, rms, hz));
        assert(observer.isTripped());
        assert(observer.getTripReason() == reasons[limit]);
        assert(!observer.clearTrip(false));
        // Recovery needs a complete healthy interval and open connection feedback.
        for (int i = 0; i < 8; ++i) assert(!tick(observer));
        assert(!observer.clearTrip(true));
        assert(observer.clearTrip(false));
        assert(observer.canOperate());
    }
    // Opposite voltage excursions do not accumulate into one timer.
    for (int i = 0; i < 10; ++i) {
        assert(tick(observer, 200.0F));
        assert(tick(observer, 260.0F));
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int input = 0; input < 4; ++input) {
        assert(observer.init(config));
        qualify(observer);
        assert(!observer.update(input == 0 ? nan : 325.0F, input == 1 ? inf : 0.0F,
                                input == 2 ? nan : 2.0F * pi * 50.0F, input != 3));
        assert(observer.getTripReason() == GridTripReason::InvalidMeasurement);
        for (int i = 0; i < 8; ++i) assert(!tick(observer));
        assert(!tick(observer, 0.0F)); // Recovery qualification is lost again.
        assert(!observer.clearTrip(false));
        observer.update(nan, nan, nan);
        assert(observer.getTripReason() == GridTripReason::InvalidMeasurement);
    }
    assert(observer.init(config));
    qualify(observer);
    for (int i = 0; i < 4; ++i) tick(observer, 230.0F, -50.0F);
    assert(observer.getTripReason() == GridTripReason::UnderFrequency);
    observer.update(nan, nan, nan);
    assert(observer.getTripReason() == GridTripReason::UnderFrequency); // First cause.

    GridObserverConfig immediate = config;
    immediate.qualification_time_s = 0.0F;
    immediate.voltage_trip_delay_s = 0.0F;
    assert(observer.init(immediate));
    assert(tick(observer));
    assert(!tick(observer, 0.0F));

    GridObserverConfig invalid[] = {config, config, config, config, config, config};
    invalid[0].sample_period_s = 0.0F;
    invalid[1].voltage_max_rms = invalid[1].voltage_min_rms;
    invalid[2].frequency_min_hz = -1.0F;
    invalid[3].voltage_trip_delay_s = -1.0F;
    invalid[4].qualification_time_s = inf;
    invalid[5].frequency_max_hz = nan;
    for (const auto &settings : invalid) {
        assert(!observer.init(settings));
        assert(!tick(observer));
        assert(!observer.canOperate());
    }
    std::puts("Grid observer qualification, timing, trip and recovery tests passed.");
}
