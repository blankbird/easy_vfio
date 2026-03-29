/*
 * easy_vfio - VFIO container management
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#include "easy_vfio.h"

int evfio_container_open(evfio_container_t *container)
{
    int fd;
    int api_version;

    if (!container)
        return EVFIO_ERR_INVAL;

    memset(container, 0, sizeof(*container));
    container->fd = -1;

    fd = open("/dev/vfio/vfio", O_RDWR);
    if (fd < 0)
        return EVFIO_ERR_OPEN;

    api_version = ioctl(fd, VFIO_GET_API_VERSION);
    if (api_version != VFIO_API_VERSION) {
        close(fd);
        return EVFIO_ERR_NOSYS;
    }

    container->fd = fd;
    return EVFIO_OK;
}

void evfio_container_close(evfio_container_t *container)
{
    if (!container)
        return;
    if (container->fd >= 0) {
        close(container->fd);
        container->fd = -1;
    }
}

int evfio_container_set_iommu(evfio_container_t *container, int type)
{
    if (!container || container->fd < 0)
        return EVFIO_ERR_INVAL;

    if (ioctl(container->fd, VFIO_SET_IOMMU, type) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

int evfio_container_check_extension(evfio_container_t *container, int extension)
{
    int ret;

    if (!container || container->fd < 0)
        return EVFIO_ERR_INVAL;

    ret = ioctl(container->fd, VFIO_CHECK_EXTENSION, extension);
    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return ret;
}
