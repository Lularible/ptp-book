/**
 * @file ptp_slave.c
 * @brief PTP slave clock program - dual-socket implementation (IEEE 1588 subset)
 *
 * Follows one grandmaster with the E2E mechanism: t2 when a Sync arrives, t1
 * from the matching Follow_Up, t3 when a Delay_Req goes out, t4 from the
 * matching Delay_Resp; a PI servo then disciplines CLOCK_REALTIME.
 *
 * Known limitation (see README.md): t2 and t3 come from CLOCK_REALTIME, the
 * clock this program steps and steers, so they only cancel out of the delay
 * formula while nothing moves the local clock in between. Every consumed
 * sample therefore invalidates the whole round (clear_round()); a real fix
 * needs a separate CLOCK_MONOTONIC measurement time base.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include "ptp_message.h"
#include "ptp_servo.h"

/*
 * Portable clock_adjtime(). The old "#ifndef clock_adjtime" guard was always
 * taken - clock_adjtime is a function, never a macro - so on any system whose
 * headers do declare it the build died with "static declaration of
 * 'clock_adjtime' follows non-static declaration". glibc gained the wrapper in
 * 2.34 (earlier it was visible only behind _GNU_SOURCE), so test the feature
 * the way linuxptp does with HAVE_CLOCK_ADJTIME, under a name of our own.
 */
#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 34)
static inline int ptp_clock_adjtime(clockid_t id, struct timex *tx)
{
    return syscall(__NR_clock_adjtime, id, tx);
}
#else
#define ptp_clock_adjtime(id, tx) clock_adjtime((id), (tx))
#endif

#define RECV_BUF_SIZE      1024   /* far larger than any PTP/IPv4 message */
#define SELECT_TIMEOUT_US  100000 /* bounds how late a timer can fire */
#define DELAY_REQ_PERIOD_S 1      /* a property of this slave, not of the message set */

/* timex.freq is ppm scaled by 2^16, so ppb * 2^16 / 1000 == ppb * 65.536
 * (same factor as linuxptp clockadj.c:64 and :81). */
#define PPB_TO_TIMEX_FREQ  65.536

/* Only used when the interface MAC cannot be read; identical on every
 * machine, so treat it as a warning sign. */
static const uint8_t fallback_clock_id[8] = {
    0x00, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
};

static ptp_port_identity_t slave_port_id;
static pi_servo_t servo;

/* The measurement round being assembled. t4 is local to handle_delay_resp(). */
static struct timespec t1, t2, t3;
static int t1_valid, t2_valid, t3_valid;

static uint16_t last_sync_seq;             /* newest accepted Sync */
static uint16_t delay_req_seq;             /* counter for outgoing Delay_Req */
static uint16_t outstanding_delay_req_seq; /* the one we are waiting to hear about */

/* Clock we follow, learned from the first accepted message and held on to, so a
 * second master on the same LAN cannot drag us between two time bases. BMCA would
 * choose; "first one wins" is the shortcut instead, and the claimant keeps the
 * slot for the life of the process - one well formed Announce arriving before the
 * real master's owns this slave until restart. A filter, not authentication. */
static ptp_clock_identity_t master_id;
static int master_id_known;

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
 * NOTE: ptp_master.c keeps its own copy of this function and of send_multicast
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

/**
 * @brief Build the port identity, deriving clockIdentity from the MAC address.
 *
 * EUI-48 -> EUI-64 as in generate_clock_identity(), linuxptp util.c:374-391:
 * first three octets, FF-FE, last three. Replaces the hard-coded
 * 00:22:33:44:55:66:77:88 every copy of ptp_lite shared. Master and slave on
 * one interface would now get the same identity - give each role its own.
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
            memcpy(&slave_port_id.clock_identity[0], &mac[0], 3);
            memcpy(&slave_port_id.clock_identity[3], eui48_to_eui64, 2);
            memcpy(&slave_port_id.clock_identity[5], &mac[3], 3);
            derived = 1;
        }
        close(fd);
    }

    if (derived == 0) {
        memcpy(slave_port_id.clock_identity, fallback_clock_id,
               sizeof(fallback_clock_id));
        fprintf(stderr, "Warning: no MAC address for %s, using a fixed "
                "clockIdentity\n", iface);
    }
    slave_port_id.port_number = htobe16(1);
}

/** @brief Count and log a rejected datagram. */
static void drop_msg(const char *why)
{
    g_dropped++;
    printf("DROP: %s (total %" PRIu64 ")\n", why, g_dropped);
}

