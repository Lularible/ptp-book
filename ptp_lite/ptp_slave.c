/**
 * @file ptp_slave.c
 * @brief PTP slave clock program - dual-socket implementation (IEEE 1588 compliant)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/timex.h>
#include <sys/syscall.h>
#include "ptp_message.h"
#include "ptp_servo.h"

#ifndef clock_adjtime
static inline int clock_adjtime(clockid_t id, struct timex *tx)
{
    return syscall(__NR_clock_adjtime, id, tx);
}
#endif

static ptp_port_identity_t slave_port_id;
static pi_servo_t servo;
static struct timespec t1, t2, t3, t4;
static uint16_t last_sync_seq = 0;
static uint16_t delay_req_seq = 0;

static int create_socket(const char *iface, int port)
{
    int fd;
    struct sockaddr_in addr;
    struct ip_mreqn mreq;
    int opt = 1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface)) < 0) {
        perror("SO_BINDTODEVICE");
    }

    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, PTP_PRIMARY_MCAST, &mreq.imr_multiaddr);
    mreq.imr_ifindex = if_nametoindex(iface);

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP");
    }

    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));

    return fd;
}

static int send_multicast(int fd, const void *data, size_t len, int port)
{
    struct sockaddr_in addr;
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, PTP_PRIMARY_MCAST, &addr.sin_addr);
    addr.sin_port = htons(port);
    
    return sendto(fd, data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
}

static void init_port_id(void)
{
    memset(&slave_port_id, 0, sizeof(slave_port_id));
    slave_port_id.clock_identity[0] = 0x00;
    slave_port_id.clock_identity[1] = 0x22;
    slave_port_id.clock_identity[2] = 0x33;
    slave_port_id.clock_identity[3] = 0x44;
    slave_port_id.clock_identity[4] = 0x55;
    slave_port_id.clock_identity[5] = 0x66;
    slave_port_id.clock_identity[6] = 0x77;
    slave_port_id.clock_identity[7] = 0x88;
    slave_port_id.port_number = htobe16(1);
}

static void adjust_clock(int64_t offset_ns, double freq_ppb, servo_state_t state)
{
    struct timex tx;
    int ret;
    struct timespec ts;
    
    switch (state) {
    case SERVO_JUMP:
        /* Use clock_settime to set the time directly (more reliable) */
        clock_gettime(CLOCK_REALTIME, &ts);
        
        if (offset_ns < 0) {
            /* Slave is behind; move the clock forward */
            ts.tv_sec += (-offset_ns) / 1000000000LL;
            ts.tv_nsec += (-offset_ns) % 1000000000LL;
            if (ts.tv_nsec >= 1000000000LL) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000LL;
            }
        } else {
            /* Slave is ahead; move the clock backward */
            ts.tv_sec -= offset_ns / 1000000000LL;
            ts.tv_nsec -= offset_ns % 1000000000LL;
            if (ts.tv_nsec < 0) {
                ts.tv_sec--;
                ts.tv_nsec += 1000000000LL;
            }
        }
        
        ret = clock_settime(CLOCK_REALTIME, &ts);
        if (ret < 0) {
            perror("clock_settime failed");
            printf("Failed to adjust clock by %ld ns\n", offset_ns);
        } else {
            printf("CLOCK JUMP: %ld ns (new time: %ld.%09ld)\n", 
                   offset_ns, ts.tv_sec, ts.tv_nsec);
        }
        break;
        
    case SERVO_LOCKED:
    case SERVO_LOCKED_STABLE:
        memset(&tx, 0, sizeof(tx));
        tx.modes = ADJ_FREQUENCY;
        tx.freq = (long)(freq_ppb * 65.536);
        ret = clock_adjtime(CLOCK_REALTIME, &tx);
        if (ret < 0) {
            perror("clock_adjtime FREQ failed");
        } else {
            printf("FREQ ADJ: %.2f ppb\n", freq_ppb);
        }
        break;
        
    default:
        break;
    }
}

static void handle_sync(ptp_sync_msg_t *msg)
{
    clock_gettime(CLOCK_REALTIME, &t2);
    last_sync_seq = be16toh(msg->header.sequence_id);
    printf("Received Sync seq=%u\n", last_sync_seq);
}

static void handle_follow_up(ptp_follow_up_msg_t *msg)
{
    ptp_to_timespec(&msg->precise_origin_timestamp, &t1);
    
    printf("Follow_Up seq=%u: t1=%ld.%09ld t2=%ld.%09ld\n",
           be16toh(msg->header.sequence_id), t1.tv_sec, t1.tv_nsec, 
           t2.tv_sec, t2.tv_nsec);
}

static void send_delay_req(int event_fd)
{
    ptp_delay_req_msg_t msg;
    
    ptp_init_delay_req(&msg, delay_req_seq++, &slave_port_id);
    clock_gettime(CLOCK_REALTIME, &t3);
    send_multicast(event_fd, &msg, sizeof(msg), PTP_EVENT_PORT);
    
    printf("Sent Delay_Req seq=%u at %ld.%09ld\n",
           be16toh(msg.header.sequence_id), t3.tv_sec, t3.tv_nsec);
}

