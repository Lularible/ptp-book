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
    hdr->flag_field = 0;
    hdr->correction_field = 0;
    
    memcpy(&hdr->source_port_identity, port_id, sizeof(ptp_port_identity_t));
    hdr->sequence_id = htobe16(seq_id);
    hdr->log_message_interval = 0x7F;
}

void ptp_init_sync(ptp_sync_msg_t *msg, uint16_t seq_id,
                   const ptp_port_identity_t *port_id)
{
    memset(msg, 0, sizeof(*msg));
    
    ptp_init_header(&msg->header, PTP_MSG_SYNC, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 0;
}

void ptp_init_follow_up(ptp_follow_up_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id,
                        const struct timespec *ts)
{
    memset(msg, 0, sizeof(*msg));
    
    ptp_init_header(&msg->header, PTP_MSG_FOLLOW_UP, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 2;
    
    timespec_to_ptp(ts, &msg->precise_origin_timestamp);
}

void ptp_init_delay_req(ptp_delay_req_msg_t *msg, uint16_t seq_id,
                        const ptp_port_identity_t *port_id)
{
    memset(msg, 0, sizeof(*msg));
    
    ptp_init_header(&msg->header, PTP_MSG_DELAY_REQ, seq_id, port_id);
    msg->header.message_length = htobe16(sizeof(*msg));
    msg->header.control_field = 1;
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
    
    timespec_to_ptp(ts, &msg->origin_timestamp);
    msg->current_utc_offset = htobe16(37);
    msg->grandmaster_priority1 = PTP_DEFAULT_PRIORITY1;
    memcpy(msg->grandmaster_identity, port_id->clock_identity, 8);
    
    /* Clock Quality - IEEE 1588-2019 7.6.3.3 */
    msg->grandmaster_clock_quality.clock_class = 248;
    msg->grandmaster_clock_quality.clock_accuracy = 0xFE;
    msg->grandmaster_clock_quality.offset_scaled_log_variance = htobe16(0xFFFF);
    
    msg->grandmaster_priority2 = PTP_DEFAULT_PRIORITY2;
    msg->steps_removed = htobe16(0);
    msg->time_source = 0xA0; /* Internal Oscillator */
}