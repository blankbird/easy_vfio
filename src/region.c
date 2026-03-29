/*
 * easy_vfio - BAR region mapping and MMIO access
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

#include "easy_vfio.h"

int evfio_region_map(evfio_region_t *region, evfio_device_t *device,
                     uint32_t index)
{
    struct vfio_region_info reg_info;
    void *addr;

    if (!region || !device || device->fd < 0)
        return EVFIO_ERR_INVAL;

    memset(region, 0, sizeof(*region));
    region->device_fd = -1;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return EVFIO_ERR_IOCTL;

    if (reg_info.size == 0)
        return EVFIO_ERR_INVAL;

    if (!(reg_info.flags & VFIO_REGION_INFO_FLAG_MMAP))
        return EVFIO_ERR_NOSYS;

    addr = mmap(NULL, reg_info.size, PROT_READ | PROT_WRITE,
                MAP_SHARED, device->fd, reg_info.offset);
    if (addr == MAP_FAILED)
        return EVFIO_ERR_MMAP;

    region->addr = addr;
    region->size = reg_info.size;
    region->offset = reg_info.offset;
    region->index = index;
    region->flags = reg_info.flags;
    region->device_fd = device->fd;

    return EVFIO_OK;
}

void evfio_region_unmap(evfio_region_t *region)
{
    if (!region || !region->addr)
        return;

    munmap(region->addr, region->size);
    region->addr = NULL;
    region->size = 0;
}

ssize_t evfio_region_read(evfio_device_t *device, uint32_t index,
                          void *buf, size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return EVFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return EVFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return EVFIO_ERR_INVAL;

    ret = pread(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return ret;
}

ssize_t evfio_region_write(evfio_device_t *device, uint32_t index,
                           const void *buf, size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return EVFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return EVFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return EVFIO_ERR_INVAL;

    ret = pwrite(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return ret;
}

/* Typed MMIO accessors for mapped regions */

uint8_t evfio_mmio_read8(evfio_region_t *region, uint64_t offset)
{
    volatile uint8_t *p = (volatile uint8_t *)((char *)region->addr + offset);
    return *p;
}

uint16_t evfio_mmio_read16(evfio_region_t *region, uint64_t offset)
{
    volatile uint16_t *p = (volatile uint16_t *)((char *)region->addr + offset);
    return *p;
}

uint32_t evfio_mmio_read32(evfio_region_t *region, uint64_t offset)
{
    volatile uint32_t *p = (volatile uint32_t *)((char *)region->addr + offset);
    return *p;
}

uint64_t evfio_mmio_read64(evfio_region_t *region, uint64_t offset)
{
    volatile uint64_t *p = (volatile uint64_t *)((char *)region->addr + offset);
    return *p;
}

void evfio_mmio_write8(evfio_region_t *region, uint64_t offset, uint8_t val)
{
    volatile uint8_t *p = (volatile uint8_t *)((char *)region->addr + offset);
    *p = val;
}

void evfio_mmio_write16(evfio_region_t *region, uint64_t offset, uint16_t val)
{
    volatile uint16_t *p = (volatile uint16_t *)((char *)region->addr + offset);
    *p = val;
}

void evfio_mmio_write32(evfio_region_t *region, uint64_t offset, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)((char *)region->addr + offset);
    *p = val;
}

void evfio_mmio_write64(evfio_region_t *region, uint64_t offset, uint64_t val)
{
    volatile uint64_t *p = (volatile uint64_t *)((char *)region->addr + offset);
    *p = val;
}
