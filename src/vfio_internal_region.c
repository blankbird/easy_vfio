/*
 * easy_vfio - BAR region mapping and MMIO access
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

#include "vfio_internal.h"

int vfio_region_map(vfio_region_t *region, vfio_device_t *device,
                     uint32_t index)
{
    struct vfio_region_info reg_info;
    void *addr;

    if (!region || !device || device->fd < 0)
        return VFIO_ERR_INVAL;

    memset(region, 0, sizeof(*region));
    region->device_fd = -1;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return VFIO_ERR_IOCTL;

    if (reg_info.size == 0)
        return VFIO_ERR_INVAL;

    if (!(reg_info.flags & VFIO_REGION_INFO_FLAG_MMAP))
        return VFIO_ERR_NOSYS;

    addr = mmap(NULL, reg_info.size, PROT_READ | PROT_WRITE,
                MAP_SHARED, device->fd, reg_info.offset);
    if (addr == MAP_FAILED)
        return VFIO_ERR_MMAP;

    region->addr = addr;
    region->size = reg_info.size;
    region->offset = reg_info.offset;
    region->index = index;
    region->flags = reg_info.flags;
    region->device_fd = device->fd;

    return VFIO_OK;
}

void vfio_region_unmap(vfio_region_t *region)
{
    if (!region || !region->addr)
        return;

    munmap(region->addr, region->size);
    region->addr = NULL;
    region->size = 0;
}

ssize_t vfio_region_read(vfio_device_t *device, uint32_t index,
                          void *buf, size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return VFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return VFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return VFIO_ERR_INVAL;

    ret = pread(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return VFIO_ERR_IOCTL;

    return ret;
}

ssize_t vfio_region_write(vfio_device_t *device, uint32_t index,
                           const void *buf, size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return VFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return VFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return VFIO_ERR_INVAL;

    ret = pwrite(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return VFIO_ERR_IOCTL;

    return ret;
}

/* Validate region pointer, bounds, and alignment for MMIO access */
static inline int mmio_check(vfio_region_t *region, uint64_t offset,
                             size_t access_size)
{
    if (!region || !region->addr)
        return 0;
    if (offset + access_size > region->size)
        return 0;
    if (access_size > 1 && (offset & (access_size - 1)) != 0)
        return 0;
    return 1;
}

/* Typed MMIO accessors for mapped regions */

uint8_t vfio_mmio_read8(vfio_region_t *region, uint64_t offset)
{
    if (!mmio_check(region, offset, sizeof(uint8_t)))
        return 0;
    volatile uint8_t *p = (volatile uint8_t *)((char *)region->addr + offset);
    return *p;
}

uint16_t vfio_mmio_read16(vfio_region_t *region, uint64_t offset)
{
    if (!mmio_check(region, offset, sizeof(uint16_t)))
        return 0;
    volatile uint16_t *p = (volatile uint16_t *)((char *)region->addr + offset);
    return *p;
}

uint32_t vfio_mmio_read32(vfio_region_t *region, uint64_t offset)
{
    if (!mmio_check(region, offset, sizeof(uint32_t)))
        return 0;
    volatile uint32_t *p = (volatile uint32_t *)((char *)region->addr + offset);
    return *p;
}

uint64_t vfio_mmio_read64(vfio_region_t *region, uint64_t offset)
{
    if (!mmio_check(region, offset, sizeof(uint64_t)))
        return 0;
    volatile uint64_t *p = (volatile uint64_t *)((char *)region->addr + offset);
    return *p;
}

void vfio_mmio_write8(vfio_region_t *region, uint64_t offset, uint8_t val)
{
    if (!mmio_check(region, offset, sizeof(uint8_t)))
        return;
    volatile uint8_t *p = (volatile uint8_t *)((char *)region->addr + offset);
    *p = val;
}

void vfio_mmio_write16(vfio_region_t *region, uint64_t offset, uint16_t val)
{
    if (!mmio_check(region, offset, sizeof(uint16_t)))
        return;
    volatile uint16_t *p = (volatile uint16_t *)((char *)region->addr + offset);
    *p = val;
}

void vfio_mmio_write32(vfio_region_t *region, uint64_t offset, uint32_t val)
{
    if (!mmio_check(region, offset, sizeof(uint32_t)))
        return;
    volatile uint32_t *p = (volatile uint32_t *)((char *)region->addr + offset);
    *p = val;
}

void vfio_mmio_write64(vfio_region_t *region, uint64_t offset, uint64_t val)
{
    if (!mmio_check(region, offset, sizeof(uint64_t)))
        return;
    volatile uint64_t *p = (volatile uint64_t *)((char *)region->addr + offset);
    *p = val;
}
