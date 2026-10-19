/**
 * @file ptp_common.h
 * @brief PTP common definitions and types
 *
 * Lightweight PTP implementation - educational edition
 * Supports E2E delay measurement, UDP/IPv4 transport, software timestamps
 *
 * @version 1.1.0
 * @date 2026-10-16
 */

#ifndef PTP_COMMON_H
#define PTP_COMMON_H

#include <stdint.h>
#include <time.h>
#include <endian.h>

/* Version banner, printed by both programs at start-up. Keep these in step
 * with the "Versioning" note in README.md. */
#define PTP_LITE_VERSION "1.1.0"
#define PTP_LITE_VERSION_DATE "2026-10-16"

/* Nanoseconds per second (same as linuxptp clockadj.c:30). */
#define NS_PER_SEC 1000000000LL

/* Largest value accepted for a decoded 48-bit seconds field. The field itself
 * spans 8.9 million years, but ptp_lite computes in nanoseconds: with both
 * operands under this bound every widened "(int64_t)a - (int64_t)b", and every
 * product of it with NS_PER_SEC, stays inside int64_t. Anything larger is
 * malformed or hostile and is dropped. On a 32-bit time_t the bound tightens to
 * INT32_MAX: past that "(time_t)sec" is implementation defined (gcc wraps, it
 * does not saturate), and a wrap can alias a huge offset down to a small one. */
#define PTP_MAX_VALID_SECONDS (sizeof(time_t) < 8 ? 2147483647ULL : 4294967295ULL)

/* PTP timestamp: 48-bit seconds + 32-bit nanoseconds */
typedef struct {
    uint16_t seconds_msb;    /* high 16 bits of seconds */
    uint32_t seconds_lsb;    /* low 32 bits of seconds */
    uint32_t nanoseconds;    /* nanoseconds part */
} __attribute__((packed)) ptp_timestamp_t;

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

/* Default parameters. The two *_INT values are log2(seconds), which is also
 * what goes on the wire in logMessageInterval (Table 42), so one constant
 * drives both the timer and the message field. */
#define PTP_DEFAULT_DOMAIN     0
#define PTP_DEFAULT_PRIORITY1  128
#define PTP_DEFAULT_PRIORITY2  128
#define PTP_DEFAULT_ANNOUNCE_INT  1   /* 2^1 = 2 s */
#define PTP_DEFAULT_SYNC_INT      0   /* 2^0 = 1 s */

/* currentUtcOffset carried by Announce. Published by IERS Bulletin C: it has
 * been 37 s since the leap second of 2017-01-01 and moves by +-1 s whenever a
 * leap second is inserted or deleted. No leap-second table here (that would
 * need the machinery of linuxptp lstab.c) - bump this constant by hand. */
#define PTP_CURRENT_UTC_OFFSET 37

/* Helper: convert a timespec to a PTP timestamp */
static inline void timespec_to_ptp(const struct timespec *ts,
                                   ptp_timestamp_t *ptp)
{
    uint64_t sec = (uint64_t)ts->tv_sec;
    ptp->seconds_msb = htobe16((uint16_t)(sec >> 32));
    ptp->seconds_lsb = htobe32((uint32_t)(sec & 0xFFFFFFFFULL));
    ptp->nanoseconds = htobe32((uint32_t)ts->tv_nsec);
}

/* Helper: convert a PTP timestamp to a timespec. Returns 0, or -1 when the
 * value cannot be used (seconds beyond PTP_MAX_VALID_SECONDS or nanoseconds
 * >= 1e9); the caller must then drop the whole message. */
static inline int ptp_to_timespec(const ptp_timestamp_t *ptp,
                                  struct timespec *ts)
{
    uint64_t sec  = ((uint64_t)be16toh(ptp->seconds_msb) << 32) |
                    be32toh(ptp->seconds_lsb);
    uint32_t nsec = be32toh(ptp->nanoseconds);

    if (sec > PTP_MAX_VALID_SECONDS || nsec >= (uint32_t)NS_PER_SEC) {
        return -1;
    }

    ts->tv_sec  = (time_t)sec;
    ts->tv_nsec = (long)nsec;
    return 0;
}

#endif /* PTP_COMMON_H */
