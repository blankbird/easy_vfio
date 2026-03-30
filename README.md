# easy_vfio

A simple C library for managing PCIe devices via the Linux VFIO (Virtual Function I/O) framework. Provides a high-level context-based API for plug-and-play integration, backed by modular low-level building blocks.

## Features

- **Context-based lifecycle** – one handle manages all device resources (container, group, device, IOMMU, interrupts)
- **Plug-and-play design** – embed `evfio_ctx_t` in your project's global handle, init on startup, deinit on shutdown
- **Driver management** – idempotent `evfio_load_vfio_driver()` to bind devices to `vfio-pci` using `driver_override`
- **MSI interrupts** – enable/disable MSI vectors with automatic eventfd management
- **DMA mapping** – map external memory or allocate+map library-managed buffers
- **BAR MMIO access** – mmap-based typed 8/16/32/64-bit accessors
- **PCI config space** – read/write arbitrary offsets
- **Defensive design** – idempotent operations, NULL safety, safe repeated open/close

## Building

### Using build.sh (recommended)

```bash
./build.sh          # Build all (static + shared library)
./build.sh test     # Build and run tests
./build.sh clean    # Remove build directory
./build.sh rebuild  # Clean + build
./build.sh install  # Install (requires prior build)
```

### Using CMake directly

```bash
mkdir build && cd build
cmake ..
cmake --build . -- -j$(nproc)
ctest --output-on-failure
```

## Quick Start

```c
#include "easy_vfio.h"

int main(void) {
    evfio_ctx_t *ctx = NULL;
    const char *bdf = "0000:01:00.0";

    /* 1. Bind device to vfio-pci (idempotent) */
    evfio_load_vfio_driver(bdf);

    /* 2. Open device (container + group + IOMMU + device) */
    evfio_open(&ctx, bdf);

    /* 3. Enable MSI interrupt (1 vector) */
    evfio_msi_enable(ctx, 1);

    /* 4. Allocate + map DMA buffer */
    evfio_dma_t dma;
    evfio_dma_alloc_map(ctx, &dma, 4096, 0x100000);
    /* dma.vaddr = CPU pointer, dma.iova = device address */

    /* 5. Or map external memory */
    evfio_dma_t ext_dma;
    char my_buf[4096];
    ext_dma.vaddr = my_buf;
    ext_dma.size = sizeof(my_buf);
    evfio_dma_map(ctx, &ext_dma, 0x200000);

    /* 6. Wait for interrupt */
    evfio_handle_interrupt(ctx, 0);

    /* 7. Cleanup */
    evfio_dma_unmap(ctx, &ext_dma);
    evfio_dma_free_unmap(ctx, &dma);
    evfio_close(ctx);  /* Disables MSI, closes all fds, frees ctx */
    return 0;
}
```

See `examples/example_basic.c` for a complete example.

## Integration

The library is designed for easy integration into existing projects:

```c
/* Embed in your project's device handle */
struct my_device {
    evfio_ctx_t *vfio;   /* VFIO context */
    /* ... your other resources ... */
};

/* Initialize */
evfio_load_vfio_driver(bdf);
evfio_open(&my_dev->vfio, bdf);

/* Use throughout your application */
evfio_dma_alloc_map(my_dev->vfio, &dma, size, iova);

/* Cleanup on shutdown */
evfio_close(my_dev->vfio);
```

## API Reference

### High-Level Context API

| Function | Description |
|----------|-------------|
| `evfio_load_vfio_driver(bdf)` | Bind device to vfio-pci (idempotent) |
| `evfio_open(&ctx, bdf)` | Create and initialize device context |
| `evfio_close(ctx)` | Destroy context and release all resources |
| `evfio_msi_enable(ctx, n)` | Enable MSI with n vectors (creates eventfds) |
| `evfio_msi_disable(ctx)` | Disable MSI and close eventfds |
| `evfio_handle_interrupt(ctx, vec)` | Wait for interrupt on vector |
| `evfio_dma_map(ctx, dma, iova)` | Map external memory for DMA (fill dma.vaddr/size first) |
| `evfio_dma_unmap(ctx, dma)` | Unmap DMA (no memory free) |
| `evfio_dma_alloc_map(ctx, dma, size, iova)` | Allocate memory + DMA map |
| `evfio_dma_free_unmap(ctx, dma)` | DMA unmap + free memory |

### Low-Level API (Advanced)

| Category | Function | Description |
|----------|----------|-------------|
| Container | `evfio_container_open/close()` | Open/close `/dev/vfio/vfio` |
| | `evfio_container_set_iommu()` | Set IOMMU type |
| Group | `evfio_group_open/close()` | Open/close IOMMU group |
| Device | `evfio_device_open/close()` | Get device fd by BDF |
| | `evfio_device_reset()` | PCI function-level reset |
| Region | `evfio_region_map/unmap()` | mmap/unmap BAR |
| | `evfio_mmio_read/write{8,16,32,64}()` | Typed MMIO access |
| IRQ | `evfio_irq_enable/disable()` | Manage IRQs with eventfds |
| Config | `evfio_pci_config_read/write()` | PCI config space I/O |
| Utility | `evfio_bdf_valid()` | Validate BDF format |
| | `evfio_get_iommu_group()` | Look up IOMMU group |
| | `evfio_strerror()` | Error code to string |

## Prerequisites

- Linux with IOMMU enabled (`intel_iommu=on` or `amd_iommu=on`)
- The `vfio-pci` kernel module loaded (`modprobe vfio-pci`)
- CMake >= 3.10 and a C11 compiler
- Appropriate permissions for `/dev/vfio/` (root, or VFIO group membership)

## License

MIT