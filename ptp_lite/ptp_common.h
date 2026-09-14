/**
 * @file ptp_common.h
 * @brief PTP common definitions and types
 *
 * Lightweight PTP implementation - educational edition
 * Supports E2E delay measurement, UDP/IPv4 transport, software timestamps
 *
 * @version 1.0.0
 * @date 2026-04-10
 */

#ifndef PTP_COMMON_H
#define PTP_COMMON_H

#include <stdint.h>
#include <time.h>
#include <endian.h>

/* Version information */
#define PTP_LITE_VERSION "1.0.0"
#define PTP_LITE_VERSION_DATE "2026-04-10"

/* PTP timestamp: 48-bit seconds + 32-bit nanoseconds */
typedef struct {
    uint16_t seconds_msb;    /* high 16 bits of seconds */
    uint32_t seconds_lsb;    /* low 32 bits of seconds */
    uint32_t nanoseconds;    /* nanoseconds part */
} __attribute__((packed)) ptp_timestamp_t;

/* Time interval: 64-bit nanoseconds */
typedef int64_t ptp_timeinterval_t;

/* Clock identity: 8 bytes */
typedef uint8_t ptp_clock_identity_t[8];

/* Port identity */
typedef struct {
    ptp_clock_identity_t clock_identity;
    uint16_t port_number;
} __attribute__((packed)) ptp_port_identity_t;

/* Message types */
#define PTP_MSG_SYNC           0x0
#define PTP_MSG_DELAY_REQ      0x1
#define PTP_MSG_FOLLOW_UP      0x8
#define PTP_MSG_DELAY_RESP     0x9
#define PTP_MSG_ANNOUNCE       0xB

/* PTP version */
#define PTP_VERSION            2

/* Multicast address and ports */
#define PTP_PRIMARY_MCAST      "224.0.1.129"
#define PTP_EVENT_PORT         319
#define PTP_GENERAL_PORT       320

/* Default parameters */
#define PTP_DEFAULT_DOMAIN     0
#define PTP_DEFAULT_PRIORITY1  128
#define PTP_DEFAULT_PRIORITY2  128
#define PTP_DEFAULT_ANNOUNCE_INT  1
#define PTP_DEFAULT_SYNC_INT      0

/* Helper: convert a timespec to a PTP timestamp */
static inline void timespec_to_ptp(const struct timespec *ts,
                                   ptp_timestamp_t *ptp)
{
    uint64_t sec = (uint64_t)ts->tv_sec;
    ptp->seconds_msb = htobe16((uint16_t)(sec >> 32));
    ptp->seconds_lsb = htobe32((uint32_t)(sec & 0xFFFFFFFFULL));
    ptp->nanoseconds = htobe32((uint32_t)ts->tv_nsec);
}

/* Helper: convert a PTP timestamp to a timespec */
static inline void ptp_to_timespec(const ptp_timestamp_t *ptp,
                                   struct timespec *ts)
{
    ts->tv_sec = ((uint64_t)be16toh(ptp->seconds_msb) << 32) |
                 be32toh(ptp->seconds_lsb);
    ts->tv_nsec = be32toh(ptp->nanoseconds);
}

#endif /* PTP_COMMON_H */