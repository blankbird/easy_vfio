/*
 * easy_vfio - A simple C library for VFIO-based PCIe device management
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef EASY_VFIO_H
#define EASY_VFIO_H

#include <stdint.h>
#include <stddef.h>
#include <linux/vfio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Error codes ---------------- */
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

/* Maximum BDF string length: "XXXX:XX:XX.X\0" */
#define EVFIO_BDF_MAX_LEN   14

/* ---------------- Types ---------------- */

/**
 * VFIO container - wraps /dev/vfio/vfio file descriptor.
 */
typedef struct evfio_container {
    int fd;       /* File descriptor for /dev/vfio/vfio */
} evfio_container_t;

/**
 * VFIO group - wraps /dev/vfio/<group_id> file descriptor.
 */
typedef struct evfio_group {
    int fd;             /* File descriptor for /dev/vfio/<group_id> */
    int group_id;       /* IOMMU group number */
    int container_fd;   /* Associated container fd */
} evfio_group_t;

/**
 * VFIO device - wraps a device file descriptor obtained from a group.
 */
typedef struct evfio_device {
    int fd;                        /* Device file descriptor */
    char bdf[EVFIO_BDF_MAX_LEN];  /* PCI BDF address (e.g. "0000:01:00.0") */
    uint32_t num_regions;          /* Number of regions */
    uint32_t num_irqs;             /* Number of IRQ types */
    uint32_t flags;                /* Device flags */
} evfio_device_t;

/**
 * Mapped BAR region for MMIO access.
 */
typedef struct evfio_region {
    void    *addr;        /* mmap'd virtual address (NULL if not mapped) */
    uint64_t size;        /* Region size in bytes */
    uint64_t offset;      /* Region offset */
    uint32_t index;       /* Region index (BAR number) */
    uint32_t flags;       /* Region flags */
    int      device_fd;   /* Associated device fd */
} evfio_region_t;

/**
 * DMA mapping descriptor.
 */
typedef struct evfio_dma {
    void    *vaddr;       /* Userspace virtual address */
    uint64_t iova;        /* IO virtual address (device-visible) */
    uint64_t size;        /* Mapping size in bytes */
} evfio_dma_t;

/* ---------------- Container API ---------------- */

/**
 * Open a VFIO container (/dev/vfio/vfio) and verify the API version.
 *
 * @param container  Pointer to container structure to initialize.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_container_open(evfio_container_t *container);

/**
 * Close a VFIO container.
 *
 * @param container  Pointer to an open container.
 */
void evfio_container_close(evfio_container_t *container);

/**
 * Set the IOMMU type for the container.
 * Must be called after at least one group has been attached.
 *
 * @param container  Pointer to an open container.
 * @param type       IOMMU type (e.g. VFIO_TYPE1v2_IOMMU).
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_container_set_iommu(evfio_container_t *container, int type);

/**
 * Check whether a given extension is supported by the container.
 *
 * @param container  Pointer to an open container.
 * @param extension  Extension ID to check.
 * @return 1 if supported, 0 if not, negative error code on failure.
 */
int evfio_container_check_extension(evfio_container_t *container, int extension);

/* ---------------- Group API ---------------- */

/**
 * Open a VFIO group and attach it to a container.
 *
 * @param group        Pointer to group structure to initialize.
 * @param container    Pointer to an open container.
 * @param group_id     IOMMU group number.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_group_open(evfio_group_t *group, evfio_container_t *container,
                     int group_id);

/**
 * Close a VFIO group.
 *
 * @param group  Pointer to an open group.
 */
void evfio_group_close(evfio_group_t *group);

/**
 * Check if a group is viable (all devices bound to VFIO driver).
 *
 * @param group  Pointer to an open group.
 * @return 1 if viable, 0 if not, negative error code on failure.
 */
int evfio_group_is_viable(evfio_group_t *group);

/* ---------------- Device API ---------------- */

/**
 * Get a device file descriptor from a VFIO group.
 *
 * @param device  Pointer to device structure to initialize.
 * @param group   Pointer to an open group.
 * @param bdf     PCI BDF address string (e.g. "0000:01:00.0").
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_device_open(evfio_device_t *device, evfio_group_t *group,
                      const char *bdf);

/**
 * Close a VFIO device.
 *
 * @param device  Pointer to an open device.
 */
void evfio_device_close(evfio_device_t *device);

/**
 * Reset the device (PCI function level reset).
 *
 * @param device  Pointer to an open device.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_device_reset(evfio_device_t *device);

/**
 * Get information about a specific region.
 *
 * @param device  Pointer to an open device.
 * @param index   Region index (BAR number, or VFIO_PCI_CONFIG_REGION_INDEX).
 * @param info    Pointer to region_info struct (caller must set argsz).
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_device_get_region_info(evfio_device_t *device, uint32_t index,
                                 struct vfio_region_info *info);

/**
 * Get information about a specific IRQ type.
 *
 * @param device  Pointer to an open device.
 * @param index   IRQ index (e.g. VFIO_PCI_INTX_IRQ_INDEX).
 * @param info    Pointer to irq_info struct (caller must set argsz).
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_device_get_irq_info(evfio_device_t *device, uint32_t index,
                              struct vfio_irq_info *info);

/* ---------------- Region (BAR) API ---------------- */

/**
 * Map a device region (BAR) into userspace for MMIO access.
 *
 * @param region  Pointer to region structure to initialize.
 * @param device  Pointer to an open device.
 * @param index   Region index (BAR number).
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_region_map(evfio_region_t *region, evfio_device_t *device,
                     uint32_t index);

/**
 * Unmap a previously mapped region.
 *
 * @param region  Pointer to a mapped region.
 */
