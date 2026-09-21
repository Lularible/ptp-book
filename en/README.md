# Precision Time Protocol: From Thought Experiments to a Working Implementation

An open-source technical book on PTP: from thought experiments to kernel-level source code, from theory to hands-on implementation.

<p align="center">
  <a href="../README.md"><img src="../assets/lang-zh.svg" alt="中文" height="30"></a>&nbsp;&nbsp;&nbsp;<a href="README.md"><img src="../assets/lang-en-active.svg" alt="English" height="30"></a>
</p>

> 🌐 Chinese version: [ptp-book (中文)](../README.md). Chinese chapters live in [`chapters/`](../chapters/).

## Read Online

[Browse this book online (Chinese)](https://web-l.github.io/lularible-books/ptp-book/index.html)

## Live Demo

![ptp_demo](https://github.com/user-attachments/assets/d6544ebd-833d-48e3-9830-e8b5e57791c3)

## What This Book Covers

The book has 41 sections in four parts:

- Part 1 (4 sections): Starts from the thought experiment "Everything around you stands still", and explores the nature of time and why synchronization matters.
- Part 2 (18 sections): Dismantles the PTP protocol mechanism by mechanism, the BMCA election, the mathematics of the four timestamps, transparent clocks, hardware timestamps, and the security mechanisms.
- Part 3 (13 sections): Dives into the LinuxPTP source code to see how an industrial-grade implementation manages the nine port states and how the PI servo controller makes a clock "catch up" with the master.
- Part 4 (6 sections): Builds a lightweight PTP implementation from scratch (ptp-lite, roughly 1,000 lines of C). The master and slave clocks actually run and synchronize.

No prior knowledge of networking protocols is required. The four thought experiments in Part 1 are enough to get you up to speed.

## Quick Start

Read online: browse the Markdown files under `en/chapters/`, in filename order.

Recommended: VS Code with the Markdown Preview Enhanced extension, or Typora / Obsidian.

Running the example code:

```bash
git clone https://github.com/Lularible/ptp-book.git
cd ptp-book/ptp_lite
make

# Terminal A
sudo ./ptp_master eth0[replace with your network interface]

# Terminal B
sudo ./ptp_slave eth0[replace with your network interface]
```

## License

Book content: [CC BY-NC-ND 4.0](../LICENSE) · ptp-lite source code: MIT

## Companion Books

This book is one volume of the "Automotive Electronics Septet" series. The other six volumes are already published:

- **[From Sand to Ruts — An Engineer's Understanding](https://github.com/Lularible/from-sand-to-ruts)** — From Turing machines to the CAN bus, from semiconductor physics to AUTOSAR; a panoramic primer written for automotive electronics engineers.
- **[HSM — From Thought Experiments to a Security Cornerstone](https://github.com/Lularible/hsm-book)** — From cave-painting cryptography to hardware security modules, covering the full technical chain of the automotive HSM.
- **[Storage — Building a Reliable Data Home on Unreliable Hardware](https://github.com/Lularible/storage-book)** — An in-depth technical book on the evolution of storage technology and the implementation of filesystems.
- **[UDS — From Diagnosis to a UDS Protocol Implementation](https://github.com/Lularible/uds-book)** — Starting from the meta-questions of diagnostics, straight through the ISO 14229 specification and the AUTOSAR DCM source code, to building a UDS stack by hand.
- **[Functional Safety — ISO 26262 Analysis and Code Implementation](https://github.com/Lularible/safety-book-iso26262)** — A functional-safety book narrated through the metaphor of the immune system, combining ISO 26262 analysis, source-code dissection, and hands-on implementation.
- **[Automotive Embedded Software Engineering — Engineering Through an Architectural Metaphor](https://github.com/Lularible/swe-book)** — The engineering-methodology volume: architecture principles and quality infrastructure, with a runnable CI-pipeline teaching project, eng-lite.

## Acknowledgements

- Thanks to [@web-l](https://github.com/web-l) for building and maintaining the [mdBook site](https://web-l.github.io/lularible-books/) for this series, which makes it easier for everyone to read.

---

If you find it useful, a ⭐ is the best support. And if you can pass it on to someone who needs it, even better. 🚗💨
