/*
 * easy_vfio - Internal low-level API declarations
 *
 * These functions provide the building blocks for the high-level context API.
 * They are NOT part of the public API and should not be used directly by
 * library consumers unless advanced usage is required.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef VFIO_INTERNAL_H
#define VFIO_INTERNAL_H

#include "easy_vfio.h"

#include <unistd.h>

/* ---------------- Common Helpers (Internal) ---------------- */

/** Align size up to the system page boundary. */
static inline uint64_t vfio_page_align(uint64_t size)
{
    long ps = sysconf(_SC_PAGESIZE);
    uint64_t page_size = (ps > 0) ? (uint64_t)ps : 4096;
    return (size + page_size - 1) & ~(page_size - 1);
}

/* ---------------- Container (Internal, Legacy) ---------------- */

int vfio_container_open(vfio_container_t *container);
void vfio_container_close(vfio_container_t *container);
int vfio_container_set_iommu(vfio_container_t *container, int type);
int vfio_container_check_extension(vfio_container_t *container, int extension);

/* ---------------- Group (Internal, Legacy) ---------------- */

int vfio_group_open(vfio_group_t *group, vfio_container_t *container, int group_id);
void vfio_group_close(vfio_group_t *group);
int vfio_group_is_viable(vfio_group_t *group);

/* ---------------- iommufd (Internal, New VFIO >= 6.8) ---------------- */

/**
 * Open /dev/iommu and initialize iommufd context.
 * Allocates an IOAS for DMA mappings.
 */
int vfio_iommufd_open(vfio_iommufd_t *iommufd);

/** Close iommufd and release resources. */
void vfio_iommufd_close(vfio_iommufd_t *iommufd);

/**
 * Open device cdev found via sysfs for the given BDF.
 * Returns the device fd on success, negative error on failure.
 */
int vfio_iommufd_device_open(const char *bdf);

/**
 * Bind a device fd to iommufd and attach it to the IOAS.
 * On success, iommufd->dev_id is set.
 */
int vfio_iommufd_device_bind(vfio_iommufd_t *iommufd, int device_fd);

/** Attach device to the IOAS within iommufd. */
int vfio_iommufd_device_attach(vfio_iommufd_t *iommufd, int device_fd);

/** Map DMA via iommufd IOAS. */
int vfio_iommufd_dma_map(vfio_iommufd_t *iommufd, void *vaddr,
                          uint64_t size, uint64_t iova);

/** Unmap DMA via iommufd IOAS. */
int vfio_iommufd_dma_unmap(vfio_iommufd_t *iommufd, uint64_t iova,
                            uint64_t size);

/* ---------------- Device (Internal) ---------------- */

int vfio_device_open(vfio_device_t *device, vfio_group_t *group, const char *bdf);

/**
 * Initialize device info from an already-opened device fd.
 * Used by the iommufd path where the fd comes from cdev open.
 */
int vfio_device_init_from_fd(vfio_device_t *device, int fd, const char *bdf);

void vfio_device_close(vfio_device_t *device);
int vfio_device_reset(vfio_device_t *device);
int vfio_device_get_region_info(vfio_device_t *device, uint32_t index, struct vfio_region_info *info);
int vfio_device_get_irq_info(vfio_device_t *device, uint32_t index, struct vfio_irq_info *info);

/* ---------------- Region/BAR (Internal) ---------------- */

int vfio_region_map(vfio_region_t *region, vfio_device_t *device, uint32_t index);
void vfio_region_unmap(vfio_region_t *region);
ssize_t vfio_region_read(vfio_device_t *device, uint32_t index, void *buf, size_t len, uint64_t offset);
ssize_t vfio_region_write(vfio_device_t *device, uint32_t index, const void *buf, size_t len, uint64_t offset);

uint8_t  vfio_mmio_read8(vfio_region_t *region, uint64_t offset);
uint16_t vfio_mmio_read16(vfio_region_t *region, uint64_t offset);
uint32_t vfio_mmio_read32(vfio_region_t *region, uint64_t offset);
uint64_t vfio_mmio_read64(vfio_region_t *region, uint64_t offset);
void vfio_mmio_write8(vfio_region_t *region, uint64_t offset, uint8_t val);
void vfio_mmio_write16(vfio_region_t *region, uint64_t offset, uint16_t val);
void vfio_mmio_write32(vfio_region_t *region, uint64_t offset, uint32_t val);
void vfio_mmio_write64(vfio_region_t *region, uint64_t offset, uint64_t val);

/* ---------------- DMA (Internal) ---------------- */

/*
 * Low-level IOMMU DMA map/unmap via legacy container.
 * These only create/remove the IOMMU mapping, they do NOT allocate or free memory.
 */
int vfio_iommu_dma_map(int container_fd, void *vaddr, uint64_t size, uint64_t iova);
int vfio_iommu_dma_unmap(int container_fd, uint64_t iova, uint64_t size);

/* ---------------- Interrupt (Internal) ---------------- */

int vfio_irq_enable(vfio_device_t *device, uint32_t irq_index, int *fds, uint32_t count);
int vfio_irq_disable(vfio_device_t *device, uint32_t irq_index);
int vfio_irq_unmask_intx(vfio_device_t *device);

/* ---------------- PCI Config (Internal) ---------------- */

ssize_t vfio_pci_config_read(vfio_device_t *device, void *buf, size_t len, uint64_t offset);
ssize_t vfio_pci_config_write(vfio_device_t *device, const void *buf, size_t len, uint64_t offset);

/* ---------------- Utility (Internal) ---------------- */

int vfio_get_iommu_group(const char *bdf);
int vfio_bind_device(const char *bdf);
int vfio_unbind_device(const char *bdf);
int vfio_bdf_valid(const char *bdf);
int vfio_pci_get_ids(const char *bdf, uint16_t *vendor_id, uint16_t *device_id);
const char *vfio_strerror(int err);

/*
 * Check if device is currently bound to vfio-pci driver.
 * Returns 1 if bound to vfio-pci, 0 otherwise (including invalid BDF).
 */
int vfio_is_bound_to_vfio(const char *bdf);

#endif /* VFIO_INTERNAL_H */