/**
 * @brief Accept this sender as the master we follow?
 *
 * Rejects our own multicast heard back through loopback, and anything from a
 * clockIdentity other than the one learned first.
 *
 * @return 1 to accept, 0 if it is not the identity we follow, -1 if it is our own
 *         clockIdentity - accept_msg() logs a different reason for each.
 */
static int master_id_ok(const ptp_header_t *hdr)
{
    const ptp_clock_identity_t *src = &hdr->source_port_identity.clock_identity;

    if (memcmp(*src, slave_port_id.clock_identity,
               sizeof(ptp_clock_identity_t)) == 0) {
        return -1;
    }
    if (master_id_known == 0) {
        memcpy(master_id, *src, sizeof(ptp_clock_identity_t));
        master_id_known = 1;
        return 1;
    }
    return memcmp(*src, master_id, sizeof(ptp_clock_identity_t)) == 0;
}

/** @brief Inbound filter: generic header checks plus the identity test. */
static int accept_msg(const ptp_header_t *hdr, size_t len, size_t expect)
{
    const char *why = ptp_check_header(hdr, len, expect);
    int id = (why == NULL) ? master_id_ok(hdr) : 1;

    if (id < 0) {
        why = "sourcePortIdentity is our own clockIdentity";
    } else if (id == 0) {
        why = "sourcePortIdentity is not the master we follow";
    }
    if (why != NULL) {
        drop_msg(why);
        return 0;
    }
    return 1;
}

/** @brief Apply an absolute frequency trim. @return 0 on success. */
static int set_freq(double freq_ppb)
{
    struct timex tx;

    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_FREQUENCY;
    /* The servo clamps freq_ppb to +-SERVO_MAX_FREQ_PPB, so the scaled value
     * (5e5 * 65.536 ~= 3.3e7) always fits in a 32-bit long. The clamp is there
     * against kernel EINVAL and integrator divergence, not against that overflow:
     * with Kp = 0.1 an unclamped output reaches 1e6 ppb, ~6.5e7 scaled, no UB. */
    tx.freq = (long)(freq_ppb * PPB_TO_TIMEX_FREQ);

    if (ptp_clock_adjtime(CLOCK_REALTIME, &tx) < 0) {
        perror("clock_adjtime FREQ failed");
        return -1;
    }
    printf("FREQ ADJ: %.2f ppb\n", freq_ppb);
    return 0;
}

