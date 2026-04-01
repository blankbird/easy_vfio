/*
 * easy_vfio - A simple C library for VFIO-based PCIe device management
 *
 * This header provides a high-level context-based API for managing PCIe
 * devices via VFIO. Supports both legacy VFIO (pre-6.8 container/group)
 * and new VFIO (6.8+ iommufd/cdev) backends.
 *
 * The context (vfio_ctx_t) encapsulates all resources for a device's
 * full lifecycle, making it easy to integrate into existing projects
 * as a plug-and-play component.
 *
 * Typical usage:
 *   vfio_ctx_t *ctx = NULL;
 *   vfio_load_vfio_driver("0000:01:00.0");
 *   vfio_open(&ctx, "0000:01:00.0");
 *   // ... use DMA, MSI, BAR via ctx ...
 *   vfio_close(ctx);
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
#define VFIO_OK             0
#define VFIO_ERR_INVAL     -1   /* Invalid argument */
#define VFIO_ERR_OPEN      -2   /* Failed to open file/device */
#define VFIO_ERR_IOCTL     -3   /* ioctl call failed */
#define VFIO_ERR_MMAP      -4   /* mmap failed */
#define VFIO_ERR_ALLOC     -5   /* Memory allocation failed */
#define VFIO_ERR_PERM      -6   /* Permission denied */
#define VFIO_ERR_NOGROUP   -7   /* IOMMU group not found */
#define VFIO_ERR_NOTVIABLE -8   /* Group not viable */
#define VFIO_ERR_BUSY      -9   /* Resource busy */
#define VFIO_ERR_NOSYS     -10  /* Not supported */

/* ================================================================
 *  Constants
 * ================================================================ */

/** Maximum BDF string length: "XXXX:XX:XX.X\0" */
#define VFIO_BDF_MAX_LEN       14

/** Maximum number of MSI/MSI-X vectors managed per device. */
#define VFIO_MAX_MSI_VECTORS   32

/** VFIO backend mode: legacy container/group (pre-6.8 kernel) */
#define VFIO_MODE_LEGACY       0

/** VFIO backend mode: iommufd + device cdev (kernel >= 6.8) */
#define VFIO_MODE_IOMMUFD      1

/* ================================================================
 *  Type Definitions
 * ================================================================ */

/** VFIO container - wraps /dev/vfio/vfio file descriptor (legacy). */
typedef struct vfio_container {
    int fd;       /* File descriptor for /dev/vfio/vfio, -1 if closed */
} vfio_container_t;

/** VFIO group - wraps /dev/vfio/<group_id> file descriptor (legacy). */
typedef struct vfio_group {
    int fd;             /* File descriptor for /dev/vfio/<group_id>, -1 if closed */
    int group_id;       /* IOMMU group number */
    int container_fd;   /* Associated container fd */
} vfio_group_t;

/** iommufd context - wraps /dev/iommu fd and IOAS state (new VFIO). */
typedef struct vfio_iommufd {
    int      fd;        /* /dev/iommu fd, -1 if closed */
    uint32_t ioas_id;   /* IOAS object ID allocated by iommufd */
    uint32_t dev_id;    /* Device ID returned by VFIO_DEVICE_BIND_IOMMUFD */
} vfio_iommufd_t;

/** VFIO device - wraps a device file descriptor. */
typedef struct vfio_device {
    int fd;                        /* Device file descriptor, -1 if closed */
    char bdf[VFIO_BDF_MAX_LEN];  /* PCI BDF address (e.g. "0000:01:00.0") */
    uint32_t num_regions;          /* Number of BAR regions */
    uint32_t num_irqs;             /* Number of IRQ types */
    uint32_t flags;                /* Device capability flags */
} vfio_device_t;

/** Mapped BAR region for MMIO access (mmap-based, legacy). */
typedef struct vfio_region {
    void    *addr;        /* mmap'd virtual address (NULL if not mapped) */
    uint64_t size;        /* Region size in bytes */
    uint64_t offset;      /* Region offset */
    uint32_t index;       /* Region index (BAR number) */
    uint32_t flags;       /* Region capability flags */
    int      device_fd;   /* Associated device fd */
} vfio_region_t;

/**
 * BAR descriptor for region-based access (pread/pwrite, no mmap).
 *
 * Cached region info so typed BAR accessors avoid querying region
 * info on every access. Initialize with vfio_bar_init().
 */
typedef struct vfio_bar {
    uint64_t offset;      /* Region offset in device fd space */
    uint64_t size;        /* Region size in bytes */
    uint32_t index;       /* BAR index (0-5) */
    uint32_t flags;       /* Region capability flags */
    int      device_fd;   /* Associated device fd */
} vfio_bar_t;

/** DMA mapping descriptor. */
typedef struct vfio_dma {
    void    *vaddr;       /* Userspace virtual address */
    uint64_t iova;        /* IO virtual address (device-visible) */
    uint64_t size;        /* Mapping size in bytes */
} vfio_dma_t;

