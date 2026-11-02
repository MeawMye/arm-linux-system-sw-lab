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

---

**Project status:** Ongoing development.

This repository is an educational implementation for exploring ARM64 Linux system software and kernel-driver integration.