/** @brief Act on one servo sample: step the phase or steer the frequency. */
static void adjust_clock(int64_t offset_ns, double freq_ppb, servo_state_t state)
{
    struct timespec ts;
    int64_t abs_ns;

    switch (state) {
    case SERVO_JUMP:
        /* linuxptp clears the trim before stepping (clock.c:2056 then :2059):
         * the rate we were steering at says nothing once the clock is moved by
         * force. freq_ppb is 0.0 on a jump. */
        if (set_freq(freq_ppb) < 0) {
            return;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        /* offset_ns is bounded by SERVO_MAX_OFFSET_NS, so nothing below can
         * overflow; negating an unchecked value is UB at INT64_MIN. */
        abs_ns = (offset_ns < 0) ? -offset_ns : offset_ns;
        if (offset_ns < 0) {
            /* Behind the master: move the clock forward. */
            ts.tv_sec  += (time_t)(abs_ns / NS_PER_SEC);
            ts.tv_nsec += (long)(abs_ns % NS_PER_SEC);
            if (ts.tv_nsec >= NS_PER_SEC) {
                ts.tv_sec++;
                ts.tv_nsec -= NS_PER_SEC;
            }
        } else {
            /* Ahead of the master: move the clock backward. */
            ts.tv_sec  -= (time_t)(abs_ns / NS_PER_SEC);
            ts.tv_nsec -= (long)(abs_ns % NS_PER_SEC);
            if (ts.tv_nsec < 0) {
                ts.tv_sec--;
                ts.tv_nsec += NS_PER_SEC;
            }
        }

        if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
            perror("clock_settime failed");
            printf("Failed to adjust clock by %" PRId64 " ns\n", offset_ns);
        } else {
            printf("CLOCK JUMP: %" PRId64 " ns (new time: %ld.%09ld)\n",
                   offset_ns, (long)ts.tv_sec, ts.tv_nsec);
        }
        break;

    case SERVO_LOCKED:
    case SERVO_LOCKED_STABLE:
        (void)set_freq(freq_ppb);
        break;

    case SERVO_UNLOCKED:
    default:
        /* Nothing to act on; the trim already in force is kept. */
        break;
    }
}

/**
 * @brief Forget the round in progress.
 *
 * t2 and t3 came from CLOCK_REALTIME, so neither means what it said once the
 * clock moved. This is what stops a stale t2/t3 pair straddling a step from
 * producing a garbage delay.
 */
static void clear_round(void)
{
    t1_valid = 0;
    t2_valid = 0;
    t3_valid = 0;
}

/** @brief Sync received: t2. */
static void handle_sync(const ptp_header_t *hdr)
{
    clock_gettime(CLOCK_REALTIME, &t2);
    t2_valid = 1;
    t1_valid = 0;   /* this Sync's send timestamp is still to come */
    last_sync_seq = be16toh(hdr->sequence_id);
    printf("Received Sync seq=%u\n", last_sync_seq);
}

/** @brief Follow_Up received: t1, but only for the newest Sync. */
static void handle_follow_up(const ptp_follow_up_msg_t *msg)
{
    uint16_t seq = be16toh(msg->header.sequence_id);

    /* Sequence pairing: a Follow_Up is only usable with the Sync carrying the
     * same sequenceId; a late, duplicated or forged one would otherwise replace
     * t1 with a timestamp unrelated to the t2 we measured. */
    if (seq != last_sync_seq || t2_valid == 0) {
        drop_msg("Follow_Up does not match the newest Sync");
        return;
    }
    if (ptp_to_timespec(&msg->precise_origin_timestamp, &t1) != 0) {
        drop_msg("Follow_Up preciseOriginTimestamp out of range");
        return;
    }
    t1_valid = 1;
    printf("Follow_Up seq=%u: t1=%ld.%09ld t2=%ld.%09ld\n", seq,
           (long)t1.tv_sec, t1.tv_nsec, (long)t2.tv_sec, t2.tv_nsec);
}

/** @brief Delay_Req sent: t3. */
static void send_delay_req(int event_fd)
{
    ptp_delay_req_msg_t msg;

    ptp_init_delay_req(&msg, delay_req_seq++, &slave_port_id);
    clock_gettime(CLOCK_REALTIME, &t3);

    if (send_multicast(event_fd, &msg, sizeof(msg), PTP_EVENT_PORT) < 0) {
        perror("sendto Delay_Req");
        return;
    }
    t3_valid = 1;
    outstanding_delay_req_seq = be16toh(msg.header.sequence_id);
    printf("Sent Delay_Req seq=%u at %ld.%09ld\n",
           outstanding_delay_req_seq, (long)t3.tv_sec, t3.tv_nsec);
}

