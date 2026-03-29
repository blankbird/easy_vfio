/*
 * easy_vfio - DMA memory management
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "easy_vfio.h"

int evfio_dma_map(evfio_container_t *container, evfio_dma_t *dma,
                  uint64_t size, uint64_t iova)
{
    struct vfio_iommu_type1_dma_map dma_map;
    void *vaddr;
    long page_size;

    if (!container || container->fd < 0 || !dma || size == 0)
        return EVFIO_ERR_INVAL;

    memset(dma, 0, sizeof(*dma));

    /* Align size to page boundary */
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    size = (size + (uint64_t)page_size - 1) & ~((uint64_t)page_size - 1);

    /* Allocate page-aligned memory */
    vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (vaddr == MAP_FAILED)
        return EVFIO_ERR_ALLOC;

    /* Create IOMMU mapping */
    memset(&dma_map, 0, sizeof(dma_map));
    dma_map.argsz = sizeof(dma_map);
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
    dma_map.vaddr = (uint64_t)(uintptr_t)vaddr;
    dma_map.iova = iova;
    dma_map.size = size;

    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &dma_map) < 0) {
        munmap(vaddr, size);
        return EVFIO_ERR_IOCTL;
    }

    dma->vaddr = vaddr;
    dma->iova = iova;
    dma->size = size;

    return EVFIO_OK;
}

int evfio_dma_unmap(evfio_container_t *container, evfio_dma_t *dma)
{
    struct vfio_iommu_type1_dma_unmap dma_unmap;

    if (!container || container->fd < 0 || !dma || !dma->vaddr)
        return EVFIO_ERR_INVAL;

    memset(&dma_unmap, 0, sizeof(dma_unmap));
    dma_unmap.argsz = sizeof(dma_unmap);
    dma_unmap.iova = dma->iova;
    dma_unmap.size = dma->size;

    if (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap) < 0)
        return EVFIO_ERR_IOCTL;

    munmap(dma->vaddr, dma->size);
    dma->vaddr = NULL;
    dma->iova = 0;
    dma->size = 0;

    return EVFIO_OK;
}
