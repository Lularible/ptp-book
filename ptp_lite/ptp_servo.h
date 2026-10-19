/**
 * @file ptp_servo.h
 * @brief PI servo controller
 */

#ifndef PTP_SERVO_H
#define PTP_SERVO_H

#include <stdint.h>

/* PI gains: the software-timestamp pair of linuxptp, SWTS_KP_SCALE = 0.1 and
 * SWTS_KI_SCALE = 0.001 (pi.c:30-31). The 0.7 / 0.3 that used to be here are
 * its *hardware* timestamp gains (pi.c:28-29) - seven and three hundred times
 * too hot for timestamps carrying tens of microseconds of jitter.
 *
 * linuxptp also rescales both gains with the sync interval and caps them by
 * norm_max (pi_sync_interval(), pi.c:157-171). ptp_lite always runs a 1 s sync
 * interval and pow(1, x) == 1, so that normalisation would be a no-op and is
 * deliberately not implemented. */
#define SERVO_KP 0.1
#define SERVO_KI 0.001

/* Step threshold (nanoseconds) - 10 ms */
#define SERVO_STEP_THRESHOLD 10000000LL

/* Contract on the caller, not a bound the servo enforces: pi_servo_sample()
 * works in double and 2^53 ns (~104 days) is where an int64_t stops converting
 * to double exactly, so a larger |offset| is not "a very large offset" but an
 * invalid measurement (a stale or hostile timestamp pair) that whoever calls
 * us must reject instead of handing it here - see ptp_slave.c. Two clocks
 * further apart than this have to be brought together with `date -s` first. */
#define SERVO_MAX_OFFSET_NS (9007199254740992LL)   /* 2^53 nanoseconds */

/* Clamp on the loop output, in ppb: +-500 ppm, the fallback limit linuxptp
 * uses when the kernel reports no adjustable range (clockadj.c:139-140).
 * Uncapped, one sample at the step threshold asks Kp * 1e7 = 1e6 ppb = 1000 ppm,
 * twice the clamp: the kernel answers EINVAL while the integral keeps charging. */
#define SERVO_MAX_FREQ_PPB 500000.0

/* Clamp on the integral accumulator, in nanoseconds of summed offset, derived
 * so that the integral term alone never exceeds the full frequency range:
 * ki * SERVO_MAX_INTEGRAL_NS == SERVO_MAX_FREQ_PPB. */
#define SERVO_MAX_INTEGRAL_NS (SERVO_MAX_FREQ_PPB / SERVO_KI)

/* Consecutive in-range samples needed to count as locked, and as stably locked
 * (the old code tested count > 1 and count > 10). */
#define SERVO_LOCKED_SAMPLES 2
#define SERVO_STABLE_SAMPLES 11

/* Servo states */
typedef enum {
    SERVO_UNLOCKED,
    SERVO_JUMP,
    SERVO_LOCKED,
    SERVO_LOCKED_STABLE
} servo_state_t;

/* PI servo structure. There is no last_freq member on purpose: linuxptp keeps
 * one (pi.c:71, pi.c:192-193) because its two-step frequency estimator starts
 * from the previous estimate. ptp_lite writes the trim with
 * clock_adjtime(ADJ_FREQUENCY), an absolute setting that stays until written
 * again, so a sample with nothing to say simply writes nothing. */
typedef struct {
    double kp;
    double ki;
    double integral;
    servo_state_t state;
    int count;   /* saturates at SERVO_STABLE_SAMPLES, so it cannot overflow */
} pi_servo_t;

/* Function declarations */
void pi_servo_init(pi_servo_t *s);
double pi_servo_sample(pi_servo_t *s, int64_t offset, servo_state_t *state);

#endif /* PTP_SERVO_H */
