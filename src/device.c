/*
 * easy_vfio - VFIO device operations
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#include "easy_vfio.h"

int evfio_device_open(evfio_device_t *device, evfio_group_t *group,
                      const char *bdf)
{
    int fd;
    struct vfio_device_info dev_info;

    if (!device || !group || group->fd < 0 || !bdf)
        return EVFIO_ERR_INVAL;

    if (!evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    memset(device, 0, sizeof(*device));
    device->fd = -1;

    fd = ioctl(group->fd, VFIO_GROUP_GET_DEVICE_FD, bdf);
    if (fd < 0)
        return EVFIO_ERR_OPEN;

    /* Get device info */
    memset(&dev_info, 0, sizeof(dev_info));
    dev_info.argsz = sizeof(dev_info);

    if (ioctl(fd, VFIO_DEVICE_GET_INFO, &dev_info) < 0) {
        close(fd);
        return EVFIO_ERR_IOCTL;
    }

    device->fd = fd;
    strncpy(device->bdf, bdf, EVFIO_BDF_MAX_LEN - 1);
    device->bdf[EVFIO_BDF_MAX_LEN - 1] = '\0';
    device->num_regions = dev_info.num_regions;
    device->num_irqs = dev_info.num_irqs;
    device->flags = dev_info.flags;

    return EVFIO_OK;
}

void evfio_device_close(evfio_device_t *device)
{
    if (!device)
        return;
    if (device->fd >= 0) {
        close(device->fd);
        device->fd = -1;
    }
}

int evfio_device_reset(evfio_device_t *device)
{
    if (!device || device->fd < 0)
        return EVFIO_ERR_INVAL;

    if (!(device->flags & VFIO_DEVICE_FLAGS_RESET))
        return EVFIO_ERR_NOSYS;

    if (ioctl(device->fd, VFIO_DEVICE_RESET) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

int evfio_device_get_region_info(evfio_device_t *device, uint32_t index,
                                 struct vfio_region_info *info)
{
    if (!device || device->fd < 0 || !info)
        return EVFIO_ERR_INVAL;

    memset(info, 0, sizeof(*info));
    info->argsz = sizeof(*info);
    info->index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, info) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

int evfio_device_get_irq_info(evfio_device_t *device, uint32_t index,
                              struct vfio_irq_info *info)
{
    if (!device || device->fd < 0 || !info)
        return EVFIO_ERR_INVAL;

    memset(info, 0, sizeof(*info));
    info->argsz = sizeof(*info);
    info->index = index;

    if (ioctl(device->fd, VFIO_DEVICE_GET_IRQ_INFO, info) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}
