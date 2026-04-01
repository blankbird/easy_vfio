/*
 * easy_vfio - iommufd/cdev backend (new VFIO, kernel 6.8+)
 *
 * Implements the new VFIO device access path using iommufd for IOMMU
 * management and device character devices (cdev) for direct device access,
 * bypassing the legacy container/group model.
 *
 * Flow:
 *   1. Open /dev/iommu (iommufd)
 *   2. Allocate an IOAS (I/O Address Space)
 *   3. Open device cdev from /dev/vfio/devices/vfioX
 *   4. Bind device to iommufd (VFIO_DEVICE_BIND_IOMMUFD)
 *   5. Attach device to IOAS (VFIO_DEVICE_ATTACH_IOMMUFD_PT)
 *   6. DMA map/unmap via iommufd IOAS ioctls
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include <linux/iommufd.h>

#include "vfio_internal.h"

/* ----------------------------------------------------------------
 *  vfio_iommufd_open - Open /dev/iommu and allocate an IOAS
 * ---------------------------------------------------------------- */

int vfio_iommufd_open(vfio_iommufd_t *iommufd)
{
    int fd;
    struct iommu_ioas_alloc alloc;

    if (!iommufd)
        return VFIO_ERR_INVAL;

    /* Clear state */
    memset(iommufd, 0, sizeof(*iommufd));
    iommufd->fd = -1;

    /* Open the iommufd device node */
    fd = open("/dev/iommu", O_RDWR);
    if (fd < 0)
        return VFIO_ERR_OPEN;

    /* Allocate an IOAS for DMA address space management */
    memset(&alloc, 0, sizeof(alloc));
    alloc.size = sizeof(alloc);

    if (ioctl(fd, IOMMU_IOAS_ALLOC, &alloc) < 0) {
        close(fd);
        return VFIO_ERR_IOCTL;
    }

    iommufd->fd = fd;
    iommufd->ioas_id = alloc.out_ioas_id;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_close - Close iommufd and release resources
 * ---------------------------------------------------------------- */

void vfio_iommufd_close(vfio_iommufd_t *iommufd)
{
    if (!iommufd)
        return;

    /* Closing the fd releases all iommufd objects */
    if (iommufd->fd >= 0) {
        close(iommufd->fd);
        iommufd->fd = -1;
    }
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_device_open - Open device cdev via sysfs lookup
 *
 *  Finds the vfio device cdev name from:
 *    /sys/bus/pci/devices/<bdf>/vfio-dev/
 *  Then opens /dev/vfio/devices/<vfioX>
 * ---------------------------------------------------------------- */

int vfio_iommufd_device_open(const char *bdf)
{
    char sysfs_path[PATH_MAX];
    char dev_path[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    int fd;

    if (!bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* Build sysfs path to find the cdev name */
    snprintf(sysfs_path, sizeof(sysfs_path),
             "/sys/bus/pci/devices/%s/vfio-dev", bdf);

    /* Scan directory for the vfioX entry */
    dir = opendir(sysfs_path);
    if (!dir)
        return VFIO_ERR_OPEN;

    fd = -1;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. entries */
        if (entry->d_name[0] == '.')
            continue;

        /* Build the /dev/vfio/devices/vfioX path */
        snprintf(dev_path, sizeof(dev_path),
                 "/dev/vfio/devices/%s", entry->d_name);

        /* Open the device cdev */
        fd = open(dev_path, O_RDWR);
        break;
    }
    closedir(dir);

    if (fd < 0)
        return VFIO_ERR_OPEN;

    return fd;
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_device_bind - Bind device fd to iommufd
 *
 *  Associates the VFIO device with the iommufd instance so that
 *  IOMMU operations can be performed through iommufd.
 * ---------------------------------------------------------------- */

int vfio_iommufd_device_bind(vfio_iommufd_t *iommufd, int device_fd)
{
    struct vfio_device_bind_iommufd bind;

    if (!iommufd || iommufd->fd < 0 || device_fd < 0)
        return VFIO_ERR_INVAL;

    /* Bind the device to iommufd */
    memset(&bind, 0, sizeof(bind));
    bind.argsz = sizeof(bind);
    bind.iommufd = iommufd->fd;

    if (ioctl(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind) < 0)
        return VFIO_ERR_IOCTL;

    /* Save the device ID returned by iommufd */
    iommufd->dev_id = bind.out_devid;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_device_attach - Attach device to the IOAS
 *
 *  Connects the device to the previously allocated IOAS so that
 *  DMA mappings in the IOAS are visible to this device.
 * ---------------------------------------------------------------- */

int vfio_iommufd_device_attach(vfio_iommufd_t *iommufd, int device_fd)
{
    struct vfio_device_attach_iommufd_pt attach;

    if (!iommufd || iommufd->fd < 0 || device_fd < 0)
        return VFIO_ERR_INVAL;

    /* Attach device to the IOAS */
    memset(&attach, 0, sizeof(attach));
    attach.argsz = sizeof(attach);
    attach.pt_id = iommufd->ioas_id;

    if (ioctl(device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach) < 0)
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_dma_map - Map DMA via iommufd IOAS
 *
 *  Creates an IOMMU mapping in the IOAS with fixed IOVA so the
 *  device can access the specified user memory.
 * ---------------------------------------------------------------- */

int vfio_iommufd_dma_map(vfio_iommufd_t *iommufd, void *vaddr,
                          uint64_t size, uint64_t iova)
{
    struct iommu_ioas_map map;

    if (!iommufd || iommufd->fd < 0 || !vaddr || size == 0)
        return VFIO_ERR_INVAL;

    /* Page-align the size */
    size = vfio_page_align(size);

    /* Set up the IOAS mapping with fixed IOVA and RW access */
    memset(&map, 0, sizeof(map));
    map.size = sizeof(map);
    map.ioas_id = iommufd->ioas_id;
    map.flags = IOMMU_IOAS_MAP_FIXED_IOVA |
                IOMMU_IOAS_MAP_WRITEABLE |
                IOMMU_IOAS_MAP_READABLE;
    map.user_va = (uint64_t)(uintptr_t)vaddr;
    map.length = size;
    map.iova = iova;

    if (ioctl(iommufd->fd, IOMMU_IOAS_MAP, &map) < 0)
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_iommufd_dma_unmap - Unmap DMA via iommufd IOAS
 * ---------------------------------------------------------------- */

int vfio_iommufd_dma_unmap(vfio_iommufd_t *iommufd, uint64_t iova,
                            uint64_t size)
{
    struct iommu_ioas_unmap unmap;

    if (!iommufd || iommufd->fd < 0 || size == 0)
        return VFIO_ERR_INVAL;

    /* Remove the IOAS mapping */
    memset(&unmap, 0, sizeof(unmap));
    unmap.size = sizeof(unmap);
    unmap.ioas_id = iommufd->ioas_id;
    unmap.iova = iova;
    unmap.length = size;

    if (ioctl(iommufd->fd, IOMMU_IOAS_UNMAP, &unmap) < 0)
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}
