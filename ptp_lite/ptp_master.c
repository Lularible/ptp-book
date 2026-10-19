/**
 * @file ptp_master.c
 * @brief PTP master clock program
 *
 * Sends Announce and Sync/Follow_Up to the PTP multicast group and answers
 * Delay_Req with Delay_Resp. There is no BMCA: this program is a master
 * because it was started as one.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include "ptp_message.h"

/* Send periods, derived from the log2 values that also go on the wire. */
#define SYNC_PERIOD_S     (1 << PTP_DEFAULT_SYNC_INT)      /* 1 s */
#define ANNOUNCE_PERIOD_S (1 << PTP_DEFAULT_ANNOUNCE_INT)  /* 2 s */

#define RECV_BUF_SIZE      1024   /* far larger than any PTP/IPv4 message */
#define SELECT_TIMEOUT_US  100000 /* bounds how late a timer can fire */

/* Only used when the interface MAC cannot be read; identical on every
 * machine, so treat it as a warning sign. */
static const uint8_t fallback_clock_id[8] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
};

static ptp_port_identity_t master_port_id;
static uint16_t sync_seq;
static uint16_t announce_seq;
static int event_fd = -1;    /* bound to 319: Sync out, Delay_Req in */
static int general_fd = -1;  /* bound to 320: Follow_Up/Announce/Delay_Resp out */

static uint64_t g_dropped;   /* rejected datagrams, reported at exit */
static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/**
 * @brief Datagram socket bound to @p port, joined to the PTP multicast group.
 *
 * NOTE: ptp_slave.c keeps its own copy of this function and of send_multicast
 * and init_port_id, so that each program reads end to end without opening a
 * second file. There is no shared ptp_net.[ch] on purpose.
 */
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
    addr.sin_port = htons((uint16_t)port);

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
    mreq.imr_ifindex = (int)if_nametoindex(iface);

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP");
    }

    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));

    return fd;
}

/** @brief Send to the PTP multicast group on @p port (see create_socket). */
static int send_multicast(int fd, const void *data, size_t len, int port)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, PTP_PRIMARY_MCAST, &addr.sin_addr);
    addr.sin_port = htons((uint16_t)port);

    return sendto(fd, data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
}

/** @brief Reply to the sender of the request we just received. */
static int send_unicast(int fd, const void *data, size_t len,
                        const struct sockaddr_in *client_addr)
{
    return sendto(fd, data, len, 0, (const struct sockaddr *)client_addr,
                  sizeof(*client_addr));
}

/**
 * @brief Build the port identity, deriving clockIdentity from the MAC address.
 *
 * EUI-48 -> EUI-64 as in generate_clock_identity(), linuxptp util.c:374-391:
 * first three octets, FF-FE, last three. Replaces the hard-coded
 * 00:11:22:33:44:55:66:77 that made every ptp_lite master on a LAN advertise the
 * same grandmaster identity.
 */
static void init_port_id(const char *iface)
{
    static const uint8_t eui48_to_eui64[2] = { 0xFF, 0xFE };
    struct ifreq ifr;
    const unsigned char *mac;
    int fd;
    int derived = 0;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0 &&
            ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
            mac = (const unsigned char *)ifr.ifr_hwaddr.sa_data;
            memcpy(&master_port_id.clock_identity[0], &mac[0], 3);
            memcpy(&master_port_id.clock_identity[3], eui48_to_eui64, 2);
            memcpy(&master_port_id.clock_identity[5], &mac[3], 3);
            derived = 1;
        }
        close(fd);
    }

    if (derived == 0) {
        memcpy(master_port_id.clock_identity, fallback_clock_id,
               sizeof(fallback_clock_id));
        fprintf(stderr, "Warning: no MAC address for %s, using a fixed "
                "clockIdentity\n", iface);
    }
    master_port_id.port_number = htobe16(1);
}

/** @brief Count and log a rejected datagram. */
static void drop_msg(const char *why)
{
    g_dropped++;
    printf("DROP: %s (total %" PRIu64 ")\n", why, g_dropped);
}

/**
 * @brief May this Delay_Req be answered?
 *
 * Header checks plus one policy check: our own clockIdentity on an inbound
 * Delay_Req means another node shares it - a master sends none of its own.
 */
