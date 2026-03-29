/*
 * easy_vfio - A simple C library for VFIO-based PCIe device management
 *
 * This header provides a high-level context-based API for managing PCIe
 * devices via VFIO. The context (evfio_ctx_t) encapsulates all resources
 * for a device's full lifecycle, making it easy to integrate into existing
 * projects as a plug-and-play component.
 *
 * Typical usage:
 *   evfio_ctx_t *ctx = NULL;
 *   evfio_load_vfio_driver("0000:01:00.0");
 *   evfio_open(&ctx, "0000:01:00.0");
 *   // ... use DMA, MSI, MMIO via ctx ...
 *   evfio_close(ctx);
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef EASY_VFIO_H
#define EASY_VFIO_H

#include <stdint.h>
#include <stddef.h>
#include <linux/vfio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  Error Codes
 * ================================================================ */
#define EVFIO_OK             0
#define EVFIO_ERR_INVAL     -1   /* Invalid argument */
#define EVFIO_ERR_OPEN      -2   /* Failed to open file/device */
#define EVFIO_ERR_IOCTL     -3   /* ioctl call failed */
#define EVFIO_ERR_MMAP      -4   /* mmap failed */
#define EVFIO_ERR_ALLOC     -5   /* Memory allocation failed */
#define EVFIO_ERR_PERM      -6   /* Permission denied */
#define EVFIO_ERR_NOGROUP   -7   /* IOMMU group not found */
#define EVFIO_ERR_NOTVIABLE -8   /* Group not viable */
#define EVFIO_ERR_BUSY      -9   /* Resource busy */
#define EVFIO_ERR_NOSYS     -10  /* Not supported */

/* ================================================================
 *  Constants
 * ================================================================ */

/** Maximum BDF string length: "XXXX:XX:XX.X\0" */
#define EVFIO_BDF_MAX_LEN       14

/** Maximum number of MSI/MSI-X vectors managed per device.
 *  32 covers most PCIe devices; increase if your hardware requires more. */
#define EVFIO_MAX_MSI_VECTORS   32

/* ================================================================
 *  Low-Level Type Definitions
 *  Used internally and accessible for advanced usage.
 * ================================================================ */

/** VFIO container - wraps /dev/vfio/vfio file descriptor. */
typedef struct evfio_container {
    int fd;       /* File descriptor for /dev/vfio/vfio, -1 if closed */
} evfio_container_t;

/** VFIO group - wraps /dev/vfio/<group_id> file descriptor. */
typedef struct evfio_group {
    int fd;             /* File descriptor for /dev/vfio/<group_id>, -1 if closed */
    int group_id;       /* IOMMU group number */
    int container_fd;   /* Associated container fd */
} evfio_group_t;

/** VFIO device - wraps a device file descriptor obtained from a group. */
typedef struct evfio_device {
    int fd;                        /* Device file descriptor, -1 if closed */
    char bdf[EVFIO_BDF_MAX_LEN];  /* PCI BDF address (e.g. "0000:01:00.0") */
    uint32_t num_regions;          /* Number of BAR regions */
    uint32_t num_irqs;             /* Number of IRQ types */
    uint32_t flags;                /* Device capability flags */
} evfio_device_t;

/** Mapped BAR region for MMIO access. */
typedef struct evfio_region {
    void    *addr;        /* mmap'd virtual address (NULL if not mapped) */
    uint64_t size;        /* Region size in bytes */
    uint64_t offset;      /* Region offset */
    uint32_t index;       /* Region index (BAR number) */
    uint32_t flags;       /* Region capability flags */
    int      device_fd;   /* Associated device fd */
} evfio_region_t;

/** DMA mapping descriptor. */
typedef struct evfio_dma {
    void    *vaddr;       /* Userspace virtual address */
    uint64_t iova;        /* IO virtual address (device-visible) */
    uint64_t size;        /* Mapping size in bytes */
} evfio_dma_t;

/* ================================================================
 *  Context / Handle
 *
 *  The context encapsulates ALL resources for a single VFIO device.
 *  Create with evfio_open(), destroy with evfio_close().
 *  Pass as the first argument to all high-level API functions.
 *
 *  Design: embed this in your project's global handle for plug-and-play:
 *    struct my_device_handle {
 *        evfio_ctx_t *vfio;
 *        // ... other resources ...
 *    };
 * ================================================================ */

/** MSI vector state - one per enabled interrupt vector. */
typedef struct evfio_msi_vector {
    int event_fd;   /* eventfd for this vector, -1 if unused */
} evfio_msi_vector_t;

