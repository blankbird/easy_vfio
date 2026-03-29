/*
 * easy_vfio - Interrupt handling
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "easy_vfio.h"

int evfio_irq_enable(evfio_device_t *device, uint32_t irq_index,
                     int *fds, uint32_t count)
{
    struct vfio_irq_set *irq_set;
    size_t irq_set_size;
    int ret;

    if (!device || device->fd < 0 || !fds || count == 0)
        return EVFIO_ERR_INVAL;

    irq_set_size = sizeof(*irq_set) + (count * sizeof(int32_t));
    irq_set = calloc(1, irq_set_size);
    if (!irq_set)
        return EVFIO_ERR_ALLOC;

    irq_set->argsz = (uint32_t)irq_set_size;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = irq_index;
    irq_set->start = 0;
    irq_set->count = count;

    memcpy(irq_set->data, fds, count * sizeof(int32_t));

    ret = ioctl(device->fd, VFIO_DEVICE_SET_IRQS, irq_set);
    free(irq_set);

    if (ret < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

int evfio_irq_disable(evfio_device_t *device, uint32_t irq_index)
{
    struct vfio_irq_set irq_set;

    if (!device || device->fd < 0)
        return EVFIO_ERR_INVAL;

    memset(&irq_set, 0, sizeof(irq_set));
    irq_set.argsz = sizeof(irq_set);
    irq_set.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set.index = irq_index;
    irq_set.start = 0;
    irq_set.count = 0;

    if (ioctl(device->fd, VFIO_DEVICE_SET_IRQS, &irq_set) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

int evfio_irq_unmask_intx(evfio_device_t *device)
{
    struct vfio_irq_set irq_set;

    if (!device || device->fd < 0)
        return EVFIO_ERR_INVAL;

    memset(&irq_set, 0, sizeof(irq_set));
    irq_set.argsz = sizeof(irq_set);
    irq_set.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK;
    irq_set.index = VFIO_PCI_INTX_IRQ_INDEX;
    irq_set.start = 0;
    irq_set.count = 1;

    if (ioctl(device->fd, VFIO_DEVICE_SET_IRQS, &irq_set) < 0)
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}