static int delay_req_ok(const ptp_header_t *hdr, size_t len)
{
    const char *why = ptp_check_header(hdr, len, sizeof(ptp_delay_req_msg_t));

    if (why == NULL &&
        memcmp(hdr->source_port_identity.clock_identity,
               master_port_id.clock_identity, sizeof(ptp_clock_identity_t)) == 0) {
        why = "Delay_Req from our own clockIdentity";
    }
    if (why != NULL) {
        drop_msg(why);
        return 0;
    }
    return 1;
}

static void send_announce(void)
{
    ptp_announce_msg_t msg;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    ptp_init_announce(&msg, announce_seq++, &master_port_id, &ts);
    send_multicast(general_fd, &msg, sizeof(msg), PTP_GENERAL_PORT);

    printf("Sent Announce seq=%u\n", be16toh(msg.header.sequence_id));
}

static void send_sync(void)
{
    ptp_sync_msg_t sync_msg;
    ptp_follow_up_msg_t follow_up_msg;
    struct timespec ts;
    ssize_t ret;

    /* Two-step: the Sync carries no send timestamp, its Follow_Up does, and
     * both share one sequenceId so that the slave can pair them. */
    ptp_init_sync(&sync_msg, sync_seq, &master_port_id);
    ret = send_multicast(event_fd, &sync_msg, sizeof(sync_msg), PTP_EVENT_PORT);

    if (ret > 0) {
        /* t1 - software timestamp, taken right after sendto() returned. */
        clock_gettime(CLOCK_REALTIME, &ts);
        ptp_init_follow_up(&follow_up_msg, sync_seq, &master_port_id, &ts);
        send_multicast(general_fd, &follow_up_msg, sizeof(follow_up_msg),
                       PTP_GENERAL_PORT);
        printf("Sent Sync+Follow_Up seq=%u time=%ld.%09ld\n",
               sync_seq, (long)ts.tv_sec, ts.tv_nsec);
    }
    sync_seq++;
}

/**
 * @brief Answer a Delay_Req with a Delay_Resp, from the general (320) socket.
 * @param hdr validated header of the request; source of the echoed sequenceId
 */
static void handle_delay_req(const ptp_header_t *hdr,
                             const struct sockaddr_in *client_addr)
{
    ptp_delay_resp_msg_t resp;
    struct timespec ts;
    ptp_timestamp_t recv_ts;

    /* t4 - when the Delay_Req arrived. */
    clock_gettime(CLOCK_REALTIME, &ts);
    timespec_to_ptp(&ts, &recv_ts);

    /* IEEE 1588-2019 11.3.2 d) 2) i): the Delay_Req common header is copied and
     * sequenceId is not on the exception list, so it is echoed, as linuxptp does
     * it in one line (port.c:2309); d) 2) v) turns sourcePortIdentity into the
     * requestingPortIdentity below. The old code used a counter of its own. */
    ptp_init_delay_resp(&resp, be16toh(hdr->sequence_id), &master_port_id,
                        &recv_ts, &hdr->source_port_identity);
    /* Table 37, octet 0 bit 2: unicastFlag is TRUE because the address this goes
     * to is unicast (linuxptp sets it on every unicast send, port.c:2318). Such
     * an answer deviates from 9.5.12, which wants multicast - see README. */
    resp.header.flag_field = htobe16(PTP_FLAG_UNICAST);
    send_unicast(general_fd, &resp, sizeof(resp), client_addr);

    printf("Sent Delay_Resp seq=%u time=%ld.%09ld\n",
           be16toh(resp.header.sequence_id), (long)ts.tv_sec, ts.tv_nsec);
}

/**
 * @brief Receive one datagram on the event (319) socket and answer a Delay_Req.
 *
 * recv_buf is not cleared between packets: no byte recvfrom left unwritten can
 * reach a handler - one shorter than the 34-byte header is counted and dropped
 * instead, and ptp_check_header() refuses anything shorter than its structure.
 */
