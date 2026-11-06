#include "singlePhaseInverter.h"
#include "gridObserver.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <initializer_list>

namespace {
constexpr float ts = 0.0001F;
constexpr float w = 2.0F * PI * 50.0F;
constexpr float peak = 20.0F;
template <typename Controller>
struct InverterFixture {
    Controller inverter;
    float phase = 0.0F;
    float sample_period = ts;
    float grid_w = w;
    bool acknowledge_pwm = true;
    InverterFixture() {
        if constexpr (std::is_same_v<Controller, singlePhaseInverterFollowing>)
            assert(inverter.init(100.0F, peak, w, ts) == 0);
        else
            assert(inverter.init(100.0F, w, ts) == 0);
    }
    float tick(float amplitude = peak, float current = 0.0F) {
        phase = std::fmod(phase + grid_w * sample_period, 2.0F * PI);
        const float duty = inverter.calculateDuty(amplitude * std::cos(phase), current * std::cos(phase));
        if constexpr (std::is_same_v<Controller, singlePhaseInverterFollowing>) {
            if (acknowledge_pwm) inverter.setPwmEnabled(inverter.isPwmRequested());
        }
        return duty;
    }
    void connect() {
        inverter.requestConnect();
        for (int n = 0; n < 100000 && !inverter.isGridConnectionRequested(); ++n) tick();
        assert(inverter.getMode() == VSI_GRID_CONNECT);
        assert(inverter.isPwmRequested());
        // Connection requires explicit feedback, even when RUN was requested.
        tick();
        assert(inverter.getMode() == VSI_GRID_CONNECT);
        inverter.setGridConnected(true);
        tick();
        assert(inverter.getMode() == VSI_GRID_CONNECTED);
    }
};
using Grid = InverterFixture<singlePhaseInverterFollowing>;
using Island = InverterFixture<singlePhaseInverterForming>;
void near(float a, float b) {
    if (!(std::fabs(a-b) < 0.00005F)) std::fprintf(stderr, "Expected %.9g, got %.9g\n", b, a);
    assert(std::fabs(a-b) < 0.00005F);
}
}
int main() {
    // Hardware starts at neutral before linking the unchanged full-amplitude
    // waveform at a near-neutral duty, for both nominal grid frequencies/rates.
    for (float frequency : {50.0F, 60.0F}) {
        for (float period : {0.0001F, 0.0002F}) {
            Grid starting;
            starting.sample_period = period;
            starting.grid_w = 2.0F * PI * frequency;
            const float mains_peak = period == ts ? 325.27F : peak;
            assert(starting.inverter.setPwmStartDutyTolerance(0.01F));
            assert(starting.inverter.init(400.0F, mains_peak, starting.grid_w, period) == 0);
            assert(starting.inverter.getPwmStartDutyTolerance() == 0.01F);
            for (float invalid : {0.0F, -0.01F, 0.5F,
                                 std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity()}) {
                assert(!starting.inverter.setPwmStartDutyTolerance(invalid));
                assert(starting.inverter.getPwmStartDutyTolerance() == 0.01F);
            }
            starting.inverter.requestConnect();
            float duty = 0.0F;
            for (int n = 0; n < 100000 && !starting.inverter.isPwmRequested(); ++n)
                duty = starting.tick(mains_peak);
            assert(starting.inverter.getMode() == VSI_PWM_WAIT);
            assert(duty == 0.5F); // First enable always has exactly neutral duty.
            assert(!starting.inverter.isGridConnectionRequested());
            for (int n = 0; n < 1000 && starting.inverter.getMode() == VSI_PWM_WAIT; ++n) {
                duty = starting.tick(mains_peak);
                if (starting.inverter.getMode() == VSI_PWM_WAIT) {
                    assert(duty == 0.5F);
                    assert(std::fabs(starting.inverter.getVab().alpha / 800.0F) > 0.00999F);
                }
            }
            assert(starting.inverter.getMode() == VSI_PWM_SETTLE);
            assert(std::fabs(duty - 0.5F) <= 0.01F);
            assert(!starting.inverter.isGridConnectionRequested());
            // Once linked, output follows computed feedforward with full amplitude.
            for (int n = 0; n < 100; ++n) {
                starting.tick(mains_peak);
                near(starting.inverter.getVdqOut().d, starting.inverter.getVdq().d);
                near(starting.inverter.getVdqOut().q, starting.inverter.getVdq().q);
            }
        }
    }
    for (uint16_t configured_cycles : {1, 25}) {
        Grid held;
        assert(held.inverter.getPwmSettleCycles() == 10);
        held.inverter.setPwmSettleCycles(0);
        assert(held.inverter.init(100.0F, peak, w, ts) == 0);
        assert(held.inverter.getPwmSettleCycles() == 0); // Configuration survives init.
        held.inverter.requestConnect();
        for (int n = 0; n < 100000 && held.inverter.getMode() != VSI_PWM_SETTLE; ++n)
            held.tick();
        assert(held.inverter.getMode() == VSI_PWM_SETTLE);
        for (int n = 0; n < 70000; ++n) {
            held.tick(); // Hold well beyond the former dwell and connection timeout.
            assert(held.inverter.getMode() == VSI_PWM_SETTLE);
            assert(held.inverter.getSync());
            assert(held.inverter.isPwmRequested());
            assert(!held.inverter.isGridConnectionRequested());
        }
        held.inverter.setPwmSettleCycles(configured_cycles);
        assert(held.inverter.getPwmSettleCycles() == configured_cycles);
        double cycles = 0.0;
        for (int n = 0; n < 10000 && !held.inverter.isGridConnectionRequested(); ++n) {
            held.inverter.setPwmSettleCycles(configured_cycles); // Same setting preserves progress.
            held.tick();
            cycles += static_cast<double>(held.inverter.getw()) * ts / (2.0 * PI);
            if (cycles < configured_cycles) assert(!held.inverter.isGridConnectionRequested());
        }
        assert(cycles >= configured_cycles && cycles < configured_cycles + 0.02);
        assert(held.inverter.getMode() == VSI_GRID_CONNECT);
    }
    Grid reconfigured;
    reconfigured.inverter.requestConnect();
    for (int n = 0; n < 100000 && reconfigured.inverter.getMode() != VSI_PWM_SETTLE; ++n)
        reconfigured.tick();
    assert(reconfigured.inverter.getMode() == VSI_PWM_SETTLE);
    for (int n = 0; n < 1500; ++n) reconfigured.tick();
    reconfigured.inverter.setPwmSettleCycles(20);
    for (int n = 0; n < 3000; ++n) reconfigured.tick();
    assert(!reconfigured.inverter.isGridConnectionRequested()); // Prior dwell was erased.
    reconfigured.inverter.setPwmSettleCycles(0); // Can also hold an in-progress dwell.
    for (int n = 0; n < 5000; ++n) reconfigured.tick();
    assert(reconfigured.inverter.getMode() == VSI_PWM_SETTLE);
    assert(!reconfigured.inverter.isGridConnectionRequested());
    // Qualification alone cannot request the relay. PWM must be acknowledged
    // and remain synchronized for ten complete measured periods at either rate.
    for (float frequency : {50.0F, 60.0F}) {
        for (float period : {0.0001F, 0.0002F}) {
            Grid settling;
            settling.sample_period = period;
            settling.grid_w = 2.0F * PI * frequency;
            settling.acknowledge_pwm = false;
            assert(settling.inverter.init(100.0F, peak, settling.grid_w, period) == 0);
            settling.inverter.requestRun();
            settling.inverter.requestConnect();
            for (int n = 0; n < 100000 && settling.inverter.getMode() != VSI_PWM_WAIT; ++n)
                settling.tick();
            assert(settling.inverter.getMode() == VSI_PWM_WAIT);
            for (int n = 0; n < 3000; ++n) {
                settling.tick();
                assert(settling.inverter.isPwmRequested());
                assert(settling.inverter.getSync());
                assert(!settling.inverter.isGridConnectionRequested());
                near(settling.inverter.getVdqOut().d, 0.0F);
                near(settling.inverter.getVdqOut().q, 0.0F);
                assert(settling.inverter.getMode() == VSI_PWM_WAIT);
            }
            settling.inverter.setPwmEnabled(true);
            float linked_duty = 0.5F;
            for (int n = 0; n < 1000 && settling.inverter.getMode() != VSI_PWM_SETTLE; ++n)
                linked_duty = settling.tick();
            assert(settling.inverter.getMode() == VSI_PWM_SETTLE);
            assert(std::fabs(linked_duty - 0.5F) <= settling.inverter.getPwmStartDutyTolerance());
            for (int n = 0; n < 400; ++n) settling.tick();
            assert(!settling.inverter.isGridConnectionRequested());
            settling.inverter.setPwmEnabled(false); // Interrupted PWM erases prior dwell.
            for (int n = 0; n < 400; ++n) settling.tick();
            assert(!settling.inverter.isGridConnectionRequested());
            settling.inverter.setPwmEnabled(true);
            double cycles = 0.0;
            for (int n = 0; n < 10000 && !settling.inverter.isGridConnectionRequested(); ++n) {
                settling.tick();
                cycles += static_cast<double>(settling.inverter.getw()) * period / (2.0 * PI);
                if (cycles < 10.0) assert(!settling.inverter.isGridConnectionRequested());
            }
            assert(cycles >= 10.0 && cycles < 10.02);
            assert(settling.inverter.getMode() == VSI_GRID_CONNECT);
            settling.inverter.setPwmEnabled(false);
            settling.inverter.setGridConnected(true);
            settling.tick();
            assert(settling.inverter.getFaultReason() == VSI_FAULT_PWM_UPDATE_FAILED);
        }
    }
    for (int interruption = 0; interruption < 5; ++interruption) {
        Grid settling;
        settling.inverter.setPwmSettleCycles(0); // Hold retains all fault/cancellation checks.
        settling.inverter.requestConnect();
        for (int n = 0; n < 100000 && settling.inverter.getMode() != VSI_PWM_SETTLE; ++n)
            settling.tick();
        assert(settling.inverter.getMode() == VSI_PWM_SETTLE);
        for (int n = 0; n < 500; ++n) settling.tick();
        if (interruption == 0) settling.phase += PI / 2.0F;
        if (interruption == 1) settling.inverter.setGridQuality(false);
        if (interruption == 2) settling.inverter.requestDisconnect();
        if (interruption == 3) settling.inverter.requestSync();
        if (interruption == 4) settling.inverter.setGridConnected(true);
        settling.tick();
        assert(!settling.inverter.isGridConnectionRequested());
        assert(!settling.inverter.isPwmRequested());
        if (interruption == 0) assert(settling.inverter.getFaultReason() == VSI_FAULT_SYNC_LOST);
        if (interruption == 1) assert(settling.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
        if (interruption == 2) assert(settling.inverter.getMode() == VSI_OFF);
        if (interruption == 3) assert(settling.inverter.getMode() == VSI_VOLTAGE_SYNC);
        if (interruption == 4) assert(settling.inverter.getFaultReason() == VSI_FAULT_EXTERNAL);
    }
    Grid sync_only;
    assert(!sync_only.inverter.getSync());
    sync_only.inverter.requestRun();
    sync_only.inverter.requestSync();
    // Hold beyond the qualification dwell and the uint16_t counter range.
    for (int n = 0; n < 100000; ++n) {
        sync_only.inverter.requestSync();
        sync_only.tick();
        assert(sync_only.inverter.getMode() == VSI_VOLTAGE_SYNC);
        assert(!sync_only.inverter.isPwmRequested());
        assert(!sync_only.inverter.isGridConnectionRequested());
    }
    assert(sync_only.inverter.getSync()); // Qualified without requesting relay closure.
    sync_only.inverter.requestDisconnect();
    sync_only.tick();
    assert(sync_only.inverter.getMode() == VSI_OFF);
    sync_only.inverter.requestSync();
    for (int n = 0; n < 2000; ++n) sync_only.tick();
    sync_only.connect();
    sync_only.tick();
    assert(sync_only.inverter.getMode() == VSI_GRID_CONNECTED); // RUN was cancelled.
    sync_only.inverter.requestSync();
    sync_only.tick();
    assert(sync_only.inverter.getMode() == VSI_DISCONNECT);
    assert(!sync_only.inverter.isPwmRequested());
    assert(!sync_only.inverter.isGridConnectionRequested());
    sync_only.inverter.setGridConnected(false);
    sync_only.tick();
    sync_only.tick();
    assert(sync_only.inverter.getMode() == VSI_VOLTAGE_SYNC);
    sync_only.inverter.init(100.0F, peak, w, ts);
    sync_only.inverter.requestSync();
    for (int n = 0; n < 10000; ++n) sync_only.tick();
    assert(sync_only.inverter.getMode() == VSI_VOLTAGE_SYNC);

    Grid grid;
    assert(grid.inverter.getMode() == VSI_OFF);
    assert(grid.inverter.getFaultReason() == VSI_FAULT_NONE);
    near(grid.tick(), 0.5F);
    assert(!grid.inverter.isPwmRequested());
    grid.inverter.requestRun();
    grid.tick();
    assert(grid.inverter.getMode() == VSI_OFF); // RUN alone cannot connect.
    grid.connect();
    grid.inverter.requestStop();
    grid.inverter.setIdqRef({2.0F, -1.0F, 0.0F});
    grid.tick();
    assert(grid.inverter.getMode() == VSI_GRID_CONNECTED);
    near(grid.inverter.getVdqOut().d, grid.inverter.getVdq().d);
    near(grid.inverter.getVdqOut().q, grid.inverter.getVdq().q);

    grid.inverter.requestRun();
    grid.tick();
    assert(grid.inverter.getMode() == VSI_RUN);
    // Full reference on the first RUN step: real PI response, with no ramp.
    constexpr float gain = 0.001F;
    constexpr float integral_step = ts / 0.0003F;
    near(grid.inverter.getVdqOut().d - grid.inverter.getVdq().d,
         gain * (2.0F + integral_step * 2.0F));
    near(grid.inverter.getVdqOut().q - grid.inverter.getVdq().q,
         gain * (-1.0F - integral_step));
    grid.inverter.setIdqRef({-3.0F, 4.0F, 0.0F});
    grid.tick();
    near(grid.inverter.getVdqOut().d - grid.inverter.getVdq().d,
         gain * (-3.0F + integral_step * (2.0F - 3.0F)));
    near(grid.inverter.getVdqOut().q - grid.inverter.getVdq().q,
         gain * (4.0F + integral_step * (-1.0F + 4.0F)));
    grid.inverter.requestStop();
    grid.tick();
    assert(grid.inverter.getMode() == VSI_GRID_CONNECTED);

    // Disconnection keeps the relay requested until measured dq current decays.
    for (int n = 0; n < 200; ++n) grid.tick(peak, 1.0F);
    grid.inverter.requestDisconnect();
    grid.tick(peak, 1.0F);
    assert(grid.inverter.getMode() == VSI_STOPPING);
    assert(grid.inverter.isGridConnectionRequested());
    assert(grid.inverter.isPwmRequested());
    for (int n = 0; n < 1000 && grid.inverter.isGridConnectionRequested(); ++n) grid.tick();
    assert(grid.inverter.getMode() == VSI_DISCONNECT);
    assert(!grid.inverter.isPwmRequested());
    assert(!grid.inverter.isGridConnectionRequested());
    // DISCONNECT remains an opening request even if measured current rises again.
    for (int n = 0; n < 200; ++n) grid.tick(peak, 1.0F);
    assert(grid.inverter.getMode() == VSI_DISCONNECT);
    assert(!grid.inverter.isPwmRequested());
    assert(!grid.inverter.isGridConnectionRequested());
    grid.inverter.setGridConnected(false);
    grid.tick();
    assert(grid.inverter.getMode() == VSI_OFF);
    near(grid.inverter.getVdqOut().q, 0.0F);

    Grid no_grid;
    no_grid.inverter.requestConnect();
    for (int n = 0; n < 70000; ++n) no_grid.tick(0.0F);
    assert(no_grid.inverter.getMode() == VSI_VOLTAGE_SYNC);
    assert(!no_grid.inverter.getSync());
    assert(!no_grid.inverter.isGridConnectionRequested());

    Grid timeout;
    timeout.connect();
    timeout.inverter.setGridConnected(false);
    timeout.tick();
    assert(timeout.inverter.getMode() == VSI_FAULT); // Lost established connection.
    assert(timeout.inverter.getFaultReason() == VSI_FAULT_GRID_CONNECTION_LOST);
    assert(!timeout.inverter.isPwmRequested());
    timeout.inverter.requestConnect();
    timeout.inverter.requestRun();
    assert(timeout.inverter.clearFault());
    assert(timeout.inverter.getFaultReason() == VSI_FAULT_NONE);
    timeout.tick();
    assert(timeout.inverter.getMode() == VSI_OFF); // Explicit enable needed again.
    timeout.inverter.requestConnect();
    for (int n = 0; n < 100000 && timeout.inverter.getMode() != VSI_FAULT; ++n) timeout.tick();
    assert(timeout.inverter.getMode() == VSI_FAULT); // No relay feedback timeout.
    assert(timeout.inverter.getFaultReason() == VSI_FAULT_CONNECTION_TIMEOUT);
    timeout.inverter.setGridConnected(true);
    assert(!timeout.inverter.clearFault());
    timeout.inverter.setGridConnected(false);
    assert(timeout.inverter.clearFault());

    Grid resync;
    resync.connect();
    resync.inverter.setSyncOff();
    resync.tick();
    assert(resync.inverter.getMode() == VSI_DISCONNECT);
    assert(!resync.inverter.isGridConnectionRequested());
    resync.inverter.setGridConnected(false);
    resync.tick();
    assert(resync.inverter.getMode() == VSI_OFF);
    resync.tick();
    assert(resync.inverter.getMode() == VSI_VOLTAGE_SYNC);
    // Qualification restarts; one out-of-tolerance interval erases prior dwell.
    for (int n = 0; n < 500; ++n) resync.tick();
    for (int n = 0; n < 500; ++n) resync.tick(0.0F);
    for (int n = 0; n < 999; ++n) resync.tick();
    assert(resync.inverter.getMode() == VSI_VOLTAGE_SYNC);

    Grid opening_timeout;
    opening_timeout.connect();
    opening_timeout.inverter.requestDisconnect();
    for (int n = 0; n < 11000; ++n) opening_timeout.tick();
    assert(opening_timeout.inverter.getMode() == VSI_FAULT);
    assert(opening_timeout.inverter.getFaultReason() == VSI_FAULT_DISCONNECTION_TIMEOUT);
    assert(!opening_timeout.inverter.isGridConnectionRequested());

    Grid stopping_timeout;
    stopping_timeout.connect();
    for (int n = 0; n < 200; ++n) stopping_timeout.tick(peak, 1.0F);
    stopping_timeout.inverter.requestDisconnect();
    for (int n = 0; n < 11000 && stopping_timeout.inverter.getMode() != VSI_FAULT; ++n) {
        stopping_timeout.tick(peak, 1.0F);
        assert(stopping_timeout.inverter.getMode() == VSI_STOPPING ||
               stopping_timeout.inverter.getMode() == VSI_FAULT);
    }
    assert(stopping_timeout.inverter.getFaultReason() == VSI_FAULT_DISCONNECTION_TIMEOUT);
    assert(!stopping_timeout.inverter.isPwmRequested());

    Grid opening_quality;
    opening_quality.connect();
    opening_quality.inverter.requestDisconnect();
    opening_quality.tick();
    assert(opening_quality.inverter.getMode() == VSI_DISCONNECT);
    opening_quality.inverter.setGridQuality(false);
    assert(opening_quality.inverter.getMode() == VSI_FAULT);
    assert(opening_quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);

    Grid invalid;
    invalid.inverter.calculateDuty(std::numeric_limits<float>::quiet_NaN(), 0.0F);
    assert(invalid.inverter.getMode() == VSI_FAULT);
    assert(invalid.inverter.getFaultReason() == VSI_FAULT_INVALID_MEASUREMENT);
    assert(!invalid.inverter.isPwmRequested());
    near(invalid.tick(), 0.5F);

    Island forming;
    forming.inverter.setVdqRef({10.0F, 0.0F, 0.0F});
    assert(forming.inverter.isPwmRequested());
    assert(std::isfinite(forming.tick()));
    forming.inverter.tripFault();
    assert(forming.inverter.getFaultReason() == VSI_FAULT_EXTERNAL);
    assert(!forming.inverter.isPwmRequested());
    near(forming.tick(), 0.5F);
    // Forming uses its own oscillator and voltage loops without a connection sequence.
    Island oscillator;
    near(oscillator.tick(0.0F), 0.5F);
    assert(!oscillator.inverter.isPwmRequested());
    oscillator.inverter.setVdqRef({10.0F, 2.0F, 0.0F});
    oscillator.inverter.setIdqRef({0.2F, -0.1F, 0.0F});
    oscillator.tick(0.0F);
    assert(oscillator.inverter.isPwmRequested());
    assert(oscillator.inverter.getIdqRefDelta().d > 0.0F);
    assert(oscillator.inverter.getIdqRefDelta().q > 0.0F);
    assert(oscillator.inverter.getVdqOut().d > 10.0F);
    oscillator.inverter.setWRef(w * 0.9F);
    const float previous_phase = oscillator.inverter.getTheta();
    oscillator.tick(0.0F);
    near(oscillator.inverter.getw(), w * 0.9F);
    near(oscillator.inverter.getTheta(), previous_phase + w * 0.9F * ts);
    oscillator.inverter.setVdqRef({0.0F, 2.0F, 0.0F});
    near(oscillator.tick(0.0F), 0.5F);
    assert(!oscillator.inverter.isPwmRequested());
    near(oscillator.inverter.getIdqRefDelta().d, 0.0F);
    near(oscillator.inverter.getVdqOut().q, 0.0F);
    oscillator.inverter.setVBus(0.0F);
    near(oscillator.tick(), 0.5F);
    assert(oscillator.inverter.getFaultReason() == VSI_FAULT_INVALID_BUS_VOLTAGE);
    oscillator.inverter.tripFault(VSI_FAULT_EXTERNAL);
    assert(oscillator.inverter.getFaultReason() == VSI_FAULT_INVALID_BUS_VOLTAGE);
    oscillator.inverter.init(100.0F, w, ts);
    assert(oscillator.inverter.getFaultReason() == VSI_FAULT_NONE);
    oscillator.inverter.setVdqRef({10.0F, 0.0F, 0.0F});
    oscillator.inverter.calculateDuty(std::numeric_limits<float>::quiet_NaN(), 0.0F);
    assert(oscillator.inverter.getFaultReason() == VSI_FAULT_INVALID_MEASUREMENT);
    assert(!oscillator.inverter.isPwmRequested());
    assert(oscillator.inverter.clearFault());
    assert(oscillator.inverter.isPwmRequested()); // Voltage reference is retained.

    Grid quality;
    quality.inverter.setGridQuality(false);
    quality.inverter.requestConnect();
    for (int n = 0; n < 10000; ++n) quality.tick();
    assert(quality.inverter.getMode() == VSI_VOLTAGE_SYNC);
    assert(!quality.inverter.isGridConnectionRequested());
    quality.inverter.setGridQuality(true);
    quality.connect();
    quality.inverter.requestRun();
    quality.tick();
    assert(quality.inverter.getMode() == VSI_RUN);
    quality.inverter.setGridQuality(false);
    // Observer verdict drops requests immediately; no current decay or next tick needed.
    assert(quality.inverter.getMode() == VSI_FAULT);
    assert(quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
    assert(!quality.inverter.isPwmRequested());
    assert(!quality.inverter.isGridConnectionRequested());
    quality.inverter.tripFault(VSI_FAULT_EXTERNAL);
    quality.inverter.calculateDuty(std::numeric_limits<float>::quiet_NaN(), 0.0F);
    assert(quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
    quality.inverter.setGridConnected(false);
    assert(!quality.inverter.clearFault()); // Bad quality remains an active inhibit.
    quality.inverter.setGridQuality(true);
    assert(quality.inverter.getMode() == VSI_FAULT); // No automatic fault clear.
    assert(quality.inverter.clearFault());
    assert(quality.inverter.getFaultReason() == VSI_FAULT_NONE);
    quality.tick();
    assert(quality.inverter.getMode() == VSI_OFF);

    Grid connecting_quality;
    connecting_quality.inverter.requestConnect();
    for (int n = 0; n < 100000 && !connecting_quality.inverter.isGridConnectionRequested(); ++n)
        connecting_quality.tick();
    assert(connecting_quality.inverter.getMode() == VSI_GRID_CONNECT);
    connecting_quality.inverter.setGridQuality(false);
    assert(connecting_quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
    assert(!connecting_quality.inverter.isGridConnectionRequested());

    Grid connected_quality;
    connected_quality.connect();
    connected_quality.inverter.setGridQuality(false);
    assert(connected_quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);

    Grid disconnecting_quality;
    disconnecting_quality.connect();
    for (int n = 0; n < 200; ++n) disconnecting_quality.tick(peak, 1.0F);
    disconnecting_quality.inverter.requestDisconnect();
    disconnecting_quality.tick(peak, 1.0F);
    assert(disconnecting_quality.inverter.getMode() == VSI_STOPPING);
    assert(disconnecting_quality.inverter.isGridConnectionRequested());
    disconnecting_quality.inverter.setGridQuality(false);
    assert(disconnecting_quality.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
    assert(!disconnecting_quality.inverter.isGridConnectionRequested());

    Grid bus;
    bus.inverter.setVBus(0.0F);
    near(bus.tick(), 0.5F);
    assert(bus.inverter.getFaultReason() == VSI_FAULT_INVALID_BUS_VOLTAGE);
    bus.inverter.init(100.0F, peak, w, ts);
    assert(bus.inverter.getFaultReason() == VSI_FAULT_NONE);
    bus.inverter.tripFault(VSI_FAULT_PWM_UPDATE_FAILED);
    assert(bus.inverter.getFaultReason() == VSI_FAULT_PWM_UPDATE_FAILED);
    assert(bus.inverter.clearFault());
    bus.inverter.tripFault(VSI_FAULT_NONE);
    assert(bus.inverter.getFaultReason() == VSI_FAULT_EXTERNAL);

    Island island;
    island.inverter.setVdqRef({10.0F, 0.0F, 0.0F});
    assert(island.inverter.isPwmRequested());
    assert(island.inverter.getFaultReason() == VSI_FAULT_NONE);

    // Real SOGI/PLL estimates feed the observer before hardware requests are used.
    Grid supervised;
    GridObserver observer;
    const float nominal_rms = peak / std::sqrt(2.0F);
    assert(observer.init({ts, nominal_rms * 0.9F, nominal_rms * 1.1F,
                          47.5F, 52.5F, 0.02F, 0.02F, 0.1F}));
    supervised.inverter.setGridQuality(observer.canOperate());
    supervised.inverter.requestConnect();
    auto supervised_tick = [&](float amplitude) {
        supervised.tick(amplitude);
        const auto voltage = supervised.inverter.getVab();
        observer.update(voltage.alpha, voltage.beta, supervised.inverter.getw());
        supervised.inverter.setGridQuality(observer.canOperate());
    };
    for (int i = 0; i < 100000 && !supervised.inverter.isGridConnectionRequested(); ++i)
        supervised_tick(peak);
    assert(observer.canOperate());
    assert(supervised.inverter.getMode() == VSI_GRID_CONNECT);
    supervised.inverter.setGridConnected(true);
    supervised_tick(peak);
    assert(supervised.inverter.getMode() == VSI_GRID_CONNECTED);
    for (int i = 0; i < 10000 && !observer.isTripped(); ++i) supervised_tick(0.0F);
    assert(observer.isTripped());
    assert(supervised.inverter.getFaultReason() == VSI_FAULT_BAD_GRID_QUALITY);
    assert(!supervised.inverter.isPwmRequested());
    assert(!supervised.inverter.isGridConnectionRequested());
    supervised.inverter.setGridConnected(false);
    for (int i = 0; i < 20000; ++i) supervised_tick(peak);
    assert(!supervised.inverter.clearFault());
    assert(observer.clearTrip(false));
    supervised.inverter.setGridQuality(observer.canOperate());
    assert(supervised.inverter.clearFault());
    assert(supervised.inverter.getMode() == VSI_OFF);
    singlePhaseInverterFollowing uninitialized_following;
    singlePhaseInverterForming uninitialized_forming;
    near(uninitialized_following.calculateDuty(0.0F, 0.0F), 0.5F);
    near(uninitialized_forming.calculateDuty(0.0F, 0.0F), 0.5F);
    assert(uninitialized_following.getFaultReason() == VSI_FAULT_INVALID_CONFIGURATION);
    assert(uninitialized_forming.getFaultReason() == VSI_FAULT_INVALID_CONFIGURATION);

    // Invalid configuration never enables control or resets uninitialized PIs.
    const float invalid_values[] = {0.0F, -1.0F,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::denorm_min()};
    for (float value : invalid_values) {
        for (int argument = 0; argument < 4; ++argument) {
            singlePhaseInverterFollowing bad;
            float args[] = {100.0F, peak, w, ts};
            args[argument] = value;
            assert(bad.init(args[0], args[1], args[2], args[3]) < 0);
            assert(bad.getFaultReason() == VSI_FAULT_INVALID_CONFIGURATION);
            assert(!bad.isPwmRequested());
            near(bad.calculateDuty(0.0F, 0.0F), 0.5F);
            assert(!bad.clearFault());
            assert(bad.init(100.0F, peak, w, ts) == 0);
            assert(bad.getFaultReason() == VSI_FAULT_NONE);
        }
        for (int argument = 0; argument < 3; ++argument) {
            singlePhaseInverterForming bad;
            float args[] = {100.0F, w, ts};
            args[argument] = value;
            assert(bad.init(args[0], args[1], args[2]) < 0);
            assert(bad.getFaultReason() == VSI_FAULT_INVALID_CONFIGURATION);
            assert(!bad.isPwmRequested());
            near(bad.calculateDuty(0.0F, 0.0F), 0.5F);
            assert(!bad.clearFault());
        }
        Grid bad_frequency;
        bad_frequency.inverter.setWRef(value);
        assert(bad_frequency.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);
        near(bad_frequency.inverter.calculateDuty(0.0F, 0.0F), 0.5F);
        assert(bad_frequency.inverter.clearFault());
        bad_frequency.inverter.calculateDuty(0.0F, 0.0F);
        near(bad_frequency.inverter.getw(), w); // Invalid setter retained valid reference.
        Island bad_forming_frequency;
        bad_forming_frequency.inverter.setWRef(value);
        assert(bad_forming_frequency.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);
        near(bad_forming_frequency.tick(), 0.5F);
    }
    Grid nyquist;
    assert(nyquist.inverter.init(100.0F, peak, PI / ts, ts) < 0);
    Grid excessive_frequency;
    excessive_frequency.inverter.setWRef(std::numeric_limits<float>::max());
    assert(excessive_frequency.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);

    const float nonfinite[] = {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};
    for (float value : nonfinite) {
        for (bool q_axis : {false, true}) {
            const dqo_t reference = q_axis ? dqo_t{0.0F, value, 0.0F} : dqo_t{value, 0.0F, 0.0F};
            Grid bad_ref;
            bad_ref.connect();
            bad_ref.inverter.requestRun();
            bad_ref.tick();
            bad_ref.inverter.setIdqRef(reference);
            assert(bad_ref.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);
            near(bad_ref.tick(), 0.5F);
            assert(!bad_ref.inverter.isPwmRequested());
            Island bad_forming_ref;
            bad_forming_ref.inverter.setIdqRef(reference);
            assert(bad_forming_ref.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);
            near(bad_forming_ref.tick(), 0.5F);
            assert(bad_forming_ref.inverter.clearFault());
            bad_forming_ref.inverter.setVdqRef(reference);
            assert(bad_forming_ref.inverter.getFaultReason() == VSI_FAULT_INVALID_REFERENCE);
        }
    }
    // A finite reference may overflow PI arithmetic; never return a nonfinite duty.
    Grid overflow;
    overflow.connect();
    overflow.inverter.requestRun();
    overflow.inverter.setIdqRef({std::numeric_limits<float>::max(), 0.0F, 0.0F});
    near(overflow.tick(), 0.5F);
    assert(overflow.inverter.getFaultReason() == VSI_FAULT_INVALID_CONTROL_STATE);
    overflow.inverter.setGridConnected(false);
    assert(overflow.inverter.clearFault());
    assert(std::isfinite(overflow.tick()));

    Island forming_overflow;
    forming_overflow.inverter.setVdqRef({std::numeric_limits<float>::max(), 0.0F, 0.0F});
    near(forming_overflow.tick(), 0.5F);
    assert(forming_overflow.inverter.getFaultReason() == VSI_FAULT_INVALID_CONTROL_STATE);
    forming_overflow.inverter.setVdqRef({10.0F, 0.0F, 0.0F});
    assert(forming_overflow.inverter.clearFault());
    assert(std::isfinite(forming_overflow.tick()));

    Grid lost_alignment;
    lost_alignment.inverter.requestConnect();
    for (int n = 0; n < 100000 && !lost_alignment.inverter.isGridConnectionRequested(); ++n)
        lost_alignment.tick();
    assert(lost_alignment.inverter.getMode() == VSI_GRID_CONNECT);
    lost_alignment.phase += PI / 2.0F;
    lost_alignment.tick();
    assert(lost_alignment.inverter.getFaultReason() == VSI_FAULT_SYNC_LOST);
    assert(!lost_alignment.inverter.isGridConnectionRequested());
    assert(!lost_alignment.inverter.getSync());

    Grid closing_alignment;
    closing_alignment.inverter.requestConnect();
    for (int n = 0; n < 100000 && !closing_alignment.inverter.isGridConnectionRequested(); ++n)
        closing_alignment.tick();
    closing_alignment.phase += PI / 2.0F;
    closing_alignment.inverter.setGridConnected(true);
    closing_alignment.tick();
    assert(closing_alignment.inverter.getMode() == VSI_FAULT); // Feedback cannot bypass alignment check.
    assert(closing_alignment.inverter.getFaultReason() == VSI_FAULT_SYNC_LOST);

    Grid qualified;
    qualified.inverter.requestSync();
    for (int n = 0; n < 100000 && !qualified.inverter.getSync(); ++n) qualified.tick();
    assert(qualified.inverter.getSync());
    assert(!qualified.inverter.isGridConnectionRequested());
    qualified.inverter.setGridQuality(false);
    assert(!qualified.inverter.getSync());
    qualified.inverter.setGridQuality(true);
    assert(!qualified.inverter.getSync());
    for (int n = 0; n < 999; ++n) {
        qualified.tick();
        assert(!qualified.inverter.getSync());
    }
    qualified.tick();
    assert(qualified.inverter.getSync());
    qualified.phase += PI / 2.0F;
    qualified.tick();
    assert(!qualified.inverter.getSync());
    assert(qualified.inverter.getMode() == VSI_VOLTAGE_SYNC);
    std::puts("Following modes, forming control, and fault tests passed.");
}
