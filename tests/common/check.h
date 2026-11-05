// Per-criterion checks for L0 test cases.
//
// A Criterion collects any number of observations and, when it goes out of
// scope, prints exactly one line that tests/common/make_record.py turns into a
// row of the results table:
//
//   @@<TAB>id<TAB>verdict<TAB>description<TAB>limit<TAB>measured
//
// Unlike assert(), a failing observation does not stop the test: every
// criterion is reported. The process exit code is non-zero if any criterion
// failed (return tc::finish() from main).
//
//   {
//       tc::Criterion c("L0-BST-02.a", "Leg 1 alone up to its current limit");
//       c.near("ILow1 ref at 2 A", boost.getILow1Ref(), 2.0, 1e-5);
//       c.check("leg 2 disabled", !boost.isLeg2Enabled());
//   }   // <- line printed here
//
//   tc::info("L0-GFL-02.b", "PLL lock time", 0.034, "s");
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace tc {

inline int &failures() {
    static int count = 0;
    return count;
}

inline std::string clean(std::string text) {
    for (char &ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '|') ch = ' ';
    }
    return text;
}

inline std::string num(double value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", value);
    return buf;
}

inline void emit(const std::string &id, const char *verdict, const std::string &desc,
                 const std::string &limit, const std::string &measured) {
    std::printf("@@\t%s\t%s\t%s\t%s\t%s\n", clean(id).c_str(), verdict,
                clean(desc).c_str(), clean(limit).c_str(), clean(measured).c_str());
    std::fflush(stdout);
}

class Criterion {
public:
    Criterion(std::string id, std::string desc) : _id(std::move(id)), _desc(std::move(desc)) {}
    Criterion(const Criterion &) = delete;
    Criterion &operator=(const Criterion &) = delete;
    ~Criterion() { end(); }

    /// Boolean observation. `what` names it in the record if it is the first to fail.
    bool check(const char *what, bool ok) {
        ++_observations;
        if (!ok) fail(what);
        return ok;
    }

    /// |measured - expected| <= tol. The worst error is reported.
    bool near(const char *what, double measured, double expected, double tol) {
        const double err = std::fabs(measured - expected);
        ++_observations;
        _tol = _tol < 0.0 ? tol : std::fmin(_tol, tol);
        if (!(err <= tol)) {  // also catches NaN
            fail(what, " = " + num(measured) + ", expected " + num(expected) + " ± " + num(tol));
            return false;
        }
        if (err > _worst_error) _worst_error = err;
        return true;
    }

    /// lo <= measured <= hi.
    bool range(const char *what, double measured, double lo, double hi) {
        ++_observations;
        if (!(measured >= lo && measured <= hi)) {
            fail(what, " = " + num(measured) + ", outside [" + num(lo) + ", " + num(hi) + "]");
            return false;
        }
        return true;
    }

    /// Prints the result line now instead of at end of scope.
    void end() {
        if (_done) return;
        _done = true;
        const bool ok = _failed == 0 && _observations > 0;
        std::string limit = "all observations hold";
        if (_tol >= 0.0) limit = "abs error ≤ " + num(_tol);
        std::string measured;
        if (_observations == 0) {
            measured = "no observation made";
        } else if (ok) {
            measured = std::to_string(_observations) + "/" + std::to_string(_observations) + " ok";
            if (_tol >= 0.0) measured += ", max abs error " + num(_worst_error);
        } else {
            measured = std::to_string(_failed) + "/" + std::to_string(_observations) +
                       " failed; first: " + _first_failure;
        }
        if (!ok) ++failures();
        emit(_id, ok ? "PASS" : "FAIL", _desc, limit, measured);
    }

private:
    void fail(const char *what, const std::string &detail = "") {
        if (_failed++ == 0) _first_failure = std::string(what ? what : "?") + detail;
    }

    std::string _id;
    std::string _desc;
    std::string _first_failure;
    int _observations = 0;
    int _failed = 0;
    double _worst_error = 0.0;
    double _tol = -1.0;
    bool _done = false;
};

/// A measured value with no stated limit (verdict INFO, never fails).
inline void info(const std::string &id, const std::string &desc, double value,
                 const std::string &unit) {
    emit(id, "INFO", desc, "—", num(value) + (unit.empty() ? "" : " " + unit));
}

/// A criterion that could not be evaluated.
inline void blocked(const std::string &id, const std::string &desc, const std::string &reason) {
    emit(id, "BLOCKED", desc, "—", reason);
}

inline int finish() { return failures() == 0 ? 0 : 1; }

}  // namespace tc
