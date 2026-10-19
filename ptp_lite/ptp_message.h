/**
 * @file ptp_message.h
 * @brief PTP message structure definitions - IEEE 1588-2019
 */

#ifndef PTP_MESSAGE_H
#define PTP_MESSAGE_H

#include <stddef.h>
#include "ptp_common.h"

/* Flag bits (IEEE 1588-2019 Table 37). The field goes on the wire as two
 * octets but ptp_header_t stores it as one uint16_t that must pass through
 * htobe16(), so a bit n of octet 0 appears here as bit (8 + n). linuxptp
 * declares "Octet flagField[2]" (msg.h:96) and plain bit values (msg.h:57-66). */
#define PTP_FLAG_TWO_STEP       0x0200U  /* octet 0.bit1 - Sync is two-step */
#define PTP_FLAG_UNICAST        0x0400U  /* octet 0.bit2 - sent to a unicast address */
#define PTP_FLAG_PTP_TIMESCALE  0x0008U  /* octet 1.bit3 - PTP timescale, not ARB */
#define PTP_FLAG_UTC_OFF_VALID  0x0004U  /* octet 1.bit2 - currentUtcOffset valid */

/* logMessageInterval value meaning "unknown / not applicable": what Table 42
 * assigns to Delay_Req, Signaling and management messages without exception. */
#define PTP_LOG_INTERVAL_NA     0x7F

/* PTP message header - IEEE 1588-2019 Table 35 (34 bytes) */
typedef struct {
    uint8_t  message_type;              /* Octet 0: message type */
    uint8_t  version_ptp;               /* Octet 1: PTP version */
    uint16_t message_length;            /* Octets 2-3: message length */
    uint8_t  domain_number;             /* Octet 4: domain number */
    /* Octet 5 is minorSdoId, not a spare field: a profile may put a value here.
     * ptp_lite implements no profile, so the all-zero value memset() leaves is
     * the correct one and a receiver must ignore it. */
    uint8_t  minor_sdoid;
    uint16_t flag_field;                /* Octets 6-7: flag field, big endian */
    uint64_t correction_field;          /* Octets 8-15: correction field, big endian */
    /* Octets 16-19 are messageTypeSpecific (Table 39): reserved for general
     * messages, available to the sender for event messages. Either way it goes
     * out as zero and the receiver ignores it. */
    uint32_t message_type_specific;
    ptp_port_identity_t source_port_identity;  /* Octets 20-29: source port ID */
    uint16_t sequence_id;               /* Octets 30-31: sequence number */
    /* Octet 32 is the legacy control field of IEEE 1588-2002, filled from
     * Table 41 (Sync 0, Delay_Req 1, Follow_Up 2, Delay_Resp 3, Management 4,
     * all others 5) and ignored by receivers. 13.3.2.13 also asks an
     * implementation without the Annex C.4.2 hardware-compatibility option to
     * transmit it as all zeros; ptp_lite keeps the Table 41 values. */
    uint8_t  control_field;
    int8_t   log_message_interval;      /* Octet 33: message interval */
} __attribute__((packed)) ptp_header_t;

/* Sync message (44 bytes) - IEEE 1588-2019 Table 44 */
typedef struct {
    ptp_header_t header;                /* 34 bytes */
    ptp_timestamp_t origin_timestamp;   /* 10 bytes */
} __attribute__((packed)) ptp_sync_msg_t;

/* Follow_Up message (44 bytes) - IEEE 1588-2019 Table 45 */
typedef struct {
    ptp_header_t header;                      /* 34 bytes */
    ptp_timestamp_t precise_origin_timestamp; /* 10 bytes */
} __attribute__((packed)) ptp_follow_up_msg_t;

/* Delay_Req message (44 bytes) - IEEE 1588-2019 Table 44 */
typedef struct {
    ptp_header_t header;                /* 34 bytes */
    ptp_timestamp_t origin_timestamp;   /* 10 bytes */
} __attribute__((packed)) ptp_delay_req_msg_t;