/** Full device lifecycle context. */
typedef struct evfio_ctx {
    /* ---- Device Identification ---- */
    char bdf[EVFIO_BDF_MAX_LEN];   /* PCI BDF address */
    int  iommu_group_id;            /* IOMMU group number */

    /* ---- VFIO Core Resources ---- */
    evfio_container_t container;    /* VFIO container (/dev/vfio/vfio) */
    evfio_group_t     group;        /* VFIO IOMMU group */
    evfio_device_t    device;       /* VFIO device */

    /* ---- MSI Interrupt State ---- */
    evfio_msi_vector_t msi_vectors[EVFIO_MAX_MSI_VECTORS];
    uint32_t           msi_count;   /* Number of enabled MSI vectors */
    int                msi_enabled; /* Non-zero if MSI is active */

    /* ---- Lifecycle State ---- */
    int initialized;                /* Non-zero after successful evfio_open() */
} evfio_ctx_t;

/* ================================================================
 *  High-Level Context API
 *
 *  These are the primary APIs for typical usage. Each takes the
 *  device context as the first argument (except load_vfio_driver
 *  and open which create/prepare the context).
 * ================================================================ */

/**
 * Load the VFIO driver for a PCI device.
 *
 * Unbinds the device from its current driver (if any) and binds it to
 * vfio-pci. Idempotent: safe to call multiple times - if the device is
 * already bound to vfio-pci, returns EVFIO_OK immediately.
 *
 * @param bdf  PCI BDF address string (e.g. "0000:01:00.0").
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_load_vfio_driver(const char *bdf);

/**
 * Open and initialize a VFIO device context.
 *
 * Performs all setup in one call: opens container, finds and opens the
 * IOMMU group, sets up IOMMU (TYPE1v2), and opens the device. On success,
 * *ctx points to a fully initialized context ready for DMA/MSI operations.
 *
 * Idempotent: if *ctx is already initialized, returns EVFIO_OK without
 * re-initializing. Pass a NULL-initialized pointer on first call.
 *
 * @param ctx  Pointer to context pointer. Set to allocated context on success.
 * @param bdf  PCI BDF address string (e.g. "0000:01:00.0").
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_open(evfio_ctx_t **ctx, const char *bdf);

/**
 * Close and destroy a VFIO device context.
 *
 * Releases all resources: disables MSI if active, closes device/group/
 * container file descriptors, and frees the context memory.
 * Safe to call with NULL or already-closed context.
 *
 * @param ctx  Context to close (may be NULL).
 */
void evfio_close(evfio_ctx_t *ctx);

/**
 * Enable MSI interrupts for the device.
 *
 * Creates eventfds for each requested vector and associates them with
 * the device's MSI IRQ. The eventfds are stored in ctx->msi_vectors[]
 * for use with evfio_handle_interrupt().
 *
 * @param ctx          Initialized device context.
 * @param num_vectors  Number of MSI vectors to enable (1..EVFIO_MAX_MSI_VECTORS).
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_msi_enable(evfio_ctx_t *ctx, uint32_t num_vectors);

/**
 * Disable MSI interrupts for the device.
 *
 * Disables the MSI IRQ and closes all associated eventfds.
 * Safe to call when MSI is not enabled (returns EVFIO_OK).
 *
 * @param ctx  Initialized device context.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_msi_disable(evfio_ctx_t *ctx);

/**
 * Wait for an MSI interrupt on a specific vector.
 *
 * Blocks until the device triggers the interrupt associated with the
 * given vector number. The vector must have been enabled via evfio_msi_enable().
 *
 * @param ctx     Initialized device context with MSI enabled.
 * @param vector  Vector index (0-based, must be < msi_count).
 * @return EVFIO_OK on interrupt received, negative error code on failure.
 */
int evfio_handle_interrupt(evfio_ctx_t *ctx, uint32_t vector);

/**
 * Map external user memory for DMA (IOMMU mapping only).
 *
 * Creates an IOMMU mapping for caller-owned memory so the device can
 * access it via the specified IOVA. Does NOT allocate memory.
 * Use evfio_dma_unmap() to remove the mapping.
 *
 * @param ctx    Initialized device context.
 * @param dma    DMA descriptor to fill (output).
 * @param vaddr  Virtual address of the caller's buffer.
 * @param size   Buffer size in bytes (will be page-aligned).
 * @param iova   IO virtual address for device access.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_map(evfio_ctx_t *ctx, evfio_dma_t *dma,
                  void *vaddr, uint64_t size, uint64_t iova);

/**
 * Unmap a DMA mapping (IOMMU unmapping only).
 *
 * Removes the IOMMU mapping created by evfio_dma_map(). Does NOT free
 * the underlying memory (the caller owns it).
 *
 * @param ctx  Initialized device context.
 * @param dma  DMA descriptor to unmap.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_unmap(evfio_ctx_t *ctx, evfio_dma_t *dma);

/**
 * Allocate memory and create a DMA mapping.
 *
 * Allocates a page-aligned buffer via mmap and creates an IOMMU mapping.
 * Use evfio_dma_free_unmap() to release both the mapping and the memory.
 *
 * @param ctx   Initialized device context.
 * @param dma   DMA descriptor to fill (output).
 * @param size  Buffer size in bytes (will be page-aligned).
 * @param iova  IO virtual address for device access.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_alloc_map(evfio_ctx_t *ctx, evfio_dma_t *dma,
                        uint64_t size, uint64_t iova);

/**
 * Unmap a DMA mapping and free the allocated memory.
 *
 * Removes the IOMMU mapping and frees the memory allocated by
 * evfio_dma_alloc_map(). Do NOT use this for mappings created
 * with evfio_dma_map() (use evfio_dma_unmap() instead).
 *
 * @param ctx  Initialized device context.
 * @param dma  DMA descriptor to free and unmap.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_free_unmap(evfio_ctx_t *ctx, evfio_dma_t *dma);

/* ================================================================
 *  Low-Level API
 *
 *  These functions provide fine-grained control over individual VFIO
 *  operations. They are used internally by the high-level API and
 *  are also available for advanced use cases.
 * ================================================================ */

