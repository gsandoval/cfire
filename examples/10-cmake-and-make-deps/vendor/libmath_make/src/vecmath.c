#include "vecmath.h"

#ifndef MATH_SCALE_BIAS
#define MATH_SCALE_BIAS 1.0
#endif

double vec3_scaled_dot(double ax, double ay, double az, double bx, double by, double bz, double scale) {
    double dot = (ax * bx) + (ay * by) + (az * bz);
    return dot * scale * (double)MATH_SCALE_BIAS;
}
