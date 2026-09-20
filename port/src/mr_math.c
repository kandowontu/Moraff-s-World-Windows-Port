#include "mr_math.h"

#include <math.h>

float mr_qb3_sin(float radians) {
    /* BRUN30.EXE BF0C-BF67.  QB3 multiplies the absolute argument by its
     * MBF-single 1/(2*pi), removes integer cycles, folds to [-.25,.25], and
     * evaluates the five-coefficient odd polynomial at DS:06AA-06BD. */
    static const float inverse_two_pi = 0.15915493667125702f; /* DS:03CA */
    static const float coefficients[5] = {
        39.71091842651367f, -76.57498931884766f,
        81.60223388671875f, -41.341678619384766f,
        6.2831854820251465f
    };

    int negative = signbit(radians) != 0;
    float cycle = fabsf(radians);
    cycle = cycle * inverse_two_pi;
    cycle = cycle - floorf(cycle); /* AC92 truncates a positive MBF single. */
    if (cycle >= 0.25f) {
        if (cycle < 0.75f) cycle = cycle - 0.5f;
        else cycle = cycle - 1.0f;
    }

    float square = cycle * cycle;
    float polynomial = coefficients[0];
    for (int index = 1; index < 5; ++index) {
        polynomial = polynomial * square;
        polynomial = polynomial + coefficients[index];
    }
    float result = polynomial * cycle;
    return negative ? -result : result;
}

static float mr_qb3_log2_positive(float value) {
    /* BRUN30.EXE B7BA-B89D: range reduction plus its two-coefficient
     * rational LOG2 approximation. */
    static const float sqrt_half = 0.7071067690849304f; /* DS:03C2 */
    static const float ln_two = 0.6931471824645996f;    /* DS:040A */
    static const float numerator_coefficient =
        -0.5527074933052063f;                           /* DS:05D6 */
    static const float denominator_magnitude =
        6.632718086242676f;                             /* -DS:05DA */

    int exponent = 0;
    float mantissa = frexpf(value, &exponent);
    float numerator;
    float current;
    if (mantissa < sqrt_half) {
        --exponent;
        numerator = mantissa - 0.5f;
        current = numerator;
    } else {
        numerator = mantissa - 1.0f;
        current = mantissa;
    }

    float denominator = current / 2.0f;
    denominator = denominator + 0.5f;
    float z = numerator / denominator;
    float z_squared = z * z;
    float rational_denominator = z_squared - denominator_magnitude;
    float rational = numerator_coefficient / rational_denominator;
    float correction = z * z_squared;
    correction = correction * rational;
    float fraction = z + correction;
    float result = fraction / ln_two;
    result = result + (float)exponent;
    return result;
}

static float mr_qb3_exp2(float value) {
    /* BRUN30.EXE B5B5-B7B9: floor, the DS:05B2-05CD degree-six Horner
     * polynomial, then a binary exponent scale. */
    static const float coefficients[7] = {
        0.0002074557705782354f,
        0.0012710057199001312f,
        0.00965065136551857f,
        0.055496565997600555f,
        0.2402271330356598f,
        0.6931471824645996f,
        1.0f
    };

    int integer = (int)floorf(value);
    float fraction = value - (float)integer;
    float result = coefficients[0];
    for (int index = 1; index < 7; ++index) {
        result = result * fraction;
        result = result + coefficients[index];
    }
    return ldexpf(result, integer);
}

float mr_qb3_pow(float base, float exponent) {
    /* Every recovered game call has a nonnegative base and positive
     * exponent.  Preserve the runtime's useful 0^positive result without
     * feeding zero into the logarithm path. */
    if (base == 0.0f && exponent > 0.0f) return 0.0f;
    if (base <= 0.0f) return NAN;
    float logarithm = mr_qb3_log2_positive(base);
    float scaled = logarithm * exponent;
    return mr_qb3_exp2(scaled);
}