static void handle_event_datagram(uint8_t *buf)
{
    struct sockaddr_in client_addr;
    const ptp_header_t *hdr;
    socklen_t addr_len = sizeof(client_addr);
    ssize_t ret;

    ret = recvfrom(event_fd, buf, RECV_BUF_SIZE, 0,
                   (struct sockaddr *)&client_addr, &addr_len);
    if (ret < (ssize_t)sizeof(ptp_header_t)) {
        drop_msg("datagram shorter than the 34-byte PTP header");
        return;
    }

    hdr = (const ptp_header_t *)buf;
    if (hdr->message_type == PTP_MSG_DELAY_REQ &&
        delay_req_ok(hdr, (size_t)ret)) {
        handle_delay_req(hdr, &client_addr);
    }
}

/** @brief Take Announce/Follow_Up from other nodes off the general socket. */
static void drain_general_socket(uint8_t *buf)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    /* With no BMCA there is nothing to do with these, but they must be taken off
     * the socket or select() would return at once, forever. Not counted in
     * g_dropped: we hear our own multicast back here, ~1.5 a second. */
    (void)recvfrom(general_fd, buf, RECV_BUF_SIZE, 0,
                   (struct sockaddr *)&client_addr, &addr_len);
}

int main(int argc, char *argv[])
{
    uint8_t recv_buf[RECV_BUF_SIZE];
    struct timespec next_announce, next_sync, now;
    struct timeval timeout;
    fd_set readfds;
    int max_fd;
    ssize_t ret;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    printf("ptp_lite master %s (%s)\n", PTP_LITE_VERSION, PTP_LITE_VERSION_DATE);
    printf("Starting PTP Master on interface %s\n", argv[1]);

    /* signal(), not sigaction(): implementation-defined semantics, but Linux and
     * glibc keep the handler installed, which is all a teaching program needs. */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    init_port_id(argv[1]);

    /* One socket per PTP port. Annex D / Table 36 put Sync and Delay_Req on 319
     * (time-stamped events) and Follow_Up, Announce and Delay_Resp on 320. The
     * single 319 socket this program used to have sent those three general
     * messages with UDP source port 319, which a real linuxptp slave discards. */
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
    max_fd = (event_fd > general_fd) ? event_fd : general_fd;

    printf("Master Clock ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
           master_port_id.clock_identity[0], master_port_id.clock_identity[1],
           master_port_id.clock_identity[2], master_port_id.clock_identity[3],
           master_port_id.clock_identity[4], master_port_id.clock_identity[5],
           master_port_id.clock_identity[6], master_port_id.clock_identity[7]);
    printf("Listening on port %d (event) and %d (general)\n",
           PTP_EVENT_PORT, PTP_GENERAL_PORT);

    /* CLOCK_MONOTONIC: these timers measure our own elapsed time and must not
     * move when the system clock is set or steered. */
    clock_gettime(CLOCK_MONOTONIC, &next_announce);
    clock_gettime(CLOCK_MONOTONIC, &next_sync);
    next_announce.tv_sec += ANNOUNCE_PERIOD_S;
    next_sync.tv_sec += SYNC_PERIOD_S;

    while (g_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (now.tv_sec >= next_announce.tv_sec) {
            send_announce();
            /* Accumulate rather than re-anchor to "now", otherwise the 100 ms
             * select() latency is added to every period. After a stall longer
             * than the period the next iterations send at once and re-align. */
            next_announce.tv_sec += ANNOUNCE_PERIOD_S;
        }

        if (now.tv_sec >= next_sync.tv_sec) {
            send_sync();
            next_sync.tv_sec += SYNC_PERIOD_S;
        }

        timeout.tv_sec = 0;
        timeout.tv_usec = SELECT_TIMEOUT_US;

        FD_ZERO(&readfds);
        FD_SET(event_fd, &readfds);
        FD_SET(general_fd, &readfds);

        ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (ret > 0) {
            if (FD_ISSET(general_fd, &readfds)) {
                drain_general_socket(recv_buf);
            }
            if (FD_ISSET(event_fd, &readfds)) {
                handle_event_datagram(recv_buf);
            }
        }
    }

    printf("Stopped, %" PRIu64 " datagram(s) rejected\n", g_dropped);
    close(event_fd);
    close(general_fd);
    return 0;
}
