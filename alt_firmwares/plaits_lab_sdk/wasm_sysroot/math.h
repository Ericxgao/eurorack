#pragma once
// The firmware is bare metal and links no libm; these are the only names the
// plaits and stmlib headers reference. Declarations, not implementations --
// anything actually calling them would fail to link, which is the correct
// outcome, because it would not link on the hardware either.
#define M_PI    3.14159265358979323846
#define M_PI_2  1.57079632679489661923
#define M_TWOPI 6.28318530717958647692
#define M_E     2.71828182845904523536
extern "C" float sqrtf(float);
extern "C" float fabsf(float);
extern "C" double sqrt(double);
extern "C" double fabs(double);