/** @brief Delay_Resp received: t4, then the E2E math and the servo update. */
static void handle_delay_resp(const ptp_delay_resp_msg_t *msg)
{
    struct timespec t4;
    servo_state_t state;
    int64_t dt_sync, dt_req, delay, offset;
    double freq;

    /* IEEE 1588-2019 9.5.7: a Delay_Resp is associated with our Delay_Req only
     * if requestingPortIdentity and the echoed sequenceId both match - 9.5.7
     * calls the latter requestingSequenceId. linuxptp: port.c:2348, :2354-2358. */
    if (be16toh(msg->header.sequence_id) != outstanding_delay_req_seq) {
        drop_msg("Delay_Resp sequenceId is not the one we sent");
        return;
    }
    /* From here on the round is finished whatever happens: every exit below
     * invalidates t1/t2/t3, so a stale timestamp can never be paired with a
     * fresh one from the next round. */
    if (memcmp(&msg->requesting_port_identity, &slave_port_id,
               sizeof(ptp_port_identity_t)) != 0) {
        drop_msg("Delay_Resp is not addressed to this port");
        clear_round();
        return;
    }
    if (t1_valid == 0 || t2_valid == 0 || t3_valid == 0) {
        drop_msg("Delay_Resp without a complete Sync/Delay_Req round");
        clear_round();
        return;
    }
    if (ptp_to_timespec(&msg->receive_timestamp, &t4) != 0) {
        drop_msg("Delay_Resp receiveTimestamp out of range");
        clear_round();
        return;
    }

    /* Both tv_sec operands are widened to int64_t before the difference is taken:
     * on an ILP32 target the subtraction would otherwise run in a 32-bit time_t,
     * where (time_t)sec has already wrapped negative. The decoded seconds are
     * bounded by PTP_MAX_VALID_SECONDS, so |dsec| < 2^32 and every product below
     * stays inside int64_t (2^32 * 1e9 ~= 4.29e18 < INT64_MAX ~= 9.22e18). */
    dt_sync = ((int64_t)t2.tv_sec - (int64_t)t1.tv_sec) * NS_PER_SEC +
              (int64_t)(t2.tv_nsec - t1.tv_nsec);
    dt_req  = ((int64_t)t4.tv_sec - (int64_t)t3.tv_sec) * NS_PER_SEC +
              (int64_t)(t4.tv_nsec - t3.tv_nsec);
    delay   = (dt_sync + dt_req) / 2;
    offset  = dt_sync - delay;

    if (delay < 0 || offset > SERVO_MAX_OFFSET_NS ||
        offset < -SERVO_MAX_OFFSET_NS) {
        /* A negative path delay, or an offset beyond SERVO_MAX_OFFSET_NS, is
         * not a very large offset: it is a measurement that straddled a clock
         * adjustment, a stale or hostile timestamp pair. Reject it rather than
         * set the system time to an arbitrary value. */
        drop_msg("measured delay/offset out of range");
        clear_round();
        return;
    }

    printf("Delay_Resp: t3=%ld.%09ld t4=%ld.%09ld delay=%" PRId64
           " ns offset=%" PRId64 " ns\n",
           (long)t3.tv_sec, t3.tv_nsec, (long)t4.tv_sec, t4.tv_nsec,
           delay, offset);

    freq = pi_servo_sample(&servo, offset, &state);
    adjust_clock(offset, freq, state);
    clear_round();     /* consumed - never reused, least of all after a jump */
}

/** @brief Announce received. Nothing more: no BMCA, no receipt timeout. */
static void handle_announce(void)
{
    printf("Received Announce\n");
}

/**
 * @brief Receive one datagram and act on it.
 * @param event_port non-zero for the 319 socket (Sync, Delay_Resp), zero for
 *                   the 320 socket (Follow_Up, Announce)
 *
 * recv_buf is not cleared between packets: no byte recvfrom left unwritten can
 * reach a handler, because every case below refuses a datagram unless as many
 * bytes arrived as the structure it casts to needs. Follow_Up used to lack that
 * guard, so a 35-byte datagram made handle_follow_up() read uninitialised stack
 * into t1 - one huge fake offset away from a clock step.
 */
