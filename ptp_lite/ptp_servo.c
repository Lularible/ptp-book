/**
 * @file ptp_servo.c
 * @brief PI servo controller implementation
 */

#include <stdlib.h>
#include <stdint.h>
#include "ptp_servo.h"

void pi_servo_init(pi_servo_t *s)
{
    s->kp = SERVO_KP;
    s->ki = SERVO_KI;
    s->integral = 0.0;
    s->last_freq = 0.0;
    s->state = SERVO_UNLOCKED;
    s->count = 0;
}

double pi_servo_sample(pi_servo_t *s, int64_t offset, servo_state_t *state)
{
    double freq_adj;
    
    /* State transition logic */
    if (llabs(offset) > SERVO_STEP_THRESHOLD) {
        /* Large offset: jump */
        s->state = SERVO_JUMP;
        s->integral = 0;
        s->count = 0;
    } else {
        /* Small offset: gradual adjustment */
        s->count++;
        if (s->count > 1) {
            s->state = SERVO_LOCKED;
        }
        if (s->count > 10) {
            s->state = SERVO_LOCKED_STABLE;
        }
    }
    
    /* PI control - only used in LOCKED states */
    if (s->state == SERVO_LOCKED || s->state == SERVO_LOCKED_STABLE) {
        s->integral += offset;
        freq_adj = -s->kp * offset - s->ki * s->integral;
    } else {
        freq_adj = 0;
    }
    
    *state = s->state;
    s->last_freq = freq_adj;
    
    return freq_adj;
}