/** MSI configuration information read from PCI config space. */
typedef struct vfio_msi_config {
    uint32_t addr_low;    /* Message Address (lower 32 bits) */
    uint32_t addr_high;   /* Message Upper Address (upper 32 bits, 0 if 32-bit MSI) */
    uint16_t raw_data;    /* Message Data */
    uint16_t control;     /* Message Control register */
} vfio_msi_config_t;

/* ================================================================
 *  Context / Handle
 *
 *  The context encapsulates ALL resources for a single VFIO device.
 *  Create with vfio_open(), destroy with vfio_close().
 *  Pass as the first argument to all high-level API functions.
 *
 *  Supports both legacy (container/group) and new (iommufd/cdev)
 *  VFIO backends. The backend is selected automatically at open time:
 *  iommufd is tried first, legacy is used as fallback.
 *
 *  Design: embed this in your project's global handle for plug-and-play:
 *    struct my_device_handle {
 *        vfio_ctx_t *vfio;
 *        // ... other resources ...
 *    };
 * ================================================================ */

/** MSI vector state - one per enabled interrupt vector. */
typedef struct vfio_msi_vector {
    int event_fd;   /* eventfd for this vector, -1 if unused */
} vfio_msi_vector_t;

/** Full device lifecycle context. */
typedef struct vfio_ctx {
    /* ---- Device Identification ---- */
    char bdf[VFIO_BDF_MAX_LEN];   /* PCI BDF address */
    int  iommu_group_id;            /* IOMMU group number (legacy mode) */

    /* ---- Backend Mode ---- */
    int mode;                       /* VFIO_MODE_LEGACY or VFIO_MODE_IOMMUFD */

    /* ---- Legacy VFIO Resources (container/group) ---- */
    vfio_container_t container;    /* VFIO container (/dev/vfio/vfio) */
    vfio_group_t     group;        /* VFIO IOMMU group */

    /* ---- New VFIO Resources (iommufd/cdev) ---- */
    vfio_iommufd_t   iommufd;     /* iommufd context */

    /* ---- Device (shared between both modes) ---- */
    vfio_device_t    device;       /* VFIO device */

    /* ---- MSI Interrupt State ---- */
    vfio_msi_vector_t msi_vectors[VFIO_MAX_MSI_VECTORS];
    uint32_t           msi_count;   /* Number of enabled MSI vectors */
    int                msi_enabled; /* Non-zero if MSI is active */

    /* ---- Lifecycle State ---- */
    int initialized;                /* Non-zero after successful vfio_open() */
} vfio_ctx_t;

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
 * vfio-pci using the driver_override mechanism. Idempotent: safe to call
 * multiple times - if the device is already bound to vfio-pci, returns
 * VFIO_OK immediately.
 *
 * @param bdf  PCI BDF address string (e.g. "0000:01:00.0").
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_load_vfio_driver(const char *bdf);

/**
 * Open and initialize a VFIO device context.
 *
 * Performs all setup in one call: opens container, finds and opens the
 * IOMMU group, sets up IOMMU (TYPE1v2), and opens the device. On success,
 * *ctx points to a fully initialized context ready for DMA/MSI operations.
 *
 * Idempotent: if *ctx is already initialized, returns VFIO_OK without
 * re-initializing. Pass a NULL-initialized pointer on first call.
 *
 * @param ctx  Pointer to context pointer. Set to allocated context on success.
 * @param bdf  PCI BDF address string (e.g. "0000:01:00.0").
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_open(vfio_ctx_t **ctx, const char *bdf);

/**
 * Close and destroy a VFIO device context.
 *
 * Releases all resources: disables MSI if active, closes device/group/
 * container file descriptors, and frees the context memory.
 * Safe to call with NULL or already-closed context.
 *
 * @param ctx  Context to close (may be NULL).
 */
void vfio_close(vfio_ctx_t *ctx);

/**
 * Enable MSI interrupts for the device.
 *
 * Creates eventfds for each requested vector and associates them with
 * the device's MSI IRQ. The eventfds are stored in ctx->msi_vectors[]
 * for use with vfio_handle_interrupt().
 *
 * @param ctx          Initialized device context.
 * @param num_vectors  Number of MSI vectors to enable (1..VFIO_MAX_MSI_VECTORS).
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_msi_enable(vfio_ctx_t *ctx, uint32_t num_vectors);

/**
 * Disable MSI interrupts for the device.
 *
 * Disables the MSI IRQ and closes all associated eventfds.
 * Safe to call when MSI is not enabled (returns VFIO_OK).
 *
 * @param ctx  Initialized device context.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_msi_disable(vfio_ctx_t *ctx);

/**
 * Wait for an MSI interrupt on a specific vector.
 *
 * Blocks until the device triggers the interrupt associated with the
 * given vector number. The vector must have been enabled via vfio_msi_enable().
 *
 * @param ctx     Initialized device context with MSI enabled.
 * @param vector  Vector index (0-based, must be < msi_count).
 * @return VFIO_OK on interrupt received, negative error code on failure.
 */