/* ---- Container ---- */
int  evfio_container_open(evfio_container_t *container);
void evfio_container_close(evfio_container_t *container);
int  evfio_container_set_iommu(evfio_container_t *container, int type);
int  evfio_container_check_extension(evfio_container_t *container, int extension);

/* ---- Group ---- */
int  evfio_group_open(evfio_group_t *group, evfio_container_t *container, int group_id);
void evfio_group_close(evfio_group_t *group);
int  evfio_group_is_viable(evfio_group_t *group);

/* ---- Device ---- */
int  evfio_device_open(evfio_device_t *device, evfio_group_t *group, const char *bdf);
void evfio_device_close(evfio_device_t *device);
int  evfio_device_reset(evfio_device_t *device);
int  evfio_device_get_region_info(evfio_device_t *device, uint32_t index, struct vfio_region_info *info);
int  evfio_device_get_irq_info(evfio_device_t *device, uint32_t index, struct vfio_irq_info *info);

/* ---- Region (BAR) ---- */
int     evfio_region_map(evfio_region_t *region, evfio_device_t *device, uint32_t index);
void    evfio_region_unmap(evfio_region_t *region);
ssize_t evfio_region_read(evfio_device_t *device, uint32_t index, void *buf, size_t len, uint64_t offset);
ssize_t evfio_region_write(evfio_device_t *device, uint32_t index, const void *buf, size_t len, uint64_t offset);

uint8_t  evfio_mmio_read8(evfio_region_t *region, uint64_t offset);
uint16_t evfio_mmio_read16(evfio_region_t *region, uint64_t offset);
uint32_t evfio_mmio_read32(evfio_region_t *region, uint64_t offset);
uint64_t evfio_mmio_read64(evfio_region_t *region, uint64_t offset);
void evfio_mmio_write8(evfio_region_t *region, uint64_t offset, uint8_t val);
void evfio_mmio_write16(evfio_region_t *region, uint64_t offset, uint16_t val);
void evfio_mmio_write32(evfio_region_t *region, uint64_t offset, uint32_t val);
void evfio_mmio_write64(evfio_region_t *region, uint64_t offset, uint64_t val);

/* ---- IOMMU DMA (low-level mapping only, no memory management) ---- */
int evfio_iommu_dma_map(int container_fd, void *vaddr, uint64_t size, uint64_t iova);
int evfio_iommu_dma_unmap(int container_fd, uint64_t iova, uint64_t size);

/* ---- Interrupt ---- */
int evfio_irq_enable(evfio_device_t *device, uint32_t irq_index, int *fds, uint32_t count);
int evfio_irq_disable(evfio_device_t *device, uint32_t irq_index);
int evfio_irq_unmask_intx(evfio_device_t *device);

/* ---- PCI Config Space ---- */
ssize_t evfio_pci_config_read(evfio_device_t *device, void *buf, size_t len, uint64_t offset);
ssize_t evfio_pci_config_write(evfio_device_t *device, const void *buf, size_t len, uint64_t offset);

/* ---- Utility ---- */
int  evfio_get_iommu_group(const char *bdf);
int  evfio_bind_device(const char *bdf);
int  evfio_unbind_device(const char *bdf);
int  evfio_bdf_valid(const char *bdf);
int  evfio_pci_get_ids(const char *bdf, uint16_t *vendor_id, uint16_t *device_id);
/**
 * Check if a PCI device is currently bound to the vfio-pci driver.
 *
 * @param bdf  PCI BDF address string.
 * @return 1 if bound to vfio-pci, 0 otherwise (including invalid BDF).
 */
int  evfio_is_bound_to_vfio(const char *bdf);
const char *evfio_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* EASY_VFIO_H */
