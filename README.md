# PTP技术书 — 从思想实验到协议实现

一本从思想实验到源码、从理论到动手实现的开源PTP技术书。

<p align="center">
  <a href="README.md"><img src="assets/lang-zh-active.svg" alt="中文" height="30"></a>&nbsp;&nbsp;&nbsp;<a href="en/README.md"><img src="assets/lang-en.svg" alt="English" height="30"></a>
</p>

> 🌐 English version: [ptp-book (EN)](en/README.md)。English chapters live in [`en/chapters/`](en/chapters/)。

## 在线阅读

[📖 在线浏览本书](https://web-l.github.io/lularible-books/ptp-book/index.html)

## 实际运行效果

![ptp_demo](https://github.com/user-attachments/assets/d6544ebd-833d-48e3-9830-e8b5e57791c3)

## 这本书讲了什么

全书 41 节，分四章：

- **第一章（4 节）**：从“你周围的一切都静止了”这个思想实验开始，讲时间的本质与同步的意义
- **第二章（18 节）**：逐机制拆解 PTP 协议——BMCA 选举、四个时间戳的数学、透明时钟、硬件时间戳、安全机制
- **第三章（13 节）**：走进 LinuxPTP 源码，看工业级实现如何驾驭 9 种端口状态、PI 伺服控制器怎么让时钟“追上”主时钟
- **第四章（6 节）**：亲手实现一个轻量级 PTP 程序（ptp-lite，约 1000 行 C），主时钟和从时钟可以实际运行起来做同步

**不需要网络协议的先修知识。第一章的四节思想实验足够让你进入状态。**

## 快速开始

在线阅读：直接浏览 `chapters/` 目录下的 Markdown 文件，按文件名顺序阅读。

推荐 VS Code + Markdown Preview Enhanced 插件，或者 Typora、Obsidian。

运行示例代码：

```bash
git clone https://github.com/Lularible/ptp-book.git
cd ptp-book/ptp_lite
make

# 终端 A
sudo ./ptp_master eth0[替换为实际网卡名]

# 终端 B
sudo ./ptp_slave eth0[替换为实际网卡名]
```

## 许可证

书籍内容：[CC BY-NC-ND 4.0](LICENSE) · ptp-lite 源码：MIT

## 姊妹篇

本书是"汽车电子七部曲"系列中的一部。另外六部已发布：

- **[从沙子到车辙——一个工程师的理解](https://github.com/Lularible/from-sand-to-ruts)** — 从图灵机到 CAN 总线，从半导体物理到 AUTOSAR，一部为汽车电子工程师写的全景入门
- **[HSM 技术书——从思想实验到安全基石](https://github.com/Lularible/hsm-book)** — 从岩画密码学到硬件安全模块，完整覆盖车载 HSM 的技术链路
- **[存储 技术书——在不可靠的硬件上构建可靠的数据家园](https://github.com/Lularible/storage-book)** — 一本关于存储技术演进与文件系统实现的深度技术书籍
- **[UDS 技术书——从望闻问切到UDS协议实现](https://github.com/Lularible/uds-book)** — 一本从诊断元问题出发，直通ISO 14229协议规范与AUTOSAR DCM源码、再到亲手实现UDS栈的技术书
- **[功能安全——ISO 26262分析与代码实现](https://github.com/Lularible/safety-book-iso26262)** — 以免疫系统为叙事线索的功能安全技术书。兼顾ISO 26262标准分析、源码拆解与动手实现
- **[汽车嵌入式软件工程——用建筑学隐喻讲工程化](https://github.com/Lularible/swe-book)** — 工程方法论卷：架构原则与质量基础设施，附可运行的 CI 流水线教学项目 eng-lite

## 致谢

- 感谢 [@web-l](https://github.com/web-l) 构建并维护本系列的 [mdBook 在线阅读站](https://web-l.github.io/lularible-books/)，方便了大家阅读。

---

如果觉得有用，点个 ⭐ 就是最好的支持。当然，如果能顺手转发给身边需要的人，那就更棒了。🚗💨
