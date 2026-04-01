/*
 * easy_vfio - BAR region-based access (pread/pwrite, not mmap)
 *
 * Provides typed 8/16/32/64-bit read/write accessors for PCI BAR
 * registers using the VFIO region interface (pread/pwrite on the
 * device fd). This approach works regardless of whether the region
 * supports mmap, and is the preferred method for the new VFIO backend.
 *
 * Usage:
 *   vfio_bar_t bar;
 *   vfio_bar_init(&bar, &device, VFIO_PCI_BAR0_REGION_INDEX);
 *   vfio_bar_read32(&bar, 0x00, &val);
 *   vfio_bar_write32(&bar, 0x04, 0xdeadbeef);
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "vfio_internal.h"

/* ----------------------------------------------------------------
 *  vfio_bar_init - Query and cache BAR region info
 * ---------------------------------------------------------------- */

int vfio_bar_init(vfio_bar_t *bar, vfio_device_t *device, uint32_t index)
{
    struct vfio_region_info reg_info;

    if (!bar || !device || device->fd < 0)
        return VFIO_ERR_INVAL;

    /* Clear the BAR descriptor */
    memset(bar, 0, sizeof(*bar));
    bar->device_fd = -1;

    /* Query region info from the VFIO device */
    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return VFIO_ERR_IOCTL;

    /* Reject zero-size regions */
    if (reg_info.size == 0)
        return VFIO_ERR_INVAL;

    /* Region must support read/write access */
    if (!(reg_info.flags & VFIO_REGION_INFO_FLAG_READ))
        return VFIO_ERR_NOSYS;

    /* Cache the region info for fast typed access */
    bar->offset = reg_info.offset;
    bar->size = reg_info.size;
    bar->index = index;
    bar->flags = reg_info.flags;
    bar->device_fd = device->fd;

    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  Internal helper: validate BAR access parameters
 * ---------------------------------------------------------------- */

static inline int bar_check(vfio_bar_t *bar, uint64_t offset,
                             size_t access_size)
{
    /* BAR must be initialized */
    if (!bar || bar->device_fd < 0)
        return 0;

    /* Access must be within region bounds */
    if (offset + access_size > bar->size)
        return 0;

    /* Enforce natural alignment */
    if (access_size > 1 && (offset & (access_size - 1)) != 0)
        return 0;

    return 1;
}

/* ----------------------------------------------------------------
 *  BAR Read Accessors (via pread on device fd)
 * ---------------------------------------------------------------- */

int vfio_bar_read8(vfio_bar_t *bar, uint64_t offset, uint8_t *val)
{
    if (!val || !bar_check(bar, offset, sizeof(*val)))
        return VFIO_ERR_INVAL;

    /* Read 1 byte at region_offset + user_offset */
    if (pread(bar->device_fd, val, sizeof(*val),
              bar->offset + offset) != sizeof(*val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_read16(vfio_bar_t *bar, uint64_t offset, uint16_t *val)
{
    if (!val || !bar_check(bar, offset, sizeof(*val)))
        return VFIO_ERR_INVAL;

    /* Read 2 bytes at region_offset + user_offset */
    if (pread(bar->device_fd, val, sizeof(*val),
              bar->offset + offset) != sizeof(*val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_read32(vfio_bar_t *bar, uint64_t offset, uint32_t *val)
{
    if (!val || !bar_check(bar, offset, sizeof(*val)))
        return VFIO_ERR_INVAL;

    /* Read 4 bytes at region_offset + user_offset */
    if (pread(bar->device_fd, val, sizeof(*val),
              bar->offset + offset) != sizeof(*val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_read64(vfio_bar_t *bar, uint64_t offset, uint64_t *val)
{
    if (!val || !bar_check(bar, offset, sizeof(*val)))
        return VFIO_ERR_INVAL;

    /* Read 8 bytes at region_offset + user_offset */
    if (pread(bar->device_fd, val, sizeof(*val),
              bar->offset + offset) != sizeof(*val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  BAR Write Accessors (via pwrite on device fd)
 * ---------------------------------------------------------------- */

int vfio_bar_write8(vfio_bar_t *bar, uint64_t offset, uint8_t val)
{
    if (!bar_check(bar, offset, sizeof(val)))
        return VFIO_ERR_INVAL;

    /* Write 1 byte at region_offset + user_offset */
    if (pwrite(bar->device_fd, &val, sizeof(val),
               bar->offset + offset) != sizeof(val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_write16(vfio_bar_t *bar, uint64_t offset, uint16_t val)
{
    if (!bar_check(bar, offset, sizeof(val)))
        return VFIO_ERR_INVAL;

    /* Write 2 bytes at region_offset + user_offset */
    if (pwrite(bar->device_fd, &val, sizeof(val),
               bar->offset + offset) != sizeof(val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_write32(vfio_bar_t *bar, uint64_t offset, uint32_t val)
{
    if (!bar_check(bar, offset, sizeof(val)))
        return VFIO_ERR_INVAL;

    /* Write 4 bytes at region_offset + user_offset */
    if (pwrite(bar->device_fd, &val, sizeof(val),
               bar->offset + offset) != sizeof(val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_bar_write64(vfio_bar_t *bar, uint64_t offset, uint64_t val)
{
    if (!bar_check(bar, offset, sizeof(val)))
        return VFIO_ERR_INVAL;

    /* Write 8 bytes at region_offset + user_offset */
    if (pwrite(bar->device_fd, &val, sizeof(val),
               bar->offset + offset) != sizeof(val))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}
