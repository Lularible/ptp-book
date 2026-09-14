/**
 * @file ptp_servo.h
 * @brief PI servo controller
 */

#ifndef PTP_SERVO_H
#define PTP_SERVO_H

#include <stdint.h>

/* PI controller parameters */
#define SERVO_KP 0.7
#define SERVO_KI 0.3

/* Step threshold (nanoseconds) - 10 ms */
#define SERVO_STEP_THRESHOLD 10000000LL

/* Servo states */
typedef enum {
    SERVO_UNLOCKED,
    SERVO_JUMP,
    SERVO_LOCKED,
    SERVO_LOCKED_STABLE
} servo_state_t;

/* PI servo structure */
typedef struct {
    double kp;
    double ki;
    double integral;
    double last_freq;
    servo_state_t state;
    int count;
} pi_servo_t;

/* Function declarations */
void pi_servo_init(pi_servo_t *s);
double pi_servo_sample(pi_servo_t *s, int64_t offset, servo_state_t *state);

#endif /* PTP_SERVO_H */