/**
 * @file ptp_servo.c
 * @brief PI servo controller implementation
 */

#include <stdint.h>
#include "ptp_servo.h"

void pi_servo_init(pi_servo_t *s)
{
    s->kp = SERVO_KP;
    s->ki = SERVO_KI;
    s->integral = 0.0;
    s->state = SERVO_UNLOCKED;
    s->count = 0;
}

double pi_servo_sample(pi_servo_t *s, int64_t offset, servo_state_t *state)
{
    double freq_adj;
    uint64_t mag;

    /* |offset| in unsigned arithmetic. llabs(INT64_MIN) is undefined
     * behaviour, and INT64_MIN is exactly what a bogus 48-bit seconds field
     * used to produce; -(offset + 1) cannot overflow for any int64_t. */
    mag = (offset < 0) ? (uint64_t)(-(offset + 1)) + 1U : (uint64_t)offset;

    if (mag > (uint64_t)SERVO_STEP_THRESHOLD) {
        /* Large offset: ask the caller for one phase step, then go back to
         * UNLOCKED with count = 0 and integral = 0 so that the loop has to prove
         * itself again before it may steer the frequency. linuxptp does the same
         * on a jump: *state = SERVO_UNLOCKED, count = 0 (pi.c:133-138).
         *
         * The jump reaches the caller through *state exactly once; the stored
         * state does not keep it. The old code left the stored state at
         * SERVO_JUMP, so the next small sample found neither "count > 1" nor
         * "count > 10" true and stepped the clock a second time. */
        s->state = SERVO_UNLOCKED;
        s->integral = 0.0;
        s->count = 0;
        *state = SERVO_JUMP;
        return 0.0;   /* a step invalidates any frequency estimate */
    }

    /* Phase is close enough to steer. Saturate the counter at the promotion
     * point instead of letting it grow without bound. */
    if (s->count < SERVO_STABLE_SAMPLES) {
        s->count++;
    }
    if (s->count >= SERVO_STABLE_SAMPLES) {
        s->state = SERVO_LOCKED_STABLE;
    } else if (s->count >= SERVO_LOCKED_SAMPLES) {
        s->state = SERVO_LOCKED;
    } else {
        /* First in-range sample: nothing to steer with yet. */
        s->state = SERVO_UNLOCKED;
    }

    /* The PI term only runs in the locked states; UNLOCKED returns 0 and the
     * caller then leaves the clock trim alone. Sign convention: offset is
     * (local time - master time), so a positive offset means "we are ahead"
     * and asks for a negative frequency correction. */
    if (s->state == SERVO_UNLOCKED) {
        freq_adj = 0.0;
    } else {
        freq_adj = -s->kp * (double)offset - s->ki * s->integral;

        if (freq_adj > SERVO_MAX_FREQ_PPB) {
            freq_adj = SERVO_MAX_FREQ_PPB;
        } else if (freq_adj < -SERVO_MAX_FREQ_PPB) {
            freq_adj = -SERVO_MAX_FREQ_PPB;
        } else {
            /* Anti-windup: integrate only while the output is not saturated.
             * Otherwise a persistent offset would charge the integral term up
             * to a value the kernel refuses with EINVAL while the loop kept
             * asking for more (linuxptp pi.c:142-148). */
            s->integral += (double)offset;
            if (s->integral > SERVO_MAX_INTEGRAL_NS) {
                s->integral = SERVO_MAX_INTEGRAL_NS;
            } else if (s->integral < -SERVO_MAX_INTEGRAL_NS) {
                s->integral = -SERVO_MAX_INTEGRAL_NS;
            }
        }
    }

    *state = s->state;
    return freq_adj;
}
