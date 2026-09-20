#ifndef MR_MATH_H
#define MR_MATH_H

/* Statically recovered QuickBASIC 3 / BRUN30 single-precision math used by
 * Moraff's Revenge.  These are deliberately separate from the host libm:
 * tiny differences can cross BASIC INT boundaries and change persistent
 * dungeon topology, rewards, prices, and progression. */
float mr_qb3_sin(float radians);
float mr_qb3_pow(float base, float exponent);

#endif
