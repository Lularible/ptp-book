# PTP Lite - A Lightweight PTP Time Synchronization Implementation

A lightweight PTP (Precision Time Protocol) implementation for teaching, following the IEEE 1588-2019 standard.

## Features

- **Simple and clear**: 1586 lines in total across the seven source files, with detailed comments
- **Complete functionality**: implements the full E2E synchronization flow
- **Easy to learn**: ideal for understanding the core mechanisms of the PTP protocol
- **Actually runs**: it really does synchronize time
- **Warning-free build**: `-Wall -Wextra -std=gnu11` with zero diagnostics on x86-64, arm64 and arm32

## Technology choices

- **Delay measurement**: E2E (End-to-End)
- **Transport**: UDP/IPv4 multicast
- **Timestamp type**: software timestamps
- **Servo algorithm**: PI controller

## Quick start

### Build

#### x86 (default)

```bash
make
```

#### ARM64

```bash
make arm64
```

Output: `ptp_master_arm64`, `ptp_slave_arm64`

#### ARM32

```bash
make arm32
```

Output: `ptp_master_arm32`, `ptp_slave_arm32`

#### All architectures

```bash
make all-arch
```

#### Help

```bash
make help
```

`make arm32` needs `arm-linux-gnueabihf-gcc` and spells out the ABI it builds
for; override it per board if your target differs:

```bash
make arm32 ARM32_MCPU=cortex-a7 ARM32_MFLOAT=softfp
```

`-D_GNU_SOURCE` is *not* needed: `-std=gnu11` already enables `_DEFAULT_SOURCE`,
which is what `struct ifreq` and `struct ip_mreqn` come from. Building with
`-std=c11` instead would fail on `htobe16()`.

#### Cross-compile toolchains

**Ubuntu/Debian**:
```bash
# install the ARM64 toolchain
sudo apt install gcc-aarch64-linux-gnu

# install the ARM32 toolchain (hard-float)
sudo apt install gcc-arm-linux-gnueabihf
```

**Fedora/CentOS**:
```bash
# install the ARM64 toolchain
sudo yum install gcc-aarch64-linux-gnu

# install the ARM32 toolchain
sudo yum install gcc-arm-linux-gnu
```

#### Verify the build

```bash
# inspect the architecture of the built programs
file ptp_master        # ELF 64-bit x86-64
file ptp_master_arm64  # ELF 64-bit ARM aarch64
file ptp_master_arm32  # ELF 32-bit ARM
```

### Run the master clock

On one machine:

```bash
sudo ./ptp_master eth0
```

### Run the slave clock

On another machine:

```bash
sudo ./ptp_slave eth0
```

Run them on **two different hosts**, or in two network namespaces on one host. Two
interfaces of one host are not a reliable substitute: without `CAP_NET_RAW` the
`SO_BINDTODEVICE` call only warns and carries on (`create_socket()`), and both
processes are then left bound to `INADDR_ANY` on 319 and 320 with `SO_REUSEPORT`,
so the kernel load-balances each arriving datagram between them and the master can
swallow the slave's `Delay_Req`. Namespaces give each clock its own stack:

```bash
sudo ip netns add ptp-a && sudo ip netns add ptp-b
sudo ip link add veth-a type veth peer name veth-b
sudo ip link set veth-a netns ptp-a
sudo ip link set veth-b netns ptp-b
sudo ip netns exec ptp-a ip addr add 192.0.2.1/24 dev veth-a
sudo ip netns exec ptp-b ip addr add 192.0.2.2/24 dev veth-b
sudo ip netns exec ptp-a ip link set lo up && sudo ip netns exec ptp-a ip link set veth-a up
sudo ip netns exec ptp-b ip link set lo up && sudo ip netns exec ptp-b ip link set veth-b up
```

192.0.2.0/24 is RFC 5737 TEST-NET-1, i.e. documentation space. No multicast route is
needed: `create_socket()` pins both the group membership and the egress interface
with `imr_ifindex`. Then, in two terminals:

```bash
sudo ip netns exec ptp-a ./ptp_master veth-a
sudo ip netns exec ptp-b ./ptp_slave  veth-b
```

