/*
 * easy_vfio - PCI config space access
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#include "easy_vfio.h"

ssize_t evfio_pci_config_read(evfio_device_t *device, void *buf,
                              size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return EVFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = VFIO_PCI_CONFIG_REGION_INDEX;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return EVFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return EVFIO_ERR_INVAL;

    ret = pread(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return ret;
}

ssize_t evfio_pci_config_write(evfio_device_t *device, const void *buf,
                               size_t len, uint64_t offset)
{
    struct vfio_region_info reg_info;
    ssize_t ret;

    if (!device || device->fd < 0 || !buf || len == 0)
        return EVFIO_ERR_INVAL;

    memset(&reg_info, 0, sizeof(reg_info));
    reg_info.argsz = sizeof(reg_info);
    reg_info.index = VFIO_PCI_CONFIG_REGION_INDEX;

    if (ioctl(device->fd, VFIO_DEVICE_GET_REGION_INFO, &reg_info) < 0)
        return EVFIO_ERR_IOCTL;

    if (offset + len > reg_info.size)
        return EVFIO_ERR_INVAL;

    ret = pwrite(device->fd, buf, len, reg_info.offset + offset);
    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return ret;
}
