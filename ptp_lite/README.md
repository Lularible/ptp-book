# PTP Lite - A Lightweight PTP Time Synchronization Implementation

A lightweight PTP (Precision Time Protocol) implementation for teaching, following the IEEE 1588-2019 standard.

## Features

- **Simple and clear**: ~1,000 lines of code in total, with detailed comments
- **Complete functionality**: implements the full E2E synchronization flow
- **Easy to learn**: ideal for understanding the core mechanisms of the PTP protocol
- **Actually runs**: it really does synchronize time
- **Warning-free build**: strict coding conventions, suitable for teaching

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

### Verify synchronization

```bash
# check the time on both machines
date

# example slave output:
# Sent Delay_Req seq=0 at 1234567890.123456789
# Sync seq=0: t1=1234567890.123456789 t2=1234567890.123470000 offset=-12345 ns
# FREQ ADJ: -12.34 ppb
# Delay_Resp: t3=... t4=... delay=5678 ns corrected_offset=-12345 ns
```

## File structure

```
ptp_lite/
├── README.md           # project documentation
├── Makefile            # build script
├── ptp_common.h        # common definitions and types
├── ptp_message.h       # message structure definitions
├── ptp_message.c       # message encoding implementation
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

parameters:
- Kp = 0.7 (proportional gain)
- Ki = 0.3 (integral gain)
```

## Configuration parameters

The main configuration is defined in `ptp_common.h`:

```c
#define PTP_PRIMARY_MCAST      "224.0.1.129"  // multicast address
#define PTP_EVENT_PORT         319             // event port
#define PTP_GENERAL_PORT       320             // general port
#define PTP_DEFAULT_DOMAIN     0               // default domain
#define PTP_DEFAULT_PRIORITY1  128             // priority 1
#define PTP_DEFAULT_PRIORITY2  128             // priority 2
```

## System requirements

- Linux operating system
- GCC compiler
- root privileges (to adjust the system clock)
- two machines on the same network

## Firewall configuration

```bash
# allow the PTP ports
sudo iptables -A INPUT -p udp --dport 319 -j ACCEPT
sudo iptables -A INPUT -p udp --dport 320 -j ACCEPT
```

## Accuracy notes

Because software timestamps are used, accuracy is typically:

- Typical: ±100 microseconds
- Best case: ±10 microseconds
- Affected by system load

For higher accuracy, use a NIC with hardware timestamp support.

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

- [ ] add hardware timestamp support
- [ ] implement the BMCA algorithm
- [ ] add a management protocol
- [ ] support multiple ports
- [ ] add security extensions

## References

- [IEEE 1588-2019 standard](https://standards.ieee.org/standard/1588-2019.html)
- [LinuxPTP project](http://linuxptp.sourceforge.net/)

## License

MIT License

## Author

This code accompanies the PTP tutorial and is provided for educational purposes.

## Contributing

Issues and pull requests are welcome!