static void handle_delay_resp(ptp_delay_resp_msg_t *msg)
{
    servo_state_t state;
    int64_t delay, offset;
    double freq;
    
    ptp_to_timespec(&msg->receive_timestamp, &t4);
    
    delay = ((t2.tv_sec - t1.tv_sec) * 1000000000LL + (t2.tv_nsec - t1.tv_nsec) +
             (t4.tv_sec - t3.tv_sec) * 1000000000LL + (t4.tv_nsec - t3.tv_nsec)) / 2;
    
    offset = (t2.tv_sec - t1.tv_sec) * 1000000000LL + 
             (t2.tv_nsec - t1.tv_nsec) - delay;
    
    printf("Delay_Resp: t3=%ld.%09ld t4=%ld.%09ld delay=%ld ns offset=%ld ns\n",
           t3.tv_sec, t3.tv_nsec, t4.tv_sec, t4.tv_nsec, delay, offset);
    
    /* Use the precise offset to adjust the clock */
    freq = pi_servo_sample(&servo, offset, &state);
    adjust_clock(offset, freq, state);
}

int main(int argc, char *argv[])
{
    int event_fd, general_fd;
    uint8_t recv_buf[1024];
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    ssize_t ret;
    struct timespec next_delay_req, now;
    int max_fd;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }
    
    printf("Starting PTP Slave on interface %s\n", argv[1]);
    
    init_port_id();
    pi_servo_init(&servo);
    
    /* Create two sockets: one listening on 319, one on 320 */
    event_fd = create_socket(argv[1], PTP_EVENT_PORT);
    if (event_fd < 0) {
        fprintf(stderr, "Failed to create event socket\n");
        return 1;
    }
    
    general_fd = create_socket(argv[1], PTP_GENERAL_PORT);
    if (general_fd < 0) {
        fprintf(stderr, "Failed to create general socket\n");
        close(event_fd);
        return 1;
    }
    
    printf("Slave Clock ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
           slave_port_id.clock_identity[0], slave_port_id.clock_identity[1],
           slave_port_id.clock_identity[2], slave_port_id.clock_identity[3],
           slave_port_id.clock_identity[4], slave_port_id.clock_identity[5],
           slave_port_id.clock_identity[6], slave_port_id.clock_identity[7]);
    
    printf("Listening on port %d (event) and %d (general)\n", 
           PTP_EVENT_PORT, PTP_GENERAL_PORT);
    
    max_fd = (event_fd > general_fd) ? event_fd : general_fd;
    
    clock_gettime(CLOCK_MONOTONIC, &next_delay_req);
    next_delay_req.tv_sec += 1;
    
    while (1) {
        fd_set readfds;
        struct timeval timeout;
        
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        if (now.tv_sec >= next_delay_req.tv_sec) {
            send_delay_req(event_fd);
            next_delay_req.tv_sec = now.tv_sec + 1;
        }
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        
        FD_ZERO(&readfds);
        FD_SET(event_fd, &readfds);
        FD_SET(general_fd, &readfds);
        
        ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret > 0) {
            /* Handle event-port (319) messages */
            if (FD_ISSET(event_fd, &readfds)) {
                addr_len = sizeof(client_addr);
                ret = recvfrom(event_fd, recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr *)&client_addr, &addr_len);
                
                if (ret > (ssize_t)sizeof(ptp_header_t)) {
                    ptp_header_t *hdr = (ptp_header_t *)recv_buf;
                    
                    switch (hdr->message_type) {
                    case PTP_MSG_SYNC:
                        if (ret >= (ssize_t)sizeof(ptp_sync_msg_t))
                            handle_sync((ptp_sync_msg_t *)recv_buf);
                        break;
                    case PTP_MSG_DELAY_RESP:
                        if (ret >= (ssize_t)sizeof(ptp_delay_resp_msg_t))
                            handle_delay_resp((ptp_delay_resp_msg_t *)recv_buf);
                        break;
                    }
                }
            }
            
            /* Handle general-port (320) messages */
            if (FD_ISSET(general_fd, &readfds)) {
                addr_len = sizeof(client_addr);
                ret = recvfrom(general_fd, recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr *)&client_addr, &addr_len);
                
                if (ret > (ssize_t)sizeof(ptp_header_t)) {
                    ptp_header_t *hdr = (ptp_header_t *)recv_buf;
                    
                    switch (hdr->message_type) {
                    case PTP_MSG_FOLLOW_UP:
                        handle_follow_up((ptp_follow_up_msg_t *)recv_buf);
                        break;
                    case PTP_MSG_ANNOUNCE:
                        printf("Received Announce\n");
                        break;
                    }
                }
            }
        }
    }
    
    close(event_fd);
    close(general_fd);
    return 0;
}