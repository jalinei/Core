// L0-BST: boost converter controller (src/boost_converter), open loop.
// Test cases: docs/testing/test-procedure.md §6.1.
//
// The sections share one BoostConverter and run in order: each section starts
// from the state the previous one left. Do not reorder them, and keep every
// call that has side effects (hotLoop, coldLoop, set*) exactly where it is.

#include "boost.h"
#include "check.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace {
constexpr double tol = 1e-5;
}

int main() {
    PidParams voltage = {0.001F, 0.1F, 1.0F, 0, 1, 0, 12};
    PidParams current = {0.0001F, 0.1F, 1.0F, 0, 1, 0, 0.95F};
    BoostConverter boost;
    BoostDutyCycles duty;

    {
        tc::Criterion c("L0-BST-01.a", "Uninitialized controller outputs nothing and enables no leg");
        c.check("coldLoop() == 0", boost.coldLoop() == 0);
        c.check("hotLoop() duties == 0", boost.hotLoop().leg1 == 0 && boost.hotLoop().leg2 == 0);
        c.check("no leg enabled", !boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        c.check("init(BUSREG) succeeds", boost.init(BUSREG, 48, voltage, current, 5) == 0);
        c.check("leg 1 current limit stored", boost.getLeg1CurrentLimit() == 5);
    }

    {
        tc::Criterion c("L0-BST-02.a", "Leg 1 alone below and at its current limit");
        for (float total : {2.0F, 5.0F}) {
            boost.setILowRef(total);
            c.near("ILow1 ref", boost.getILow1Ref(), total, tol);
            c.check("ILow2 ref == 0", boost.getILow2Ref() == 0);
            c.check("leg 1 on, leg 2 off", boost.isLeg1Enabled() && !boost.isLeg2Enabled());
            auto d = boost.hotLoop();
            c.check("leg 1 duty > 0, leg 2 duty == 0", d.leg1 > 0 && d.leg2 == 0);
        }
    }

    {
        tc::Criterion c("L0-BST-02.b", "Above the limit leg 1 stays at the limit and leg 2 supplies the excess");
        boost.reset();
        boost.setILowRef(8);
        c.near("ILow1 ref", boost.getILow1Ref(), 5, tol);
        c.near("ILow2 ref", boost.getILow2Ref(), 3, tol);
        c.check("both legs on", boost.isLeg1Enabled() && boost.isLeg2Enabled());
        boost.setILow1(4);
        boost.setILow2(1);
        duty = boost.hotLoop();
        c.near("leg 1 PI duty", duty.leg1, 0.1F * (1 + current.Ts), tol);
        c.near("leg 2 PI duty", duty.leg2, 0.1F * (2 + 2 * current.Ts), tol);
        c.near("getDutyLeg1", boost.getDutyLeg1(), duty.leg1, tol);
        c.near("getDutyLeg2", boost.getDutyLeg2(), duty.leg2, tol);
        boost.setVLow(24);
        c.near("total input current", boost.getILowTotal(), 5, tol);
        c.near("input power", boost.getPower(), 120, tol);
    }

    {
        tc::Criterion c("L0-BST-02.c", "Removing leg 2 demand clears its output and integral before reuse");
        for (int i = 0; i < 100; ++i) boost.hotLoop();
        boost.setILowRef(5);
        c.check("leg 2 off, duty 0", !boost.isLeg2Enabled() && boost.getDutyLeg2() == 0);
        boost.setILowRef(8);
        duty = boost.hotLoop();
        c.near("leg 2 duty restarts from zero integral", duty.leg2, 0.1F * (2 + 2 * current.Ts), tol);
        boost.setILowRef(0);
        c.check("zero demand: no leg on", !boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        c.check("zero demand: duties 0", boost.getDutyLeg1() == 0 && boost.getDutyLeg2() == 0);
    }

    {
        tc::Criterion c("L0-BST-03.a", "Cold-loop saturation sets the total reference, split across both legs");
        boost.setMode(BUSREG);
        boost.setVBusRef(1000);
        c.near("saturated total ref", boost.coldLoop(), 12, tol);
        c.near("ILow1 ref", boost.getILow1Ref(), 5, tol);
        c.near("ILow2 ref", boost.getILow2Ref(), 7, tol);
        boost.setILow1(0);
        boost.setILow2(0);
        for (int i = 0; i < 10000; ++i) {
            duty = boost.hotLoop();
            c.range("leg 1 duty", duty.leg1, 0, 0.95F);
            c.range("leg 2 duty", duty.leg2, 0, 0.95F);
        }
    }

    {
        tc::Criterion c("L0-BST-03.b", "IDLE clears references and duties");
        boost.setMode(IDLE);
        c.check("ILow ref 0, leg 2 off", boost.getILowRef() == 0 && !boost.isLeg2Enabled());
        duty = boost.hotLoop();
        c.check("duties 0", duty.leg1 == 0 && duty.leg2 == 0);
    }

    {
        tc::Criterion c("L0-BST-04.a", "MPPT perturbs current demand from total input power");
        boost.setMode(MPPT);
        boost.setVBus(48);
        boost.setILow1(2);
        boost.setILow2(1);
        c.near("first step +0.1 A", boost.coldLoop(), 0.1F, tol);
        boost.setILow2(2);  // Rising total power: continue increasing current.
        c.near("power rose: continue up", boost.coldLoop(), 0.2F, tol);
        boost.setILow2(1);  // Falling total power: reverse current perturbation.
        c.near("power fell: reverse", boost.coldLoop(), 0.1F, tol);
        c.near("total = leg 1 + leg 2", boost.getILowRef(), boost.getILow1Ref() + boost.getILow2Ref(), tol);
    }

    {
        tc::Criterion c("L0-BST-04.b", "MPPT restarts on reference changes, keeps leg 1 priority, turns back at bounds");
        boost.setILowRef(4.95F);
        c.near("step from new reference", boost.coldLoop(), 5.05F, tol);
        c.near("ILow1 ref at limit", boost.getILow1Ref(), 5, tol);
        c.near("ILow2 ref takes excess", boost.getILow2Ref(), 0.05F, tol);
        c.check("leg 2 on", boost.isLeg2Enabled());
        boost.setILowRef(11.95F);
        c.near("clamped at upper bound", boost.coldLoop(), 12, tol);
        c.near("turns back from upper bound", boost.coldLoop(), 11.9F, tol);
        boost.setILowRef(0);
        c.near("step up from zero", boost.coldLoop(), 0.1F, tol);
        boost.setILow2(0.5F);  // Power decrease directs the search down to zero.
        c.near("search down to zero", boost.coldLoop(), 0, tol);
        c.check("no leg on at zero", !boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        c.near("turns back from lower bound", boost.coldLoop(), 0.1F, tol);
        boost.setILow2(1);
    }

    BoostConverter comparison;
    PidParams otherVoltage = voltage;
    {
        tc::Criterion c("L0-BST-04.c", "MPPT ignores bus voltages and voltage PID gains; hot loop does not advance it");
        otherVoltage.Kp = 10;
        otherVoltage.Ti = 0.01F;
        c.check("comparison init", comparison.init(MPPT, 100, otherVoltage, current, 5) == 0);
        boost.setMode(MPPT);
        boost.setVBus(0);
        boost.setVBusRef(1000);
        comparison.setVBusRef(1);
        comparison.setVLow(boost.getVLow());
        comparison.setILow1(boost.getILow1());
        comparison.setILow2(boost.getILow2());
        boost.setILowRef(8);
        comparison.setILowRef(8);
        c.near("same cold-loop output", boost.coldLoop(), comparison.coldLoop(), tol);
        auto comparisonDuty = comparison.hotLoop();
        duty = boost.hotLoop();
        c.near("same leg 1 duty", duty.leg1, comparisonDuty.leg1, tol);
        c.near("same leg 2 duty", duty.leg2, comparisonDuty.leg2, tol);
        c.near("reference after one step", boost.getILowRef(), 8.1F, tol);
        for (int i = 0; i < 10; ++i) boost.hotLoop();
        c.near("hot loop does not advance MPPT", boost.getILowRef(), 8.1F, tol);
    }

    {
        tc::Criterion c("L0-BST-04.d", "Voltage PID current bounds also constrain MPPT");
        otherVoltage.lower_bound = 3;
        c.check("init with lower bound 3", comparison.init(MPPT, 48, otherVoltage, current, 5) == 0);
        c.near("starts above lower bound", comparison.coldLoop(), 3.1F, tol);
        otherVoltage.upper_bound = 3;
        c.check("init with bounds 3..3", comparison.init(MPPT, 48, otherVoltage, current, 5) == 0);
        c.near("pinned at 3 A", comparison.coldLoop(), 3, tol);
        c.near("still pinned at 3 A", comparison.coldLoop(), 3, tol);
        comparison.setMode(IDLE);
        comparison.Mppt();
        c.near("Mppt() in IDLE leaves zero reference", comparison.getILowRef(), 0, tol);
    }

    {
        tc::Criterion c("L0-BST-05.a", "STARTUP ramp starts from the latest bus sample and follows the rate");
        c.check("init(STARTUP)", boost.init(STARTUP, 48, voltage, current, 5) == 0);
        c.near("default ramp rate 10 V/s", boost.getStartupRampRate(), 10, tol);
        boost.setStartupRampRate(1000);  // With Ts = 1 ms, advance 1 V per call.
        boost.setVBus(24);               // Updated after init; first ramp step must start here.
        boost.setVBusRef(26.5F);
        c.near("first cold-loop output", boost.coldLoop(), 0.1F * (1 + voltage.Ts), tol);
        c.near("ramp seeded at 24 V + 1 V", boost.getStartupVBusRef(), 25, tol);
        boost.setVBus(24.5F);  // Feedback changes must not reseed the ramp.
        boost.coldLoop();
        c.near("not reseeded by feedback", boost.getStartupVBusRef(), 26, tol);
        boost.coldLoop();
        c.near("reaches target", boost.getStartupVBusRef(), 26.5F, tol);
        boost.coldLoop();
        c.near("no overshoot", boost.getStartupVBusRef(), 26.5F, tol);
        c.check("mode stays STARTUP", boost.getMode() == STARTUP);
        c.near("target distinct from ramp state", boost.getVBusRef(), 26.5F, tol);
        c.near("total = leg 1 + leg 2", boost.getILowRef(), boost.getILow1Ref() + boost.getILow2Ref(), tol);
    }

    {
        tc::Criterion c("L0-BST-05.b", "Target and rate changes continue the ramp without reset or overshoot");
        boost.setVBusRef(30);
        boost.setStartupRampRate(500);
        boost.coldLoop();
        c.near("continues at new rate", boost.getStartupVBusRef(), 27, tol);
        boost.setVBusRef(26.75F);
        boost.coldLoop();
        c.near("no downward overshoot", boost.getStartupVBusRef(), 26.75F, tol);
        for (float rate : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
            boost.setStartupRampRate(rate);
            c.near("invalid rate rejected", boost.getStartupRampRate(), 500, tol);
        }
    }

    {
        tc::Criterion c("L0-BST-05.c", "Reset and re-entry restart from the measured voltage, keeping target and rate");
        boost.setVBus(20);
        boost.reset();
        c.near("reset reseeds at 20 V", boost.getStartupVBusRef(), 20, tol);
        boost.coldLoop();
        c.near("ramp continues", boost.getStartupVBusRef(), 20.5F, tol);
        boost.setMode(IDLE);
        boost.setVBus(21);
        boost.setMode(STARTUP);
        boost.coldLoop();
        c.near("re-entry reseeds at 21 V", boost.getStartupVBusRef(), 21.5F, tol);
        c.near("rate kept", boost.getStartupRampRate(), 500, tol);
        boost.setMode(BUSREG);
        boost.setVBusRef(1000);
        c.near("BUSREG applies target directly", boost.coldLoop(), 12, tol);
        c.near("no ramp advance outside STARTUP", boost.getStartupVBusRef(), 21, tol);
    }

    {
        tc::Criterion c("L0-BST-06.a", "STARTUP never enables converter 2 and respects the current ceiling");
        boost.setMode(STARTUP);
        c.near("default ceiling = leg 1 limit", boost.getStartupCurrentMax(), 5, tol);
        boost.setStartupCurrentMax(2);
        c.near("ceiling set to 2 A", boost.getStartupCurrentMax(), 2, tol);
        boost.setILowRef(12);
        c.near("manual ref clamped", boost.getILowRef(), 2, tol);
        c.near("ILow1 ref clamped", boost.getILow1Ref(), 2, tol);
        c.check("leg 2 off", boost.getILow2Ref() == 0 && !boost.isLeg2Enabled());
        boost.setStartupRampRate(1000000);
        for (int i = 0; i < 100; ++i) {
            c.check("coldLoop <= 2 A", boost.coldLoop() <= 2);
            c.check("ILow1 ref <= 2 A", boost.getILow1Ref() <= 2);
            c.check("ILow2 ref == 0", boost.getILow2Ref() == 0);
            c.check("leg 2 duty == 0", boost.hotLoop().leg2 == 0);
        }
    }

    {
        tc::Criterion c("L0-BST-06.b", "Ceiling updates apply immediately, reject invalid values, never exceed the leg 1 limit");
        boost.setStartupCurrentMax(1);  // Tighten the stored reference immediately.
        c.near("ref tightened to 1 A", boost.getILowRef(), 1, tol);
        c.near("ILow1 ref tightened", boost.getILow1Ref(), 1, tol);
        boost.reset();
        c.near("ceiling survives reset", boost.getStartupCurrentMax(), 1, tol);
        boost.setStartupCurrentMax(0);
        c.check("zero ceiling: coldLoop 0", boost.coldLoop() == 0);
        duty = boost.hotLoop();
        c.check("zero ceiling: duties 0", duty.leg1 == 0 && duty.leg2 == 0);
        c.check("zero ceiling: no leg on", !boost.isLeg1Enabled() && !boost.isLeg2Enabled());
        for (float limit : {-1.0F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
            boost.setStartupCurrentMax(limit);
            c.near("invalid ceiling rejected", boost.getStartupCurrentMax(), 0, tol);
        }
        boost.setStartupCurrentMax(100);
        c.near("ceiling capped at leg 1 limit", boost.getStartupCurrentMax(), 5, tol);
        boost.setStartupCurrentMax(1);
        boost.setMode(BUSREG);
        c.near("BUSREG restores PID bounds", boost.coldLoop(), 12, tol);
        c.near("BUSREG uses leg 2", boost.getILow2Ref(), 7, tol);
        boost.setMode(STARTUP);
        c.near("ceiling kept across modes", boost.getStartupCurrentMax(), 1, tol);
        c.check("STARTUP again: <= 1 A, leg 2 off", boost.coldLoop() <= 1 && !boost.isLeg2Enabled());
    }

    {
        tc::Criterion c("L0-BST-06.c", "A ceiling below the PID lower bound is still respected");
        voltage.lower_bound = 3;
        c.check("init with lower bound 3", boost.init(STARTUP, 48, voltage, current, 5) == 0);
        boost.setStartupCurrentMax(1);
        c.near("clamped to 1 A", boost.coldLoop(), 1, tol);
        boost.setILowRef(0);
        c.near("manual 0 held at ceiling", boost.getILowRef(), 1, tol);
        boost.setStartupCurrentMax(0);
        c.near("zero ceiling wins", boost.coldLoop(), 0, tol);
        boost.setMode(BUSREG);
        boost.setVBusRef(1000);
        c.near("BUSREG unaffected", boost.coldLoop(), 12, tol);
        voltage.lower_bound = 0;
    }

    // Burst hysteresis pauses switching in STARTUP and BUSREG, preserving mode.
    for (boost_mode mode : {STARTUP, BUSREG}) {
        const char *suffix = mode == STARTUP ? " (STARTUP)" : " (BUSREG)";
        const char *letter = mode == STARTUP ? "-startup" : "-busreg";
        BoostConverter burst;
        {
            tc::Criterion c(std::string("L0-BST-07.a") + letter,
                            std::string("Burst pauses at the upper bound without a cold-loop call") + suffix);
            c.check("disabled before config", !burst.isBurstEnabled() && !burst.isBurstPaused());
            c.check("init", burst.init(mode, 48, voltage, current, 5) == 0);
            c.check("bounds 50/52 accepted", burst.setVBusBurstBounds(50, 52) == 0);
            c.check("burst enabled", burst.isBurstEnabled());
            c.near("lower bound", burst.getVBusBurstMin(), 50, tol);
            c.near("upper bound", burst.getVBusBurstMax(), 52, tol);
            burst.setVBusRef(60);
            burst.setStartupRampRate(1000);
            c.check("active: coldLoop > 0", burst.coldLoop() > 0);
            c.check("active: leg 1 duty > 0", burst.hotLoop().leg1 > 0);
            c.check("active: leg 1 on", burst.isLeg1Enabled());
            c.check("STARTUP keeps leg 2 off", mode == BUSREG || !burst.isLeg2Enabled());
            const float ramp = burst.getStartupVBusRef();
            burst.setVBus(52);
            auto pausedDuty = burst.hotLoop();
            c.check("paused, mode kept", burst.isBurstPaused() && burst.getMode() == mode);
            c.check("paused: duties 0", pausedDuty.leg1 == 0 && pausedDuty.leg2 == 0);
            c.check("paused: no leg on", !burst.isLeg1Enabled() && !burst.isLeg2Enabled());
            c.near("paused: total ref 0", burst.getILowRef(), 0, tol);
            c.near("paused: ILow1 ref 0", burst.getILow1Ref(), 0, tol);
            c.near("paused: ILow2 ref 0", burst.getILow2Ref(), 0, tol);
            burst.setILowRef(12);  // Manual references cannot bypass a burst pause.
            c.check("manual ref cannot bypass", burst.getILowRef() == 0);
            for (float bus : {51.9F, 50.1F, 51.0F}) {
                burst.setVBus(bus);
                c.check("inside band: coldLoop 0", burst.coldLoop() == 0);
                c.check("inside band: leg 1 duty 0", burst.hotLoop().leg1 == 0);
                c.check("inside band: still paused", burst.isBurstPaused());
                c.near("ramp frozen", burst.getStartupVBusRef(), ramp, tol);
            }
        }
        {
            tc::Criterion c(std::string("L0-BST-07.b") + letter,
                            std::string("Burst resumes at the lower bound; demand regenerated by the cold loop") + suffix);
            burst.setVBus(50);
            auto pausedDuty = burst.hotLoop();
            c.check("released at 50 V", !burst.isBurstPaused());
            c.check("no duty until cold loop", pausedDuty.leg1 == 0 && pausedDuty.leg2 == 0);
            burst.coldLoop();
            burst.coldLoop();
            c.check("leg 1 on after cold loop", burst.isLeg1Enabled());
            c.check("STARTUP keeps leg 2 off", mode == BUSREG || !burst.isLeg2Enabled());
            burst.setVBus(51);  // Stay active inside the band after release.
            burst.hotLoop();
            c.check("stays active inside band", !burst.isBurstPaused());
        }
        {
            tc::Criterion c(std::string("L0-BST-07.c") + letter,
                            std::string("Cold loop detects overvoltage; reset and mode change cannot bypass a pause") + suffix);
            burst.setVBus(53);
            c.check("cold loop: 0 at 53 V", burst.coldLoop() == 0);
            c.check("paused", burst.isBurstPaused());
            burst.setVBus(51);
            burst.reset();
            c.check("reset keeps pause", burst.isBurstPaused());
            burst.setMode(mode == STARTUP ? BUSREG : STARTUP);
            c.check("mode change keeps pause", burst.isBurstPaused());
            c.check("leg 1 duty 0", burst.hotLoop().leg1 == 0);
        }
        {
            tc::Criterion c(std::string("L0-BST-07.d") + letter,
                            std::string("Bounds persist across init; MPPT and IDLE are exempt from burst") + suffix);
            c.check("re-init at 53 V", burst.init(mode, 53, voltage, current, 5) == 0);
            c.check("starts enabled and paused", burst.isBurstEnabled() && burst.isBurstPaused());
            c.check("coldLoop 0", burst.coldLoop() == 0);
            burst.setMode(MPPT);
            c.check("MPPT not paused", !burst.isBurstPaused());
            burst.setVLow(24);
            burst.setILowRef(8);
            c.check("MPPT ignores bounds", burst.coldLoop() > 8);
            c.check("MPPT uses leg 2", burst.isLeg2Enabled() && burst.hotLoop().leg2 > 0);
            burst.setMode(IDLE);
            c.check("IDLE: not paused, leg 1 off", !burst.isBurstPaused() && !burst.isLeg1Enabled());
        }
        {
            tc::Criterion c(std::string("L0-BST-01.b") + letter,
                            std::string("Invalid burst bounds are rejected and keep the configured ones") + suffix);
            c.check("upper < lower", burst.setVBusBurstBounds(52, 50) < 0);
            c.check("upper == lower", burst.setVBusBurstBounds(50, 50) < 0);
            c.check("negative", burst.setVBusBurstBounds(-1, 52) < 0);
            c.check("infinite", burst.setVBusBurstBounds(50, std::numeric_limits<float>::infinity()) < 0);
            c.check("NaN", burst.setVBusBurstBounds(std::numeric_limits<float>::quiet_NaN(), 52) < 0);
            c.near("lower bound kept", burst.getVBusBurstMin(), 50, tol);
            c.near("upper bound kept", burst.getVBusBurstMax(), 52, tol);
        }
    }

    {
        tc::Criterion c("L0-BST-07.e", "Configuring bounds while active pauses immediately with no PI windup kept");
        BoostConverter burst;
        c.check("init(BUSREG) at 53 V", burst.init(BUSREG, 53, voltage, current, 5) == 0);
        burst.setVBusRef(1000);
        for (int i = 0; i < 100; ++i) {
            burst.coldLoop();
            burst.hotLoop();
        }
        c.check("bounds accepted", burst.setVBusBurstBounds(50, 52) == 0);
        c.check("paused, duties 0",
                burst.isBurstPaused() && burst.getDutyLeg1() == 0 && burst.getDutyLeg2() == 0);
        burst.setVBusRef(50);
        burst.setVBus(50);
        c.near("zero error gives zero demand", burst.coldLoop(), 0, tol);
        c.check("released, leg 1 off", !burst.isBurstPaused() && !burst.isLeg1Enabled());
    }

    {
        tc::Criterion c("L0-BST-01.c", "Invalid measurements and configuration cannot enable a leg");
        boost.setILow2(std::numeric_limits<float>::quiet_NaN());
        c.near("NaN measurement ignored", boost.getILow2(), 1, tol);
        for (float threshold : {0.0F, -1.0F, std::numeric_limits<float>::infinity()}) {
            c.check("invalid leg 1 limit rejected", boost.init(BUSREG, 48, voltage, current, threshold) < 0);
            c.check("no leg on", !boost.isLeg1Enabled() && !boost.isLeg2Enabled());
            duty = boost.hotLoop();
            c.check("duties 0", duty.leg1 == 0 && duty.leg2 == 0);
        }
        current.upper_bound = 1.1F;
        c.check("duty bound > 1 rejected", boost.init(BUSREG, 48, voltage, current, 5) < 0);
    }

    return tc::finish();
}
