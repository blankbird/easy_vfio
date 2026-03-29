/*
 * easy_vfio - VFIO group management
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#include "easy_vfio.h"

int evfio_group_open(evfio_group_t *group, evfio_container_t *container,
                     int group_id)
{
    char path[64];
    int fd;
    struct vfio_group_status group_status;

    if (!group || !container || container->fd < 0 || group_id < 0)
        return EVFIO_ERR_INVAL;

    memset(group, 0, sizeof(*group));
    group->fd = -1;

    snprintf(path, sizeof(path), "/dev/vfio/%d", group_id);

    fd = open(path, O_RDWR);
    if (fd < 0)
        return EVFIO_ERR_OPEN;

    /* Check group is viable */
    memset(&group_status, 0, sizeof(group_status));
    group_status.argsz = sizeof(group_status);

    if (ioctl(fd, VFIO_GROUP_GET_STATUS, &group_status) < 0) {
        close(fd);
        return EVFIO_ERR_IOCTL;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        close(fd);
        return EVFIO_ERR_NOTVIABLE;
    }

    /* Attach group to container */
    if (ioctl(fd, VFIO_GROUP_SET_CONTAINER, &container->fd) < 0) {
        close(fd);
        return EVFIO_ERR_IOCTL;
    }

    group->fd = fd;
    group->group_id = group_id;
    group->container_fd = container->fd;

    return EVFIO_OK;
}

void evfio_group_close(evfio_group_t *group)
{
    if (!group)
        return;
    if (group->fd >= 0) {
        ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER);
        close(group->fd);
        group->fd = -1;
    }
}

int evfio_group_is_viable(evfio_group_t *group)
{
    struct vfio_group_status status;

    if (!group || group->fd < 0)
        return EVFIO_ERR_INVAL;

    memset(&status, 0, sizeof(status));
    status.argsz = sizeof(status);

    if (ioctl(group->fd, VFIO_GROUP_GET_STATUS, &status) < 0)
        return EVFIO_ERR_IOCTL;

    return !!(status.flags & VFIO_GROUP_FLAGS_VIABLE);
}