/* Delay_Resp message (54 bytes) - IEEE 1588-2019 Table 46 */
typedef struct {
    ptp_header_t header;                /* 34 bytes */
    ptp_timestamp_t receive_timestamp;  /* 10 bytes */
    ptp_port_identity_t requesting_port_identity; /* 10 bytes */
} __attribute__((packed)) ptp_delay_resp_msg_t;

/* Clock Quality structure (4 bytes) - IEEE 1588-2019 */
typedef struct {
    uint8_t  clock_class;                    /* Octet 0: clock class */
    uint8_t  clock_accuracy;                 /* Octet 1: clock accuracy */
    uint16_t offset_scaled_log_variance;     /* Octets 2-3: offset scaled log variance */
} __attribute__((packed)) ptp_clock_quality_t;

/* Announce message (64 bytes) - IEEE 1588-2019 Table 43 */
typedef struct {
    ptp_header_t header;                     /* 34 bytes */
    ptp_timestamp_t origin_timestamp;        /* 10 bytes */
    uint16_t current_utc_offset;             /* 2 bytes */
    uint8_t  reserved;                       /* 1 byte */
    uint8_t  grandmaster_priority1;          /* 1 byte */
    ptp_clock_quality_t grandmaster_clock_quality; /* 4 bytes */
    uint8_t  grandmaster_priority2;          /* 1 byte */
    ptp_clock_identity_t grandmaster_identity; /* 8 bytes */
    uint16_t steps_removed;                  /* 2 bytes */
    uint8_t  time_source;                    /* 1 byte */
} __attribute__((packed)) ptp_announce_msg_t;

/* Compile-time checks of structure sizes */
_Static_assert(sizeof(ptp_header_t) == 34, "Header size incorrect");
_Static_assert(sizeof(ptp_sync_msg_t) == 44, "Sync size incorrect");
_Static_assert(sizeof(ptp_follow_up_msg_t) == 44, "Follow_Up size incorrect");
_Static_assert(sizeof(ptp_delay_req_msg_t) == 44, "Delay_Req size incorrect");
_Static_assert(sizeof(ptp_delay_resp_msg_t) == 54, "Delay_Resp size incorrect");
_Static_assert(sizeof(ptp_announce_msg_t) == 64, "Announce size incorrect");

/* Function declarations */
void ptp_init_header(ptp_header_t *hdr, uint8_t msg_type,
                     uint16_t seq_id, const ptp_port_identity_t *port_id);

/**
 * @brief Minimal inbound validation of a received message header.
 *
 * Checks what every conforming receiver must look at before acting on a
 * datagram: version, domain, the declared message_length against the size of
 * the structure the caller is about to cast to, and the number of bytes that
 * actually arrived. Without it, any host able to send 35 UDP bytes could drive
 * the clock of a ptp_lite slave.
 *
 * @param hdr    header at the start of the datagram; the caller must have
 *               received at least sizeof(ptp_header_t) bytes
 * @param len    bytes received
 * @param expect sizeof() the complete message the caller handles
 * @return NULL if the message may be processed, else a loggable reason string.
 */
const char *ptp_check_header(const ptp_header_t *hdr, size_t len,
                             size_t expect);

void ptp_init_sync(ptp_sync_msg_t *msg, uint16_t seq_id,
                   const ptp_port_identity_t *port_id);

void ptp_init_follow_up(ptp_follow_up_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id,
                        const struct timespec *ts);

void ptp_init_delay_req(ptp_delay_req_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id);

void ptp_init_delay_resp(ptp_delay_resp_msg_t *msg, uint16_t seq_id,
                         const ptp_port_identity_t *port_id,
                         const ptp_timestamp_t *ts,
                         const ptp_port_identity_t *req_port_id);
/* NOTE: seq_id must be the sequenceId of the Delay_Req being answered, read
 * back with be16toh() - a Delay_Resp echoes it, it does not count its own. */

void ptp_init_announce(ptp_announce_msg_t *msg, uint16_t seq_id,
                       const ptp_port_identity_t *port_id,
                       const struct timespec *ts);

#endif /* PTP_MESSAGE_H */
