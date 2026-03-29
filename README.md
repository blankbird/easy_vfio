# easy_vfio

A simple C library for managing PCIe devices via the Linux VFIO (Virtual Function I/O) framework. Provides a clean API for container/group/device lifecycle, BAR MMIO access, DMA memory mapping, interrupt setup, and PCI config space read/write.

## Features

- **Container management** – open/close `/dev/vfio/vfio`, set IOMMU type
- **Group management** – open/close IOMMU groups, viability checks
- **Device management** – get device fd, query regions/IRQs, reset
- **BAR region access** – mmap-based MMIO with typed 8/16/32/64-bit accessors, or pread/pwrite-based PIO
- **DMA mapping** – allocate page-aligned buffers and map them for device DMA
- **Interrupt handling** – enable/disable eventfd-based INTx, MSI, MSI-X
- **PCI config space** – read/write arbitrary offsets
- **Utility helpers** – BDF validation, IOMMU group lookup, device bind/unbind to `vfio-pci`

## Building

```bash
make            # Build static (libeasy_vfio.a) and shared (libeasy_vfio.so) libraries
make test       # Build and run unit tests
make examples   # Build example programs
make clean      # Remove build artifacts
```

### Install / Uninstall

```bash
sudo make install    # Install to /usr/local by default
sudo make uninstall
```

Override the install prefix with `PREFIX=/path make install`.

## Quick Start

```c
#include "easy_vfio.h"

int main(void) {
    evfio_container_t container;
    evfio_group_t group;
    evfio_device_t device;
    const char *bdf = "0000:01:00.0";

    /* 1. Find the IOMMU group */
    int group_id = evfio_get_iommu_group(bdf);

    /* 2. Open container & group */
    evfio_container_open(&container);
    evfio_group_open(&group, &container, group_id);
    evfio_container_set_iommu(&container, VFIO_TYPE1v2_IOMMU);

    /* 3. Open the device */
    evfio_device_open(&device, &group, bdf);

    /* 4. Map BAR0 for MMIO */
    evfio_region_t bar0;
    evfio_region_map(&bar0, &device, VFIO_PCI_BAR0_REGION_INDEX);
    uint32_t reg = evfio_mmio_read32(&bar0, 0x00);
    evfio_region_unmap(&bar0);

    /* 5. DMA mapping */
    evfio_dma_t dma;
    evfio_dma_map(&container, &dma, 4096, 0x100000);
    /* dma.vaddr is the CPU-visible pointer; dma.iova is the device address */
    evfio_dma_unmap(&container, &dma);

    /* 6. Cleanup */
    evfio_device_close(&device);
    evfio_group_close(&group);
    evfio_container_close(&container);
    return 0;
}
```

See `examples/example_basic.c` for a more complete example.

## Prerequisites

- Linux with IOMMU enabled (`intel_iommu=on` or `amd_iommu=on` in kernel cmdline)
- The `vfio-pci` kernel module loaded (`modprobe vfio-pci`)
- The target PCIe device bound to `vfio-pci` (use `evfio_bind_device()` or manual sysfs bind)
- Appropriate permissions for `/dev/vfio/` (root, or membership in the VFIO group)

## API Reference

| Category | Function | Description |
|----------|----------|-------------|
| Container | `evfio_container_open()` | Open `/dev/vfio/vfio` |
| | `evfio_container_close()` | Close the container |
| | `evfio_container_set_iommu()` | Set IOMMU type |
| | `evfio_container_check_extension()` | Query extension support |
| Group | `evfio_group_open()` | Open & attach IOMMU group |
| | `evfio_group_close()` | Close the group |
| | `evfio_group_is_viable()` | Check group viability |
| Device | `evfio_device_open()` | Get device fd by BDF |
| | `evfio_device_close()` | Close device |
| | `evfio_device_reset()` | PCI function-level reset |
| | `evfio_device_get_region_info()` | Query region info |
| | `evfio_device_get_irq_info()` | Query IRQ info |
| Region | `evfio_region_map()` | mmap a BAR |
| | `evfio_region_unmap()` | Unmap a BAR |
| | `evfio_region_read()` / `evfio_region_write()` | PIO-based region I/O |
| | `evfio_mmio_read{8,16,32,64}()` | Typed MMIO reads |
| | `evfio_mmio_write{8,16,32,64}()` | Typed MMIO writes |
| DMA | `evfio_dma_map()` | Allocate & map DMA buffer |
| | `evfio_dma_unmap()` | Unmap & free DMA buffer |
| IRQ | `evfio_irq_enable()` | Enable IRQs with eventfds |
| | `evfio_irq_disable()` | Disable IRQs |
| | `evfio_irq_unmask_intx()` | Unmask INTx |
| Config | `evfio_pci_config_read()` | Read PCI config space |
| | `evfio_pci_config_write()` | Write PCI config space |
| Utility | `evfio_get_iommu_group()` | Look up IOMMU group for BDF |
| | `evfio_bind_device()` | Bind device to vfio-pci |
| | `evfio_unbind_device()` | Unbind device from driver |
| | `evfio_bdf_valid()` | Validate BDF format |
| | `evfio_pci_get_ids()` | Read vendor/device IDs |
| | `evfio_strerror()` | Error code to string |

## License

MIT