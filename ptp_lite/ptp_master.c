/**
 * @file ptp_master.c
 * @brief PTP master clock program
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
#include "ptp_message.h"

static ptp_port_identity_t master_port_id;
static uint16_t sync_seq = 0;
static uint16_t announce_seq = 0;
static uint16_t delay_resp_seq = 0;

static int create_socket(const char *iface)
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

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PTP_EVENT_PORT);

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

static int send_unicast(int fd, const void *data, size_t len, struct sockaddr_in *client_addr)
{
    return sendto(fd, data, len, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
}

static void init_port_id(void)
{
    memset(&master_port_id, 0, sizeof(master_port_id));
    master_port_id.clock_identity[0] = 0x00;
    master_port_id.clock_identity[1] = 0x11;
    master_port_id.clock_identity[2] = 0x22;
    master_port_id.clock_identity[3] = 0x33;
    master_port_id.clock_identity[4] = 0x44;
    master_port_id.clock_identity[5] = 0x55;
    master_port_id.clock_identity[6] = 0x66;
    master_port_id.clock_identity[7] = 0x77;
    master_port_id.port_number = htobe16(1);
}

static void send_announce(int fd)
{
    ptp_announce_msg_t msg;
    struct timespec ts;
    
    clock_gettime(CLOCK_REALTIME, &ts);
    ptp_init_announce(&msg, announce_seq++, &master_port_id, &ts);
    send_multicast(fd, &msg, sizeof(msg), PTP_GENERAL_PORT);
    
    printf("Sent Announce seq=%u\n", be16toh(msg.header.sequence_id));
}

static void send_sync(int fd)
{
    ptp_sync_msg_t sync_msg;
    ptp_follow_up_msg_t follow_up_msg;
    struct timespec ts;
    ssize_t ret;
    
    ptp_init_sync(&sync_msg, sync_seq, &master_port_id);
    ret = send_multicast(fd, &sync_msg, sizeof(sync_msg), PTP_EVENT_PORT);
    
    if (ret > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ptp_init_follow_up(&follow_up_msg, sync_seq, &master_port_id, &ts);
        send_multicast(fd, &follow_up_msg, sizeof(follow_up_msg), PTP_GENERAL_PORT);
        printf("Sent Sync+Follow_Up seq=%u time=%ld.%09ld\n", sync_seq, ts.tv_sec, ts.tv_nsec);
    }
    sync_seq++;
}

static void handle_delay_req(int fd, uint8_t *data, struct sockaddr_in *client_addr)
{
    ptp_delay_req_msg_t *req = (ptp_delay_req_msg_t *)data;
    ptp_delay_resp_msg_t resp;
    struct timespec ts;
    ptp_timestamp_t recv_ts;
    
    clock_gettime(CLOCK_REALTIME, &ts);
    timespec_to_ptp(&ts, &recv_ts);
    
    ptp_init_delay_resp(&resp, delay_resp_seq++, &master_port_id, &recv_ts, &req->header.source_port_identity);
    send_unicast(fd, &resp, sizeof(resp), client_addr);
    
    printf("Sent Delay_Resp seq=%u time=%ld.%09ld\n", be16toh(resp.header.sequence_id), ts.tv_sec, ts.tv_nsec);
}

int main(int argc, char *argv[])
{
    int fd;
    uint8_t recv_buf[1024];
    struct timespec next_announce, next_sync, now;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    ssize_t ret;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }
    
    printf("Starting PTP Master on interface %s\n", argv[1]);
    
    init_port_id();
    fd = create_socket(argv[1]);
    
    if (fd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }
    
    printf("Master Clock ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
           master_port_id.clock_identity[0], master_port_id.clock_identity[1],
           master_port_id.clock_identity[2], master_port_id.clock_identity[3],
           master_port_id.clock_identity[4], master_port_id.clock_identity[5],
           master_port_id.clock_identity[6], master_port_id.clock_identity[7]);
    
    clock_gettime(CLOCK_MONOTONIC, &next_announce);
    clock_gettime(CLOCK_MONOTONIC, &next_sync);
    next_announce.tv_sec += 2;
    next_sync.tv_sec += 1;
    
    while (1) {
        fd_set readfds;
        struct timeval timeout;
        
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        if (now.tv_sec >= next_announce.tv_sec) {
            send_announce(fd);
            next_announce.tv_sec = now.tv_sec + 2;
        }
        
        if (now.tv_sec >= next_sync.tv_sec) {
            send_sync(fd);
            next_sync.tv_sec = now.tv_sec + 1;
        }
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret > 0 && FD_ISSET(fd, &readfds)) {
            addr_len = sizeof(client_addr);
            ret = recvfrom(fd, recv_buf, sizeof(recv_buf), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
            
            if (ret > (ssize_t)sizeof(ptp_header_t)) {
                ptp_header_t *hdr = (ptp_header_t *)recv_buf;
                if (hdr->message_type == PTP_MSG_DELAY_REQ &&
                    ret >= (ssize_t)sizeof(ptp_delay_req_msg_t)) {
                    handle_delay_req(fd, recv_buf, &client_addr);
                }
            }
        }
    }
    
    close(fd);
    return 0;
}