The clockIdentity is derived from the interface MAC address, so each veth gets its
own - which is exactly what master and slave on *one* interface could never do: they
would advertise the same identity, and a slave discards every message whose
sourcePortIdentity is its own.

Both programs stop cleanly on `Ctrl-C` (SIGINT/SIGTERM), close their sockets and
print how many datagrams they examined and rejected - a subset of everything they
ignored; see [`DROP:` lines in the log](#4-drop-lines-in-the-log).

### Why sudo

Binding 319/320 does *not* need root - both ports are above 1024. Two calls do:

| call | capability | where |
|---|---|---|
| `clock_settime()` / `clock_adjtime()` | `CAP_SYS_TIME` | `ptp_slave.c`, `adjust_clock()` / `set_freq()` |
| `setsockopt(SO_BINDTODEVICE)` | `CAP_NET_RAW` (Linux >= 5.0; `CAP_NET_ADMIN` on older kernels) | `create_socket()` |

The second one is non-fatal: without it the program only prints
`SO_BINDTODEVICE: Operation not permitted` and keeps working, just without
being pinned to one interface. The first one is not - a slave without
`CAP_SYS_TIME` logs `clock_adjtime FREQ failed` and never adjusts anything.
So the tell-tale symptom of a missing capability is *"I see `Received Sync` and
`Delay_Resp`, but never `FREQ ADJ` or `CLOCK JUMP`"*.

To grant the minimum instead of full root:

```bash
sudo setcap 'cap_net_raw,cap_sys_time+ep' ./ptp_slave
./ptp_slave eth0            # no sudo needed afterwards
```

Note that a rebuild clears the file capabilities, so re-run `setcap` after
every `make`.

### Verify synchronization

```bash
# check the time on both machines
date

# example slave output (one complete measurement round):
# Received Sync seq=0
# Follow_Up seq=0: t1=1234567890.123456789 t2=1234567890.123451211
# Sent Delay_Req seq=0 at 1234567890.200000000
# Delay_Resp: t3=1234567890.200000000 t4=1234567890.200105578 delay=50000 ns offset=-55578 ns
# FREQ ADJ: 5557.80 ppb
```

The numbers above are consistent, and worth checking by hand once:
`delay = ((t2-t1) + (t4-t3)) / 2 = (-5578 + 105578) / 2 = 50000 ns`,
`offset = (t2-t1) - delay = -5578 - 50000 = -55578 ns`, and
`FREQ ADJ = -Kp * offset = -0.1 * (-55578) = 5557.80 ppb` (the integral term is
still zero on the first locked sample).

A round is only complete when a Sync, its Follow_Up and a Delay_Req/Resp pair
with matching sequenceIds all arrive; anything else prints a `DROP:` line
instead. The first sample after start-up deliberately does not adjust the clock
(`SERVO_UNLOCKED`), so it is normal to see one `Delay_Resp` line with no
`FREQ ADJ` after it.

## File structure

```
ptp_lite/
├── README.md           # project documentation
├── LICENSE             # MIT license
├── Makefile            # build script
├── ptp_common.h        # common definitions and types
├── ptp_message.h       # message structure definitions
├── ptp_message.c       # message encoding and inbound validation
├── ptp_servo.h         # servo algorithm header
├── ptp_servo.c         # servo algorithm implementation
├── ptp_master.c        # the master clock program
├── ptp_slave.c         # the slave clock program
└── .gitignore          # git ignore file
```

## Implemented message types

- **Announce**: the master's advertisement
- **Sync + Follow_Up**: time synchronization
- **Delay_Req**: delay request
- **Delay_Resp**: delay response

## Synchronization principle

### E2E delay measurement

```
Master clock             Slave clock
  |                        |
  |-- Sync --------------> | (t2: receive time)
  |                        |
  |-- Follow_Up --------> | (carries t1)
  |                        |
  |                        |-- Delay_Req --> (t3: send time)
  |                        |
  |<-- Delay_Req --------- |
  |                        |
  |-- Delay_Resp -------> | (carries t4)
  |                        |

path delay = [(t2-t1) + (t4-t3)] / 2

true offset = (t2-t1) - path delay

where:
- t1: Sync send time (master clock)
- t2: Sync receive time (slave clock)
- t3: Delay_Req send time (slave clock)
- t4: Delay_Req receive time (master clock)
```

### PI controller

A proportional-integral controller smoothly disciplines the clock frequency:

```
frequency adjustment = -Kp × offset - Ki × ∫offset dt

parameters (see ptp_servo.h):
- Kp = 0.1      proportional gain
- Ki = 0.001    integral gain
```

These are the *software* timestamp gains of linuxptp (`SWTS_KP_SCALE` /
`SWTS_KI_SCALE`, `pi.c:30-31`). Its hardware-timestamp gains are 0.7 / 0.3,
which is seven and three hundred times hotter and far too aggressive for
timestamps that carry tens of microseconds of scheduling jitter.

Three protections keep the loop from running away:

- `SERVO_MAX_FREQ_PPB` = ±500000 ppb (±500 ppm) clamps the output, matching the
  fallback limit linuxptp uses when the kernel reports no adjustable range
  (`clockadj.c:139-140`). Uncapped, one sample at the 10 ms step threshold would
  ask for `Kp × 1e7` = 1e6 ppb = **1000 ppm**, twice the clamp; the kernel answers
  EINVAL while the integral keeps charging. That is what the clamp is for - not
  for keeping `timex.freq` inside an `int32`: 1e6 ppb scaled by 65.536 is ~6.6e7,
  which fits comfortably on every ABI ptp_lite builds for.
- The integral term is **not** accumulated while the output is saturated
  (anti-windup, `pi.c:142-148`), and is separately clamped to
  `SERVO_MAX_INTEGRAL_NS`.
- An offset whose magnitude exceeds `SERVO_STEP_THRESHOLD` (10 ms) produces one
  `CLOCK JUMP`; beyond `SERVO_MAX_OFFSET_NS` (2^53 ns, about 104 days - the
  point where an `int64_t` no longer converts to `double` exactly) it is
  rejected as an invalid measurement instead. Bring two clocks that far apart
  roughly in line with `date -s` before starting.

## Configuration parameters

The main configuration is defined in `ptp_common.h`:

```c
#define PTP_PRIMARY_MCAST      "224.0.1.129"  // multicast address
#define PTP_EVENT_PORT         319             // event port
#define PTP_GENERAL_PORT       320             // general port
#define PTP_DEFAULT_DOMAIN     0               // default domain
#define PTP_DEFAULT_PRIORITY1  128             // priority 1
#define PTP_DEFAULT_PRIORITY2  128             // priority 2
#define PTP_DEFAULT_ANNOUNCE_INT  1            // log2(s): Announce every 2 s
#define PTP_DEFAULT_SYNC_INT      0            // log2(s): Sync every 1 s
#define PTP_CURRENT_UTC_OFFSET 37              // seconds, from IERS Bulletin C
#define NS_PER_SEC             1000000000LL
#define PTP_MAX_VALID_SECONDS  4294967295ULL   // bound for a decoded timestamp
```

The two `*_INT` values are `log2(seconds)` and drive **both** the send timer and
the `logMessageInterval` field on the wire (Sync and Follow_Up carry `0x00`,
Announce carries `0x01`, Delay_Req and the unicast Delay_Resp carry `0x7F`).

`PTP_CURRENT_UTC_OFFSET` is 37 because that has been the leap-second offset
since 2017-01-01. ptp_lite keeps no leap-second table: when IERS Bulletin C
announces a change, update this one constant. It is announced as valid by the
`utcOffsetValid` and `ptpTimescale` flag bits set in `ptp_init_announce()`.

Servo constants live in `ptp_servo.h`, message flags in `ptp_message.h`, and the
receive-period constants (`DELAY_REQ_PERIOD_S`, `SELECT_TIMEOUT_US`,
`RECV_BUF_SIZE`) in each program, next to the loop that uses them.

## System requirements

- Linux operating system
- GCC compiler (`-std=gnu11`, glibc or an equivalent; see the
  `ptp_clock_adjtime` block in `ptp_slave.c`)
- `CAP_SYS_TIME` and `CAP_NET_RAW`, i.e. root or `setcap` - see
  [Why sudo](#why-sudo). Only the slave adjusts the clock; the master can run
  unprivileged apart from the `SO_BINDTODEVICE` warning.
- two hosts on the same network, each with its own network interface

## Firewall configuration

```bash
# allow the PTP ports
sudo iptables -A INPUT -p udp --dport 319 -j ACCEPT
sudo iptables -A INPUT -p udp --dport 320 -j ACCEPT
```

## Accuracy notes

Because software timestamps are used, expect **tens of microseconds, not
nanoseconds**:

- Typical: ±100 microseconds, with the occasional millisecond spike under load.
- The Announce this program sends agrees with that: `clockAccuracy` is `0xFE`
  ("unknown") and `clockClass` 248, so a real BMCA would never prefer it over a
  traceable grandmaster. Do not read better numbers into this project than the
  messages it publishes.
- Two structural limits cap the achievable accuracy no matter how idle the
  machine is: neither `t1` nor `t3` comes from the kernel's transmit path, and the
  delay formula assumes the two directions are symmetric, which no congested LAN
  is. The timestamps are read at opposite ends of `sendto()`: `t3` *before* it is
  called (`ptp_slave.c:377` then `:379`), `t1` *after* it returned
  (`ptp_master.c:216` then `:220`). Write `δ_s`/`δ_m` for the slave-side and
  master-side `sendto()` overhead. Then `t3` is early by `δ_s` and `t1` is late by
  `δ_m`, so `delay = path + (δ_s − δ_m)/2` and
  `offset = true offset + asymmetry/2 − (δ_s + δ_m)/2`. Two consequences: `delay`
  is essentially unbiased when the two calls cost about the same, while `offset`
  carries a steady negative bias of between half and one full `sendto()` latency -
  the slave is systematically made to look slow by the cost of its own syscall.

Five further known limitations, all deliberate:

1. **t2 and t3 are read from the clock being disciplined.** `CLOCK_REALTIME` is
   what `clock_settime()`/`clock_adjtime()` move, so a step landing between t2
   and t3 corrupts both terms at once. The code mitigates this by invalidating
   the whole measurement round after every sample (`clear_round()`), so no
   timestamp survives a clock step, and by keeping the send timers on
   `CLOCK_MONOTONIC`. A complete fix needs a separate monotonic measurement time
   base for the local interval, which is beyond the scope of this project.
2. **No BMCA, no announce timeout.** Exactly one master is followed - the first
   one seen - and if it disappears the slave keeps running on its own oscillator
   without noticing.
3. **"`Delay_Resp` unicast" violates a *shall*.** §9.5.12: "Delay_Resp messages
   shall be transmitted as multicast if the associated Delay_Req message was sent
   as multicast." The `Delay_Req` here *is* multicast (`ptp_slave.c:379`), and the
   master answers the requester privately anyway (`ptp_master.c:255`). Kept on
   purpose: in a teaching network answering the asker is easier to watch, and the
   slaves do not hear each other's responses. Two consequences follow. A conformant
   multicast peer that ignores unicast traffic to its PTP port will not pair the
   response, so ptp_lite does not interoperate with such an implementation. And
   `logMessageInterval = 0x7F` is defensible only for this unicast answer - Table 42
   wants `portDS.logMinDelayReqInterval` in a multicast Delay_Resp, and §9.5.7 b)
   tells a conformant slave to copy that field into its own `logMinDelayReqInterval`,
   so a `0x7F` reply would hand it a meaningless interval. `unicastFlag`
   (Table 37, octet 0 bit 2) is set, as linuxptp does on every unicast send.
   Changing to multicast would need a `logMinDelayReqInterval` data set ptp_lite
   does not keep, and would make every slave hear every other slave's response -
   which the `requestingPortIdentity` test then has to filter out again. So the
   deviation stays, and is written down here instead.
4. **Any TLV on an inbound message is a rejection.** `ptp_check_header()` requires
   `message_length` to equal the size of the structure it is about to cast to, so a
   peer with security extensions enabled - linuxptp appends an AUTHENTICATION TLV in
   `sad_append_auth_tlv()` (`port.c:3311-3313`), or NSM response data (`port.c:2321`)
   - is dropped with `message_length does not match the type`. The exact test is
   kept because it is also the guard against truncated datagrams; interop needs real
   TLV walking, which this project does not do.
5. **The master identity is won by whoever speaks first.** `master_id_ok()`
   (`ptp_slave.c:204-227`) accepts the first datagram that passes the generic checks and
   never re-elects, so one forged Announce arriving before the real master's owns
   this slave until it is restarted. `clockIdentity` is plaintext on the wire: this
   is a filter against accidents, not authentication. For the real thing see 1588
   Annex P (and chapter 2.16 of this book).

## FAQ

### 1. No messages received

Check the firewall and multicast configuration:

```bash
ip maddr show eth0
```

### 2. Time is inaccurate

Make sure the system clock isn't being disturbed by other services such as NTP:

```bash
timedatectl set-ntp false
```

### 3. Offset is large

Software timestamps have limited accuracy; try:
- reducing system load
- using a real-time kernel
- upgrading to hardware timestamps

### 4. `DROP:` lines in the log

Both programs count and print the datagrams they look at and refuse, together with
the reason. That is the normal outcome of the inbound checks and not an error by
itself. The reasons worth watching for:

- `Follow_Up does not match the newest Sync` / `Delay_Resp sequenceId is not the
  one we sent` - normal when packets are lost, duplicated or reordered, or when
  a measurement round was invalidated by a clock step;
- `sourcePortIdentity is our own clockIdentity` - our own multicast came back
  through loopback, or master and slave share one interface and their identities
  collide (see [Run the slave clock](#run-the-slave-clock));
- `sourcePortIdentity is not the master we follow` - a second master appeared on
  the LAN. ptp_lite has no BMCA and sticks to the first one it saw;
- `datagram shorter than the 34-byte PTP header` - something that is not PTP at
  all arrived on 319 or 320;
- `version_ptp` / `domain_number` / `message_length` - a non-PTPv2 peer, another
  domain, a truncated datagram, or a peer that appends TLVs (limitation 4).

What the exit line `Stopped, N datagram(s) rejected` does **not** count: on the
master, the Announce and Follow_Up it hears back from its own multicast. With no
BMCA there is nothing to do with any general message, and counting the ~1.5 of our
own per second would bury every real rejection - so `N` is a count of datagrams
examined and refused, not of every datagram ignored.

Run `tcpdump -i eth0 -s0 -vv 'udp port 319 or 320'` alongside to see what is
actually on the wire.

## Learning path

Recommended reading order:

1. Read the README for a project overview.
2. Study `ptp_common.h` to understand the base data types.
3. Analyze `ptp_message.c` to learn message encoding.
4. Run the master to watch messages being sent.
5. Run the slave to understand the sync flow.
6. Change parameters and experiment.

## Extension ideas

Possible future improvements:

- [ ] add hardware timestamp support (`SIOCSHWTSTAMP`, `SO_TIMESTAMPING`)
- [ ] implement the BMCA algorithm (this project follows the first master it sees)
- [ ] add a management protocol (`PTP_MSG_MANAGEMENT`, a `pmc`-compatible UDS)
- [ ] support multiple ports (note: the two sockets per program for 319/320 are a
      transport requirement, not multi-port support)
- [ ] add security extensions (the inbound checks here are unauthenticated: they
      reject malformed, truncated, out-of-domain and foreign-master datagrams,
      but they cannot stop anyone who knows the master's clockIdentity)

## References

- [IEEE 1588-2019 standard](https://standards.ieee.org/standard/1588-2019.html)
- [LinuxPTP project](http://linuxptp.sourceforge.net/)

## Versioning

`PTP_LITE_VERSION` and `PTP_LITE_VERSION_DATE` in `ptp_common.h` are printed in
the start-up banner of both programs. They are the only surviving part of an
older book/code synchronization mechanism, so the convention is deliberately
minimal:

- bump the version when the code and the accompanying chapters change together,
- update the date to the day of that change,
- keep the two strings consistent with the version quoted in the book.

## License

MIT - see [LICENSE](LICENSE).

## Author

This code accompanies the PTP tutorial and is provided for educational purposes.
Every simplification and known defect is documented in chapter 4 of the book;
read `ptp_lite/` as the source of truth and the chapters as the explanation.

## Contributing

Issues and pull requests are welcome!
