/**
 * @file ptp_message.c
 * @brief PTP message encoding implementation - IEEE 1588-2019
 */

#include <string.h>
#include <arpa/inet.h>
#include "ptp_message.h"

void ptp_init_header(ptp_header_t *hdr, uint8_t msg_type,
                     uint16_t seq_id, const ptp_port_identity_t *port_id)
{
    memset(hdr, 0, sizeof(*hdr));

    hdr->message_type = msg_type;
    hdr->version_ptp = PTP_VERSION;
    hdr->domain_number = PTP_DEFAULT_DOMAIN;
    /* Both are multi-octet quantities, so they are written big-endian like
     * every other integer in the header, even while the value is 0. */
    hdr->flag_field = htobe16(0);
    hdr->correction_field = htobe64(0);

    memcpy(&hdr->source_port_identity, port_id, sizeof(ptp_port_identity_t));
    hdr->sequence_id = htobe16(seq_id);
    /* The per-message functions below override this where the interval is
     * meaningful (Table 42). */
    hdr->log_message_interval = PTP_LOG_INTERVAL_NA;
}

const char *ptp_check_header(const ptp_header_t *hdr, size_t len,
                             size_t expect)
{
    size_t msg_len;

    /* The caller checked that len covers the 34-byte header, so every field
     * read here arrived on the wire. */
    if (hdr->version_ptp != PTP_VERSION) {
        return "version_ptp is not PTPv2";
    }
    if (hdr->domain_number != PTP_DEFAULT_DOMAIN) {
        return "domain_number is not ours";
    }
    /* Exact on purpose: ptp_lite builds no TLVs, so a different length is one we
     * cannot decode - a peer sending AUTHENTICATION/NSM TLVs is rejected here. */
    msg_len = be16toh(hdr->message_length);
    if (msg_len != expect) {
        return "message_length does not match the type";
    }
    if (len < msg_len) {
        return "message truncated on the wire";
    }
    return NULL;
}

void ptp_init_sync(ptp_sync_msg_t *msg, uint16_t seq_id,
                   const ptp_port_identity_t *port_id)
{
    memset(msg, 0, sizeof(*msg));

    ptp_init_header(&msg->header, PTP_MSG_SYNC, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    /* Legacy control field, Table 41. */
    msg->header.control_field = 0;
    /* The Sync is always followed by a Follow_Up, i.e. this is two-step.
     * Without the flag a conforming slave treats the Sync as one-step, uses
     * originTimestamp (left at zero here) and throws the Follow_Up away. */
    msg->header.flag_field = htobe16(PTP_FLAG_TWO_STEP);
    msg->header.log_message_interval = PTP_DEFAULT_SYNC_INT;
}

void ptp_init_follow_up(ptp_follow_up_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id,
                        const struct timespec *ts)
{
    memset(msg, 0, sizeof(*msg));

    ptp_init_header(&msg->header, PTP_MSG_FOLLOW_UP, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 2;
    /* Carries the interval of the Sync sequence it belongs to. */
    msg->header.log_message_interval = PTP_DEFAULT_SYNC_INT;

    timespec_to_ptp(ts, &msg->precise_origin_timestamp);
}

void ptp_init_delay_req(ptp_delay_req_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id)
{
    memset(msg, 0, sizeof(*msg));

    ptp_init_header(&msg->header, PTP_MSG_DELAY_REQ, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 1;
    /* Table 42: a Delay_Req always carries 0x7F. */
    msg->header.log_message_interval = PTP_LOG_INTERVAL_NA;
}

void ptp_init_delay_resp(ptp_delay_resp_msg_t *msg, uint16_t seq_id,
                         const ptp_port_identity_t *port_id,
                         const ptp_timestamp_t *ts,
                         const ptp_port_identity_t *req_port_id)
{
    memset(msg, 0, sizeof(*msg));

    ptp_init_header(&msg->header, PTP_MSG_DELAY_RESP, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 3;
    /* Table 42: 7F fits this unicast send, which 9.5.12 does not allow - README. */
    msg->header.log_message_interval = PTP_LOG_INTERVAL_NA;

    memcpy(&msg->receive_timestamp, ts, sizeof(ptp_timestamp_t));
    memcpy(&msg->requesting_port_identity, req_port_id, sizeof(ptp_port_identity_t));
}

void ptp_init_announce(ptp_announce_msg_t *msg, uint16_t seq_id,
                       const ptp_port_identity_t *port_id,
                       const struct timespec *ts)
{
    memset(msg, 0, sizeof(*msg));

    ptp_init_header(&msg->header, PTP_MSG_ANNOUNCE, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 5;
    msg->header.log_message_interval = PTP_DEFAULT_ANNOUNCE_INT;

    timespec_to_ptp(ts, &msg->origin_timestamp);
    msg->current_utc_offset = htobe16(PTP_CURRENT_UTC_OFFSET);
    msg->grandmaster_priority1 = PTP_DEFAULT_PRIORITY1;
    memcpy(msg->grandmaster_identity, port_id->clock_identity,
           sizeof(ptp_clock_identity_t));

    /* Clock Quality - IEEE 1588-2019 7.6.3.3: all "I know nothing" values
     * (class 248 = default/no traceability, accuracy 0xFE = unknown, maximum
     * variance). Under a real BMCA this master therefore always loses to a
     * grandmaster with traceable time, which is the right outcome for an
     * oscillator-fed teaching program. */
    msg->grandmaster_clock_quality.clock_class = 248;
    msg->grandmaster_clock_quality.clock_accuracy = 0xFE;
    msg->grandmaster_clock_quality.offset_scaled_log_variance = htobe16(0xFFFF);

    msg->grandmaster_priority2 = PTP_DEFAULT_PRIORITY2;
    msg->steps_removed = htobe16(0);
    msg->time_source = 0xA0; /* Internal Oscillator */

    /* The hard-coded currentUtcOffset above is only meaningful together with
     * utcOffsetValid; ptpTimescale says the timescale is PTP and not ARB, which
     * is what makes a UTC offset apply at all (Table 37). */
    msg->header.flag_field = htobe16(PTP_FLAG_UTC_OFF_VALID |
                                     PTP_FLAG_PTP_TIMESCALE);
}
