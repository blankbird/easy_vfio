/*
 * easy_vfio - DMA IOMMU mapping operations
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "vfio_internal.h"

int vfio_iommu_dma_map(int container_fd, void *vaddr, uint64_t size,
                        uint64_t iova)
{
    struct vfio_iommu_type1_dma_map dma_map;

    if (container_fd < 0 || !vaddr || size == 0)
        return VFIO_ERR_INVAL;

    size = vfio_page_align(size);

    memset(&dma_map, 0, sizeof(dma_map));
    dma_map.argsz = sizeof(dma_map);
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
    dma_map.vaddr = (uint64_t)(uintptr_t)vaddr;
    dma_map.iova = iova;
    dma_map.size = size;

    if (ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map) < 0)
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

int vfio_iommu_dma_unmap(int container_fd, uint64_t iova, uint64_t size)
{
    struct vfio_iommu_type1_dma_unmap dma_unmap;

    if (container_fd < 0 || size == 0)
        return VFIO_ERR_INVAL;

    memset(&dma_unmap, 0, sizeof(dma_unmap));
    dma_unmap.argsz = sizeof(dma_unmap);
    dma_unmap.iova = iova;
    dma_unmap.size = size;

    if (ioctl(container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap) < 0)
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}
