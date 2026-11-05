#include "boost.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace {
void near(float actual, float expected) {
    assert(std::fabs(actual - expected) < 1e-5F);
}
}

int main() {
    PidParams voltage = {0.001F, 0.1F, 1.0F, 0, 1, 0, 12};
    PidParams current = {0.0001F, 0.1F, 1.0F, 0, 1, 0, 0.95F};
    BoostConverter boost;
    assert(boost.coldLoop() == 0);
    assert(boost.hotLoop().leg1 == 0 && boost.hotLoop().leg2 == 0);
    assert(!boost.isLeg1Enabled() && !boost.isLeg2Enabled());
    assert(boost.init(BUSREG, 48, voltage, current, 5) == 0);
    assert(boost.getLeg1CurrentLimit() == 5);

    // First leg alone below and exactly at the threshold.
    for (float total : {2.0F, 5.0F}) {
        boost.setILowRef(total);
        near(boost.getILow1Ref(), total);
        assert(boost.getILow2Ref() == 0);
        assert(boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        auto duty = boost.hotLoop();
        assert(duty.leg1 > 0 && duty.leg2 == 0);
    }

    // Above the threshold, leg 1 stays at 5 A and leg 2 supplies the excess.
    boost.reset();
    boost.setILowRef(8);
    near(boost.getILow1Ref(), 5);
    near(boost.getILow2Ref(), 3);
    assert(boost.isLeg1Enabled() && boost.isLeg2Enabled());
    boost.setILow1(4);
    boost.setILow2(1);
    auto duty = boost.hotLoop();
    near(duty.leg1, 0.1F * (1 + current.Ts));
    near(duty.leg2, 0.1F * (2 + 2 * current.Ts));
    near(boost.getDutyLeg1(), duty.leg1);
    near(boost.getDutyLeg2(), duty.leg2);
    boost.setVLow(24);
    near(boost.getILowTotal(), 5);
    near(boost.getPower(), 120);

    // Removing leg 2 demand clears its output and integral before reuse.
    for (int i = 0; i < 100; ++i) boost.hotLoop();
    boost.setILowRef(5);
    assert(!boost.isLeg2Enabled() && boost.getDutyLeg2() == 0);
    boost.setILowRef(8);
    duty = boost.hotLoop();
    near(duty.leg2, 0.1F * (2 + 2 * current.Ts));
    boost.setILowRef(0);
    assert(!boost.isLeg1Enabled() && !boost.isLeg2Enabled());
    assert(boost.getDutyLeg1() == 0 && boost.getDutyLeg2() == 0);

    // Cold-loop saturation sets a total reference, then allocates both legs.
    boost.setMode(BUSREG);
    boost.setVBusRef(1000);
    near(boost.coldLoop(), 12);
    near(boost.getILow1Ref(), 5);
    near(boost.getILow2Ref(), 7);
    boost.setILow1(0);
    boost.setILow2(0);
    for (int i = 0; i < 10000; ++i) {
        duty = boost.hotLoop();
        assert(duty.leg1 >= 0 && duty.leg1 <= 0.95F);
        assert(duty.leg2 >= 0 && duty.leg2 <= 0.95F);
    }
    boost.setMode(IDLE);
    assert(boost.getILowRef() == 0 && !boost.isLeg2Enabled());
    duty = boost.hotLoop();
    assert(duty.leg1 == 0 && duty.leg2 == 0);

    // MPPT directly perturbs current demand based on total input power.
    boost.setMode(MPPT);
    boost.setVBus(48);
    boost.setILow1(2);
    boost.setILow2(1);
    near(boost.coldLoop(), 0.1F);
    boost.setILow2(2); // Rising total power: continue increasing current.
    near(boost.coldLoop(), 0.2F);
    boost.setILow2(1); // Falling total power: reverse current perturbation.
    near(boost.coldLoop(), 0.1F);
    near(boost.getILowRef(), boost.getILow1Ref() + boost.getILow2Ref());

    // Reference changes restart MPPT comparisons and preserve the leg 1 priority.
    boost.setILowRef(4.95F);
    near(boost.coldLoop(), 5.05F);
    near(boost.getILow1Ref(), 5);
    near(boost.getILow2Ref(), 0.05F);
    assert(boost.isLeg2Enabled());
    boost.setILowRef(11.95F);
    near(boost.coldLoop(), 12);
    near(boost.coldLoop(), 11.9F); // Search turns back from the upper limit.
    boost.setILowRef(0);
    near(boost.coldLoop(), 0.1F);
    boost.setILow2(0.5F); // Power decrease directs the search down to zero.
    near(boost.coldLoop(), 0);
    assert(!boost.isLeg1Enabled() && !boost.isLeg2Enabled());
    near(boost.coldLoop(), 0.1F); // Turn back from the lower limit.
    boost.setILow2(1);

    // MPPT ignores both bus voltages and all voltage PID gains. The hot loop
    // regulates only current and does not advance the MPPT reference.
    BoostConverter comparison;
    PidParams otherVoltage = voltage;
    otherVoltage.Kp = 10;
    otherVoltage.Ti = 0.01F;
    assert(comparison.init(MPPT, 100, otherVoltage, current, 5) == 0);
    boost.setMode(MPPT);
    boost.setVBus(0);
    boost.setVBusRef(1000);
    comparison.setVBusRef(1);
    comparison.setVLow(boost.getVLow());
    comparison.setILow1(boost.getILow1());
    comparison.setILow2(boost.getILow2());
    boost.setILowRef(8);
    comparison.setILowRef(8);
    near(boost.coldLoop(), comparison.coldLoop());
    auto comparisonDuty = comparison.hotLoop();
    duty = boost.hotLoop();
    near(duty.leg1, comparisonDuty.leg1);
    near(duty.leg2, comparisonDuty.leg2);
    near(boost.getILowRef(), 8.1F);
    for (int i = 0; i < 10; ++i) boost.hotLoop();
    near(boost.getILowRef(), 8.1F);

    // Positive and fixed current bounds also constrain MPPT directly.
    otherVoltage.lower_bound = 3;
    assert(comparison.init(MPPT, 48, otherVoltage, current, 5) == 0);
    near(comparison.coldLoop(), 3.1F);
    otherVoltage.upper_bound = 3;
    assert(comparison.init(MPPT, 48, otherVoltage, current, 5) == 0);
    near(comparison.coldLoop(), 3);
    near(comparison.coldLoop(), 3);
    comparison.setMode(IDLE);
    comparison.Mppt();
    near(comparison.getILowRef(), 0);

    // Startup follows a voltage reference ramp, seeded from the latest sample.
    assert(boost.init(STARTUP, 48, voltage, current, 5) == 0);
    near(boost.getStartupRampRate(), 10);
    boost.setStartupRampRate(1000); // With Ts = 1 ms, advance 1 V per call.
    boost.setVBus(24); // Updated after init; first ramp step must start here.
    boost.setVBusRef(26.5F);
    near(boost.coldLoop(), 0.1F * (1 + voltage.Ts));
    near(boost.getStartupVBusRef(), 25);
    boost.setVBus(24.5F); // Feedback changes must not reseed the ramp.
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 26);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 26.5F);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 26.5F);
    assert(boost.getMode() == STARTUP);
    near(boost.getVBusRef(), 26.5F); // Target is distinct from ramp state.
    near(boost.getILowRef(), boost.getILow1Ref() + boost.getILow2Ref());

    // Changing the target and rate continues smoothly without a reset.
    boost.setVBusRef(30);
    boost.setStartupRampRate(500);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 27);
    boost.setVBusRef(26.75F);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 26.75F); // No downward overshoot either.
    for (float rate : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        boost.setStartupRampRate(rate);
        near(boost.getStartupRampRate(), 500);
    }

    // Reset and re-entry restart from measured voltage, preserving target/rate.
    boost.setVBus(20);
    boost.reset();
    near(boost.getStartupVBusRef(), 20);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 20.5F);
    boost.setMode(IDLE);
    boost.setVBus(21);
    boost.setMode(STARTUP);
    boost.coldLoop();
    near(boost.getStartupVBusRef(), 21.5F);
    near(boost.getStartupRampRate(), 500);
    boost.setMode(BUSREG);
    boost.setVBusRef(1000);
    near(boost.coldLoop(), 12); // BUSREG still applies the target directly.
    near(boost.getStartupVBusRef(), 21); // No ramp advancement outside STARTUP.

    // STARTUP must never enable converter 2, even for a saturated voltage demand.
    boost.setMode(STARTUP);
    near(boost.getStartupCurrentMax(), 5);
    boost.setStartupCurrentMax(2);
    near(boost.getStartupCurrentMax(), 2);
    boost.setILowRef(12);
    near(boost.getILowRef(), 2);
    near(boost.getILow1Ref(), 2);
    assert(boost.getILow2Ref() == 0 && !boost.isLeg2Enabled());
    boost.setStartupRampRate(1000000);
    for (int i = 0; i < 100; ++i) {
        assert(boost.coldLoop() <= 2);
        assert(boost.getILow1Ref() <= 2);
        assert(boost.getILow2Ref() == 0);
        assert(boost.hotLoop().leg2 == 0);
    }
    boost.setStartupCurrentMax(1); // Tighten the stored reference immediately.
    near(boost.getILowRef(), 1);
    near(boost.getILow1Ref(), 1);
    boost.reset();
    near(boost.getStartupCurrentMax(), 1);
    boost.setStartupCurrentMax(0);
    assert(boost.coldLoop() == 0);
    duty = boost.hotLoop();
    assert(duty.leg1 == 0 && duty.leg2 == 0);
    assert(!boost.isLeg1Enabled() && !boost.isLeg2Enabled());
    for (float limit : {-1.0F, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
        boost.setStartupCurrentMax(limit);
        near(boost.getStartupCurrentMax(), 0);
    }
    boost.setStartupCurrentMax(100);
    near(boost.getStartupCurrentMax(), 5); // Never exceed converter 1's limit.
    boost.setStartupCurrentMax(1);
    boost.setMode(BUSREG);
    near(boost.coldLoop(), 12); // Restore ordinary PID bounds outside STARTUP.
    near(boost.getILow2Ref(), 7);
    boost.setMode(STARTUP);
    near(boost.getStartupCurrentMax(), 1);
    assert(boost.coldLoop() <= 1 && !boost.isLeg2Enabled());

    // A startup ceiling below the ordinary lower bound must still be respected.
    voltage.lower_bound = 3;
    assert(boost.init(STARTUP, 48, voltage, current, 5) == 0);
    boost.setStartupCurrentMax(1);
    near(boost.coldLoop(), 1);
    boost.setILowRef(0);
    near(boost.getILowRef(), 1);
    boost.setStartupCurrentMax(0);
    near(boost.coldLoop(), 0);
    boost.setMode(BUSREG);
    boost.setVBusRef(1000);
    near(boost.coldLoop(), 12);
    voltage.lower_bound = 0;

    // Burst hysteresis pauses switching in STARTUP and BUSREG, preserving mode.
    for (boost_mode mode : {STARTUP, BUSREG}) {
        BoostConverter burst;
        assert(!burst.isBurstEnabled() && !burst.isBurstPaused());
        assert(burst.init(mode, 48, voltage, current, 5) == 0);
        assert(burst.setVBusBurstBounds(50, 52) == 0);
        assert(burst.isBurstEnabled());
        near(burst.getVBusBurstMin(), 50);
        near(burst.getVBusBurstMax(), 52);
        burst.setVBusRef(60);
        burst.setStartupRampRate(1000);
        assert(burst.coldLoop() > 0);
        assert(burst.hotLoop().leg1 > 0);
        assert(burst.isLeg1Enabled());
        assert(mode == BUSREG || !burst.isLeg2Enabled());

        // No cold-loop call is needed to detect the upper boundary.
        const float ramp = burst.getStartupVBusRef();
        burst.setVBus(52);
        auto pausedDuty = burst.hotLoop();
        assert(burst.isBurstPaused() && burst.getMode() == mode);
        assert(pausedDuty.leg1 == 0 && pausedDuty.leg2 == 0);
        assert(!burst.isLeg1Enabled() && !burst.isLeg2Enabled());
        near(burst.getILowRef(), 0);
        near(burst.getILow1Ref(), 0);
        near(burst.getILow2Ref(), 0);
        burst.setILowRef(12); // Manual references cannot bypass a burst pause.
        assert(burst.getILowRef() == 0);
        for (float bus : {51.9F, 50.1F, 51.0F}) {
            burst.setVBus(bus);
            assert(burst.coldLoop() == 0);
            assert(burst.hotLoop().leg1 == 0);
            assert(burst.isBurstPaused());
            near(burst.getStartupVBusRef(), ramp); // Ramp frozen during pause.
        }

        // Release at the lower boundary; demand must be regenerated by coldLoop.
        burst.setVBus(50);
        pausedDuty = burst.hotLoop();
        assert(!burst.isBurstPaused());
        assert(pausedDuty.leg1 == 0 && pausedDuty.leg2 == 0);
        burst.coldLoop();
        burst.coldLoop();
        assert(burst.isLeg1Enabled());
        assert(mode == BUSREG || !burst.isLeg2Enabled());
        burst.setVBus(51); // Stay active inside the band after release.
        burst.hotLoop();
        assert(!burst.isBurstPaused());

        // The cold loop also detects overvoltage, and reset cannot bypass a pause.
        burst.setVBus(53);
        assert(burst.coldLoop() == 0);
        assert(burst.isBurstPaused());
        burst.setVBus(51);
        burst.reset();
        assert(burst.isBurstPaused());
        burst.setMode(mode == STARTUP ? BUSREG : STARTUP);
        assert(burst.isBurstPaused());
        assert(burst.hotLoop().leg1 == 0);

        // Bounds persist across init; a high initial bus starts paused.
        assert(burst.init(mode, 53, voltage, current, 5) == 0);
        assert(burst.isBurstEnabled() && burst.isBurstPaused());
        assert(burst.coldLoop() == 0);
        burst.setMode(MPPT);
        assert(!burst.isBurstPaused());
        burst.setVLow(24);
        burst.setILowRef(8);
        assert(burst.coldLoop() > 8); // MPPT ignores these voltage boundaries.
        assert(burst.isLeg2Enabled() && burst.hotLoop().leg2 > 0);
        burst.setMode(IDLE);
        assert(!burst.isBurstPaused() && !burst.isLeg1Enabled());

        // Invalid updates preserve configured boundaries.
        assert(burst.setVBusBurstBounds(52, 50) < 0);
        assert(burst.setVBusBurstBounds(50, 50) < 0);
        assert(burst.setVBusBurstBounds(-1, 52) < 0);
        assert(burst.setVBusBurstBounds(50, std::numeric_limits<float>::infinity()) < 0);
        assert(burst.setVBusBurstBounds(std::numeric_limits<float>::quiet_NaN(), 52) < 0);
        near(burst.getVBusBurstMin(), 50);
        near(burst.getVBusBurstMax(), 52);
    }

    // Configuring boundaries while active stops immediately, with no retained PI windup.
    BoostConverter burst;
    assert(burst.init(BUSREG, 53, voltage, current, 5) == 0);
    burst.setVBusRef(1000);
    for (int i = 0; i < 100; ++i) {
        burst.coldLoop();
        burst.hotLoop();
    }
    assert(burst.setVBusBurstBounds(50, 52) == 0);
    assert(burst.isBurstPaused() && burst.getDutyLeg1() == 0 && burst.getDutyLeg2() == 0);
    burst.setVBusRef(50);
    burst.setVBus(50);
    near(burst.coldLoop(), 0); // Zero voltage error after reset produces zero demand.
    assert(!burst.isBurstPaused() && !burst.isLeg1Enabled());

    // Invalid input/configuration cannot enable either leg.
    boost.setILow2(std::numeric_limits<float>::quiet_NaN());
    near(boost.getILow2(), 1);
    for (float threshold : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
        assert(boost.init(BUSREG, 48, voltage, current, threshold) < 0);
        assert(!boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        duty = boost.hotLoop();
        assert(duty.leg1 == 0 && duty.leg2 == 0);
    }
    current.upper_bound = 1.1F;
    assert(boost.init(BUSREG, 48, voltage, current, 5) < 0);
}
