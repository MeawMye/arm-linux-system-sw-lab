# arm-linux-system-sw-lab
QEMU-based ARM64 Linux system software lab covering kernel modules, character drivers, concurrency, and virtual hardware interfaces.


# ARM Linux System SW Lab

A hands-on ARM64 Linux system software project using QEMU and Buildroot.

This project explores how Linux applications, kernel drivers, Device Tree descriptions, and virtual hardware interact. The implementation is developed incrementally, with each stage focusing on a specific Linux kernel subsystem.

## 1. Project Overview

**Objective**

Understand the interaction between userspace applications, Linux kernel drivers, and hardware interfaces through practical implementation and debugging.

**Development Environment**

| Component | Environment |
|---|---|
| Host OS | Ubuntu 24.04 (x86_64) |
| Virtualization | VirtualBox |
| Target Architecture | ARM64 (AArch64) |
| Emulator | QEMU 8.2.2 (`virt` machine) |
| Build System | Buildroot |
| Guest Kernel | Linux 6.12.27 |
| Guest Userspace | BusyBox |
| Cross Compiler | Buildroot AArch64 toolchain |
| Language | C |

## 2. System Architecture

The project currently connects a custom character device to a Device Tree-matched platform driver.

```text
Userspace
   |
   | read() / write()
   v
/dev/mydevice
   |
   v
Linux VFS
   |
   v
Character Device (miscdevice)
   |
   +-- 256-byte Ring Buffer
   +-- Mutex Synchronization
   |
   v
Platform Driver
   |
   +-- probe() / remove()
   +-- Device Tree Matching
   +-- GPIO Descriptor Acquisition
   |
   v
Linux GPIO Subsystem
   |
   v
QEMU PL061 GPIO Controller
```

GPIO acquisition and driver binding are implemented. GPIO value control is planned for a subsequent stage.

## 3. Implementation

### Character Device Driver

Implemented a Linux character device using `miscdevice` and `file_operations`.

Key features:

- `/dev/mydevice` registration
- Userspace data transfer with `copy_from_user()` and `copy_to_user()`
- 256-byte circular buffer
- Wrap-around handling
- Partial read/write support
- `-EAGAIN` handling for empty and full buffer conditions
- Mutex synchronization protecting shared buffer state

### Platform Driver Integration

Extended the driver to use the Linux platform driver model.

- Registered a `platform_driver`
- Implemented `probe()` and `remove()` callbacks
- Added an Open Firmware device matching table
- Used `platform_set_drvdata()` and `platform_get_drvdata()` for driver-specific data
- Acquired and released GPIO resources using `gpiod_get()` and `gpiod_put()`
- Registered and deregistered the character device during driver binding and removal

### Device Tree Integration

Modified the QEMU Device Tree to describe a custom device associated with the PL061 GPIO controller.

```dts
mydevice {
    compatible = "demo,mydevice";
    control-gpios = <&pl061 0 0>;
};
```

The `compatible` property is matched against the driver's `of_device_id` table.

The `control-gpios` property describes the GPIO resource requested by the driver.

## 4. Repository Structure

```text
arm-linux-system-sw-lab/
├── devicetree/
│   ├── qemu-virt.dts
│   ├── qemu-virt.dtb
│   ├── qemu-mydevice.dts
│   └── qemu-mydevice.dtb
├── kernel/
│   └── mydevice/
│       ├── Makefile
│       └── mydevice.c
├── .gitignore
└── README.md
```

## 5. Build and Execution

The commands below assume that Buildroot and the Linux kernel have already been built in `~/buildroot`.

### Build the Kernel Module

On the Ubuntu host:

```bash
cd ~/arm-linux-system-sw-lab/kernel/mydevice

make -C ~/buildroot/output/build/linux-6.12.27 \
    M=$PWD \
    ARCH=arm64 \
    CROSS_COMPILE=$HOME/buildroot/output/host/bin/aarch64-buildroot-linux-gnu- \
    modules
```

### Copy the Module into the Root Filesystem

Ensure QEMU is stopped before modifying the root filesystem image.

```bash
sudo mkdir -p /mnt/arm-rootfs

sudo mount -o loop \
    ~/buildroot/output/images/rootfs.ext2 \
    /mnt/arm-rootfs

sudo cp ~/arm-linux-system-sw-lab/kernel/mydevice/mydevice.ko \
    /mnt/arm-rootfs/root/

sync
sudo umount /mnt/arm-rootfs
```

### Boot QEMU with the Modified Device Tree

The project uses a modified Device Tree binary:

```text
devicetree/qemu-mydevice.dtb
```

Start QEMU using the Buildroot-generated launch script and provide the custom DTB through QEMU's `-dtb` option.

### Load the Driver

In the QEMU guest:

```bash
insmod /root/mydevice.ko

dmesg | tail -n 20

ls -l /dev/mydevice
```

The driver's `probe()` callback initializes its resources and registers the character device when a matching platform device is bound.

To unload:

```bash
rmmod mydevice
```

## 6. Validation

### Character Device Functional Tests

The following behaviors were verified using shell commands in the QEMU guest.