void evfio_region_unmap(evfio_region_t *region);

/**
 * Read from a region using PIO (pread) instead of mmap.
 * Useful for regions that don't support mmap.
 *
 * @param device  Pointer to an open device.
 * @param index   Region index.
 * @param buf     Buffer to read into.
 * @param len     Number of bytes to read.
 * @param offset  Offset within the region.
 * @return Number of bytes read on success, negative error code on failure.
 */
ssize_t evfio_region_read(evfio_device_t *device, uint32_t index,
                          void *buf, size_t len, uint64_t offset);

/**
 * Write to a region using PIO (pwrite) instead of mmap.
 *
 * @param device  Pointer to an open device.
 * @param index   Region index.
 * @param buf     Buffer containing data to write.
 * @param len     Number of bytes to write.
 * @param offset  Offset within the region.
 * @return Number of bytes written on success, negative error code on failure.
 */
ssize_t evfio_region_write(evfio_device_t *device, uint32_t index,
                           const void *buf, size_t len, uint64_t offset);

/* Typed MMIO accessors for mapped regions */
uint8_t  evfio_mmio_read8(evfio_region_t *region, uint64_t offset);
uint16_t evfio_mmio_read16(evfio_region_t *region, uint64_t offset);
uint32_t evfio_mmio_read32(evfio_region_t *region, uint64_t offset);
uint64_t evfio_mmio_read64(evfio_region_t *region, uint64_t offset);

void evfio_mmio_write8(evfio_region_t *region, uint64_t offset, uint8_t val);
void evfio_mmio_write16(evfio_region_t *region, uint64_t offset, uint16_t val);
void evfio_mmio_write32(evfio_region_t *region, uint64_t offset, uint32_t val);
void evfio_mmio_write64(evfio_region_t *region, uint64_t offset, uint64_t val);

/* ---------------- DMA API ---------------- */

/**
 * Allocate memory and create a DMA mapping.
 *
 * @param container  Pointer to an open container with IOMMU set.
 * @param dma        Pointer to DMA descriptor to initialize.
 * @param size       Size of the DMA buffer to allocate (page-aligned).
 * @param iova       IO virtual address to map at.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_map(evfio_container_t *container, evfio_dma_t *dma,
                  uint64_t size, uint64_t iova);

/**
 * Remove a DMA mapping and free the associated memory.
 *
 * @param container  Pointer to the container.
 * @param dma        Pointer to the DMA descriptor.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_dma_unmap(evfio_container_t *container, evfio_dma_t *dma);

/* ---------------- Interrupt API ---------------- */

/**
 * Enable interrupts by associating eventfds with an IRQ type.
 *
 * @param device    Pointer to an open device.
 * @param irq_index IRQ index (VFIO_PCI_INTX_IRQ_INDEX, VFIO_PCI_MSI_IRQ_INDEX, etc.)
 * @param fds       Array of eventfd file descriptors.
 * @param count     Number of eventfds.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_irq_enable(evfio_device_t *device, uint32_t irq_index,
                     int *fds, uint32_t count);

/**
 * Disable interrupts for a given IRQ type.
 *
 * @param device    Pointer to an open device.
 * @param irq_index IRQ index to disable.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_irq_disable(evfio_device_t *device, uint32_t irq_index);

/**
 * Unmask an INTx interrupt.
 *
 * @param device  Pointer to an open device.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_irq_unmask_intx(evfio_device_t *device);

/* ---------------- PCI Config Space API ---------------- */

/**
 * Read from PCI configuration space.
 *
 * @param device  Pointer to an open device.
 * @param buf     Buffer to read into.
 * @param len     Number of bytes to read.
 * @param offset  Offset within config space.
 * @return Number of bytes read on success, negative error code on failure.
 */
ssize_t evfio_pci_config_read(evfio_device_t *device, void *buf,
                              size_t len, uint64_t offset);

/**
 * Write to PCI configuration space.
 *
 * @param device  Pointer to an open device.
 * @param buf     Buffer containing data to write.
 * @param len     Number of bytes to write.
 * @param offset  Offset within config space.
 * @return Number of bytes written on success, negative error code on failure.
 */
ssize_t evfio_pci_config_write(evfio_device_t *device, const void *buf,
                               size_t len, uint64_t offset);

/* ---------------- Utility API ---------------- */

/**
 * Look up the IOMMU group number for a PCI device by its BDF address.
 *
 * @param bdf  PCI BDF address string (e.g. "0000:01:00.0").
 * @return Group number on success (>= 0), negative error code on failure.
 */
int evfio_get_iommu_group(const char *bdf);

/**
 * Bind a PCI device to the vfio-pci driver.
 *
 * @param bdf  PCI BDF address string.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_bind_device(const char *bdf);

/**
 * Unbind a PCI device from its current driver.
 *
 * @param bdf  PCI BDF address string.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_unbind_device(const char *bdf);

/**
 * Validate a PCI BDF address string format.
 *
 * @param bdf  PCI BDF address string.
 * @return 1 if valid, 0 if invalid.
 */
int evfio_bdf_valid(const char *bdf);

/**
 * Read the vendor and device IDs for a PCI device.
 *
 * @param bdf       PCI BDF address string.
 * @param vendor_id Output: vendor ID.
 * @param device_id Output: device ID.
 * @return EVFIO_OK on success, negative error code on failure.
 */
int evfio_pci_get_ids(const char *bdf, uint16_t *vendor_id,
                      uint16_t *device_id);

/**
 * Return a human-readable error message for an error code.
 *
 * @param err  Error code (EVFIO_ERR_*).
 * @return Static string describing the error.
 */
const char *evfio_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* EASY_VFIO_H */
