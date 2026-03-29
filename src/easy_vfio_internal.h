/*
 * easy_vfio - Internal low-level API declarations
 *
 * These functions provide the building blocks for the high-level context API.
 * They are NOT part of the public API and should not be used directly by
 * library consumers unless advanced usage is required.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef EASY_VFIO_INTERNAL_H
#define EASY_VFIO_INTERNAL_H

#include "easy_vfio.h"

/* ---------------- Container (Internal) ---------------- */

int evfio_container_open(evfio_container_t *container);
void evfio_container_close(evfio_container_t *container);
int evfio_container_set_iommu(evfio_container_t *container, int type);
int evfio_container_check_extension(evfio_container_t *container, int extension);

/* ---------------- Group (Internal) ---------------- */

int evfio_group_open(evfio_group_t *group, evfio_container_t *container, int group_id);
void evfio_group_close(evfio_group_t *group);
int evfio_group_is_viable(evfio_group_t *group);

/* ---------------- Device (Internal) ---------------- */

int evfio_device_open(evfio_device_t *device, evfio_group_t *group, const char *bdf);
void evfio_device_close(evfio_device_t *device);
int evfio_device_reset(evfio_device_t *device);
int evfio_device_get_region_info(evfio_device_t *device, uint32_t index, struct vfio_region_info *info);
int evfio_device_get_irq_info(evfio_device_t *device, uint32_t index, struct vfio_irq_info *info);

/* ---------------- Region/BAR (Internal) ---------------- */

int evfio_region_map(evfio_region_t *region, evfio_device_t *device, uint32_t index);
void evfio_region_unmap(evfio_region_t *region);
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

/* ---------------- DMA (Internal) ---------------- */

/*
 * Low-level IOMMU DMA map/unmap. These only create/remove the IOMMU mapping,
 * they do NOT allocate or free memory.
 */
int evfio_iommu_dma_map(int container_fd, void *vaddr, uint64_t size, uint64_t iova);
int evfio_iommu_dma_unmap(int container_fd, uint64_t iova, uint64_t size);

/* ---------------- Interrupt (Internal) ---------------- */

int evfio_irq_enable(evfio_device_t *device, uint32_t irq_index, int *fds, uint32_t count);
int evfio_irq_disable(evfio_device_t *device, uint32_t irq_index);
int evfio_irq_unmask_intx(evfio_device_t *device);

/* ---------------- PCI Config (Internal) ---------------- */

ssize_t evfio_pci_config_read(evfio_device_t *device, void *buf, size_t len, uint64_t offset);
ssize_t evfio_pci_config_write(evfio_device_t *device, const void *buf, size_t len, uint64_t offset);

/* ---------------- Utility (Internal) ---------------- */

int evfio_get_iommu_group(const char *bdf);
int evfio_bind_device(const char *bdf);
int evfio_unbind_device(const char *bdf);
int evfio_bdf_valid(const char *bdf);
int evfio_pci_get_ids(const char *bdf, uint16_t *vendor_id, uint16_t *device_id);
const char *evfio_strerror(int err);

/* Check if device is currently bound to vfio-pci driver */
int evfio_is_bound_to_vfio(const char *bdf);

#endif /* EASY_VFIO_INTERNAL_H */