static void handle_datagram(int fd, uint8_t *buf, int event_port)
{
    /* Out-parameter for recvfrom only: ptp_lite authenticates a sender by its
     * clockIdentity (accept_msg), not by IP address. */
    struct sockaddr_in client_addr;
    const ptp_header_t *hdr;
    socklen_t addr_len = sizeof(client_addr);
    size_t len;
    ssize_t ret;

    ret = recvfrom(fd, buf, RECV_BUF_SIZE, 0,
                   (struct sockaddr *)&client_addr, &addr_len);
    if (ret < (ssize_t)sizeof(ptp_header_t)) {
        drop_msg("datagram shorter than the 34-byte PTP header");
        return;
    }

    hdr = (const ptp_header_t *)buf;
    len = (size_t)ret;

    if (event_port != 0) {
        switch (hdr->message_type) {
        case PTP_MSG_SYNC:
            if (accept_msg(hdr, len, sizeof(ptp_sync_msg_t))) {
                handle_sync(hdr);
            }
            break;
        case PTP_MSG_DELAY_RESP:
            if (accept_msg(hdr, len, sizeof(ptp_delay_resp_msg_t))) {
                handle_delay_resp((const ptp_delay_resp_msg_t *)buf);
            }
            break;
        default:
            break;
        }
    } else {
        switch (hdr->message_type) {
        case PTP_MSG_FOLLOW_UP:
            if (accept_msg(hdr, len, sizeof(ptp_follow_up_msg_t))) {
                handle_follow_up((const ptp_follow_up_msg_t *)buf);
            }
            break;
        case PTP_MSG_ANNOUNCE:
            if (accept_msg(hdr, len, sizeof(ptp_announce_msg_t))) {
                handle_announce();
            }
            break;
        default:
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int event_fd, general_fd, max_fd;
    uint8_t recv_buf[RECV_BUF_SIZE];
    ssize_t ret;
    struct timespec next_delay_req, now;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    printf("ptp_lite slave %s (%s)\n", PTP_LITE_VERSION, PTP_LITE_VERSION_DATE);
    printf("Starting PTP Slave on interface %s\n", argv[1]);

    /* signal(), not sigaction(): implementation-defined semantics, but Linux and
     * glibc keep the handler installed, which is all a teaching program needs. */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    init_port_id(argv[1]);
    pi_servo_init(&servo);

    /* 319 carries the time-stamped events, 320 the general messages. */
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

    /* CLOCK_MONOTONIC has nothing to do with PTP time, so our timer must not
     * move when we step or steer CLOCK_REALTIME. */
    clock_gettime(CLOCK_MONOTONIC, &next_delay_req);
    next_delay_req.tv_sec += DELAY_REQ_PERIOD_S;

    while (g_running) {
        fd_set readfds;
        struct timeval timeout;

        clock_gettime(CLOCK_MONOTONIC, &now);

        if (now.tv_sec >= next_delay_req.tv_sec) {
            send_delay_req(event_fd);
            /* Accumulate rather than re-anchor to "now", or the 100 ms select()
             * latency is added to every period. */
            next_delay_req.tv_sec += DELAY_REQ_PERIOD_S;
        }

        timeout.tv_sec = 0;
        timeout.tv_usec = SELECT_TIMEOUT_US;

        FD_ZERO(&readfds);
        FD_SET(event_fd, &readfds);
        FD_SET(general_fd, &readfds);

        ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (ret > 0) {
            if (FD_ISSET(event_fd, &readfds)) {
                handle_datagram(event_fd, recv_buf, 1);
            }
            if (FD_ISSET(general_fd, &readfds)) {
                handle_datagram(general_fd, recv_buf, 0);
            }
        }
    }

    printf("Stopped, %" PRIu64 " datagram(s) rejected\n", g_dropped);
    close(event_fd);
    close(general_fd);
    return 0;
}