| Test | Result |
|---|---|
| Basic read/write data transfer | Passed |
| Empty buffer `-EAGAIN` | Passed |
| Ring buffer wrap-around | Passed |
| Full buffer `-EAGAIN` | Passed |
| Partial write | Passed |

Example:

```bash
printf 'HELLO' > /dev/mydevice
dd if=/dev/mydevice bs=5 count=1 status=none
```

Expected output:

```text
HELLO
```

### GPIO Resource Integration

The platform driver obtains its GPIO descriptor through:

```c
gpiod_get(&pdev->dev, "control", GPIOD_OUT_LOW);
```

The descriptor is released during driver removal using `gpiod_put()`.

GPIO output switching and its runtime validation are not yet included in this stage.

## 7. Development Progress

| Stage | Scope | Status |
|---|---|---|
| 1 | QEMU ARM64 Linux bring-up and kernel inspection | Completed |
| 2 | Character device and circular buffer | Completed |
| 3 | Mutex-based synchronization | Implemented |
| 4 | Platform driver and Device Tree integration | Implemented |
| 5 | GPIO output control | In progress |
| 6 | Userspace device-control application | Planned |

## 8. Learning Focus

This project emphasizes understanding Linux kernel interfaces and subsystem interactions rather than only implementing functional code.

Topics explored include:

- Linux kernel module lifecycle
- Userspace-to-kernel data transfer
- Character device interfaces
- Circular buffer management and synchronization
- Linux platform device-driver matching
- Device Tree hardware descriptions
- GPIO descriptor-based resource management
- QEMU-based embedded Linux development and debugging

## PollIN / Wait Queue Validation

- Verified that poll() times out when the ring buffer is empty.
- Verified that poll() waked up when another process writes data to /dev/mydevice. 
- Confirmed POLLIN readiness before the 30-second timeout.

## POLLOUT Validation

- Verified that POLLOUT times out after 30 seconds when the 256-bytes ring buffer is full.
- Verified that reading 1 byte frees buffer space and wakes the waiting poll() process.
- Confirmed that POLLOUT readiness is reported after buffer space becomes available.

---
## P4. Userspace Monitoring Daemon

### Overview

Implemented a multithreaded monitoring daemon that collects system statistics and receives events from a custom Linux character device.

The daemon runs on the QEMU ARM64 guest and integrates two independent data producers with a shared, bounded queue and a consumer thread.

### Architecture

```text
/proc/stat, /proc/meminfo
          |
          v
    Proc Producer
          |
          +--------------------+
                               |
/dev/mydevice                  |
          |                    |
     poll() / read()           |
          |                    |
          v                    v
  Device Producer        System Data
          |                    |
          +---------+----------+
                    |
                    v
        Shared Ring Queue (16)
          Mutex + Condition Variables
                    |
                    v
                Consumer
                    |
                    v
             Console Output
```

### Implementation

- Implemented a system-statistics producer that reads `/proc/stat` and `/proc/meminfo`.
- Implemented a device-event producer using `poll()` and `read()` on `/dev/mydevice`.
- Used a 16-entry circular queue to transfer data between producer and consumer threads.
- Applied `pthread_mutex_t` and `pthread_cond_t` for synchronized queue access and blocking.
- Used a tagged union to distinguish system statistics from device events.
- Implemented coordinated thread termination using `active_producers` and `stop_requested`.

### ARM64 Integration Test

The daemon was cross-compiled using the Buildroot AArch64 toolchain and deployed to the QEMU guest running Linux 6.12.27.

**Test procedure**

1. Boot QEMU with the modified `qemu-mydevice.dtb`.
2. Load the `mydevice.ko` kernel module.
3. Start the monitoring daemon in the background.
4. Write a test message to `/dev/mydevice`.
5. Verify the message reaches the consumer and the daemon terminates normally.

```bash
insmod /root/mydevice.ko

/usr/bin/monitor_daemon &
printf 'hello' > /dev/mydevice
wait
```

**Observed results**

| Test | Result |
|---|---|
| Platform driver binding and device registration | Passed |
| CPU and memory monitoring | 30 samples collected |
| Device event transfer | 5 bytes (`hello`) |
| Device event received by consumer | Passed |
| Producer-consumer synchronization | Passed |
| Final queue occupancy | 0 |
| Remaining active producers | 0 |

Selected console output:

```text
Read 5 bytes
usr_data: hello

CPU: 1.00%
Memory: 13.28%

used: 0, head: 15, cpu_data: 0.000000, active_producers: 0
```

The `usr_data: hello` message was printed by the consumer thread, confirming successful delivery through the shared queue.

The final queue index is consistent with 31 enqueued records (30 system samples and one device event) in a 16-entry circular queue.

### Validation Scope

The integration test confirms the basic end-to-end data path:

`Userspace write() → Kernel Driver → Kernel Ring Buffer → poll()/read() → Device Producer → Shared Queue → Consumer`

Queue saturation under concurrent load, extended stress testing, and comprehensive error-recovery scenarios remain outside the validated scope.

This repository is an educational implementation for exploring ARM64 Linux system software and kernel-driver integration.