int vfio_handle_interrupt(vfio_ctx_t *ctx, uint32_t vector);

/**
 * Map external user memory for DMA (IOMMU mapping only).
 *
 * Creates an IOMMU mapping for caller-owned memory so the device can
 * access it via the specified IOVA. Does NOT allocate memory.
 * The caller must fill dma->vaddr and dma->size before calling.
 * On success, dma->iova is set to the provided iova.
 * Use vfio_dma_unmap() to remove the mapping.
 *
 * @param ctx    Initialized device context.
 * @param dma    DMA descriptor with vaddr and size pre-filled (iova set on success).
 * @param iova   IO virtual address for device access.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_dma_map(vfio_ctx_t *ctx, vfio_dma_t *dma, uint64_t iova);

/**
 * Unmap a DMA mapping (IOMMU unmapping only).
 *
 * Removes the IOMMU mapping created by vfio_dma_map(). Does NOT free
 * the underlying memory (the caller owns it).
 *
 * @param ctx  Initialized device context.
 * @param dma  DMA descriptor to unmap.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_dma_unmap(vfio_ctx_t *ctx, vfio_dma_t *dma);

/**
 * Allocate memory and create a DMA mapping.
 *
 * Allocates a page-aligned buffer via mmap and creates an IOMMU mapping.
 * Use vfio_dma_free_unmap() to release both the mapping and the memory.
 *
 * @param ctx   Initialized device context.
 * @param dma   DMA descriptor to fill (output).
 * @param size  Buffer size in bytes (will be page-aligned).
 * @param iova  IO virtual address for device access.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_dma_alloc_map(vfio_ctx_t *ctx, vfio_dma_t *dma,
                        uint64_t size, uint64_t iova);

/**
 * Unmap a DMA mapping and free the allocated memory.
 *
 * Removes the IOMMU mapping and frees the memory allocated by
 * vfio_dma_alloc_map(). Do NOT use this for mappings created
 * with vfio_dma_map() (use vfio_dma_unmap() instead).
 *
 * @param ctx  Initialized device context.
 * @param dma  DMA descriptor to free and unmap.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_dma_free_unmap(vfio_ctx_t *ctx, vfio_dma_t *dma);

/**
 * Get MSI configuration information from PCI config space.
 *
 * Reads the MSI capability structure from the device's sysfs PCI config
 * file (/sys/bus/pci/devices/<bdf>/config) and returns the message
 * address (low/high) and data fields. This reads from sysfs rather than
 * the VFIO config region because VFIO may not return valid MSI data
 * after taking over the device.
 *
 * Requires MSI to be enabled via vfio_msi_enable() first.
 *
 * @param ctx     Initialized device context with MSI enabled.
 * @param config  Output structure to receive MSI config data.
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_msi_get_config(vfio_ctx_t *ctx, vfio_msi_config_t *config);

/* ================================================================
 *  BAR Region-Based Access API
 *
 *  These functions access BAR registers via VFIO region pread/pwrite
 *  rather than mmap. This works with all VFIO backends and does not
 *  require the region to support VFIO_REGION_INFO_FLAG_MMAP.
 *
 *  Initialize a vfio_bar_t with vfio_bar_init(), then use the typed
 *  accessors for 8/16/32/64-bit reads and writes.
 * ================================================================ */

/**
 * Initialize a BAR descriptor for region-based access.
 *
 * Queries the VFIO region info for the specified BAR index and caches
 * the offset and size so subsequent accessors avoid repeated ioctls.
 *
 * @param bar     BAR descriptor to initialize (output).
 * @param device  Opened VFIO device.
 * @param index   BAR index (0-5, or VFIO_PCI_BAR0_REGION_INDEX etc.).
 * @return VFIO_OK on success, negative error code on failure.
 */
int vfio_bar_init(vfio_bar_t *bar, vfio_device_t *device, uint32_t index);

/* Typed BAR reads via region pread (not mmap) */
int vfio_bar_read8(vfio_bar_t *bar, uint64_t offset, uint8_t *val);
int vfio_bar_read16(vfio_bar_t *bar, uint64_t offset, uint16_t *val);
int vfio_bar_read32(vfio_bar_t *bar, uint64_t offset, uint32_t *val);
int vfio_bar_read64(vfio_bar_t *bar, uint64_t offset, uint64_t *val);

/* Typed BAR writes via region pwrite (not mmap) */
int vfio_bar_write8(vfio_bar_t *bar, uint64_t offset, uint8_t val);
int vfio_bar_write16(vfio_bar_t *bar, uint64_t offset, uint16_t val);
int vfio_bar_write32(vfio_bar_t *bar, uint64_t offset, uint32_t val);
int vfio_bar_write64(vfio_bar_t *bar, uint64_t offset, uint64_t val);

#ifdef __cplusplus
}
#endif

#endif /* EASY_VFIO_H */
