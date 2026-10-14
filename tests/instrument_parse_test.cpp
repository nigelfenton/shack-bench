// instrument_parse_test — NanoVNA `data` line parsing, no instrument needed.
//
// A loose parse turned a stray two-token line into Γ = 0, which plots as a
// perfect SWR 1.0 point. Only exactly two finite numbers are a data point.

#include "Instrument.h"

#include <QString>
#include <cmath>
#include <cstdio>

using namespace TciMon;

static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

int main()
{
    std::printf("=== NanoVNA data lines ===\n");
    double re = 9.0, im = 9.0;
    check(parseComplexPair("0.123 -0.456", &re, &im)
              && std::abs(re - 0.123) < 1e-12 && std::abs(im + 0.456) < 1e-12,
          "a normal 're im' line parses");
    check(parseComplexPair("  1e-3   2.5E-1 ", &re, &im) && re == 1e-3,
          "exponents and extra spaces are fine");
    check(!parseComplexPair("ch> data", &re, &im),
          "a prompt merged with an echo is NOT a point (was Γ = 0)");
    check(!parseComplexPair("0.1", &re, &im), "one token is not a point");
    check(!parseComplexPair("0.1 0.2 0.3", &re, &im), "three tokens is not a point");
    check(!parseComplexPair("nan 0.2", &re, &im), "NaN is not a point");
    check(!parseComplexPair("", &re, &im), "an empty line is not a point");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
