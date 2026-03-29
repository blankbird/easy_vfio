/*
 * easy_vfio - High-level context API implementation
 *
 * Provides a simplified, context-based interface to VFIO operations.
 * Each function operates on an evfio_ctx_t handle that encapsulates
 * all resources for a single PCIe device.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>

#include "easy_vfio_internal.h"

/* ----------------------------------------------------------------
 *  evfio_load_vfio_driver - Idempotent VFIO driver bind
 * ---------------------------------------------------------------- */

int evfio_load_vfio_driver(const char *bdf)
{
    if (!bdf || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    /* Already bound to vfio-pci? Nothing to do. */
    if (evfio_is_bound_to_vfio(bdf))
        return EVFIO_OK;

    return evfio_bind_device(bdf);
}

/* ----------------------------------------------------------------
 *  evfio_open - Create and initialize a device context
 * ---------------------------------------------------------------- */

int evfio_open(evfio_ctx_t **ctx, const char *bdf)
{
    evfio_ctx_t *c;
    int ret;
    int group_id;
    uint32_t i;

    if (!ctx || !bdf || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    /* Idempotent: already initialized */
    if (*ctx != NULL && (*ctx)->initialized)
        return EVFIO_OK;

    /* Allocate context */
    c = calloc(1, sizeof(*c));
    if (!c)
        return EVFIO_ERR_ALLOC;

    /* Initialize all fds to -1 */
    c->container.fd = -1;
    c->group.fd = -1;
    c->device.fd = -1;
    for (i = 0; i < EVFIO_MAX_MSI_VECTORS; i++)
        c->msi_vectors[i].event_fd = -1;

    snprintf(c->bdf, EVFIO_BDF_MAX_LEN, "%s", bdf);

    /* Look up IOMMU group */
    group_id = evfio_get_iommu_group(bdf);
    if (group_id < 0) {
        ret = group_id;
        goto err_free;
    }
    c->iommu_group_id = group_id;

    /* Open container */
    ret = evfio_container_open(&c->container);
    if (ret != EVFIO_OK)
        goto err_free;

    /* Open group and attach to container */
    ret = evfio_group_open(&c->group, &c->container, group_id);
    if (ret != EVFIO_OK)
        goto err_close_container;

    /* Set IOMMU type */
    ret = evfio_container_set_iommu(&c->container, VFIO_TYPE1v2_IOMMU);
    if (ret != EVFIO_OK)
        goto err_close_group;

    /* Open device */
    ret = evfio_device_open(&c->device, &c->group, bdf);
    if (ret != EVFIO_OK)
        goto err_close_group;

    c->initialized = 1;
    *ctx = c;
    return EVFIO_OK;

err_close_group:
    evfio_group_close(&c->group);
err_close_container:
    evfio_container_close(&c->container);
err_free:
    free(c);
    return ret;
}

/* ----------------------------------------------------------------
 *  evfio_close - Destroy context and release all resources
 * ---------------------------------------------------------------- */

void evfio_close(evfio_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* Disable MSI if still active */
    if (ctx->msi_enabled)
        evfio_msi_disable(ctx);

    /* Close VFIO resources in reverse order */
    evfio_device_close(&ctx->device);
    evfio_group_close(&ctx->group);
    evfio_container_close(&ctx->container);

    ctx->initialized = 0;
    free(ctx);
}

/* ----------------------------------------------------------------
 *  evfio_msi_enable - Enable MSI interrupts with eventfds
 * ---------------------------------------------------------------- */

int evfio_msi_enable(evfio_ctx_t *ctx, uint32_t num_vectors)
{
    int fds[EVFIO_MAX_MSI_VECTORS];
    uint32_t i;
    int ret;

    if (!ctx || !ctx->initialized)
        return EVFIO_ERR_INVAL;
    if (num_vectors == 0 || num_vectors > EVFIO_MAX_MSI_VECTORS)
        return EVFIO_ERR_INVAL;

    /* Already enabled with same config? */
    if (ctx->msi_enabled && ctx->msi_count == num_vectors)
        return EVFIO_OK;

    /* If previously enabled with different count, disable first */
    if (ctx->msi_enabled)
        evfio_msi_disable(ctx);

    /* Create eventfds for each vector */
    for (i = 0; i < num_vectors; i++) {
        int efd = eventfd(0, EFD_CLOEXEC);
        if (efd < 0) {
            /* Cleanup already created eventfds */
            uint32_t j;
            for (j = 0; j < i; j++) {
                close(ctx->msi_vectors[j].event_fd);
                ctx->msi_vectors[j].event_fd = -1;
            }
            return EVFIO_ERR_OPEN;
        }
        ctx->msi_vectors[i].event_fd = efd;
        fds[i] = efd;
    }

    /* Enable MSI via VFIO */
    ret = evfio_irq_enable(&ctx->device, VFIO_PCI_MSI_IRQ_INDEX,
                           fds, num_vectors);
    if (ret != EVFIO_OK) {
        /* Cleanup eventfds on failure */
        for (i = 0; i < num_vectors; i++) {
            close(ctx->msi_vectors[i].event_fd);
            ctx->msi_vectors[i].event_fd = -1;
        }
        return ret;
    }

    ctx->msi_count = num_vectors;
    ctx->msi_enabled = 1;
    return EVFIO_OK;
}

/* ----------------------------------------------------------------
 *  evfio_msi_disable - Disable MSI interrupts and close eventfds
 * ---------------------------------------------------------------- */

int evfio_msi_disable(evfio_ctx_t *ctx)
{
    uint32_t i;
    int ret;

    if (!ctx || !ctx->initialized)
        return EVFIO_ERR_INVAL;

    /* Not enabled? Nothing to do. */
    if (!ctx->msi_enabled)
        return EVFIO_OK;

    /* Disable IRQ in VFIO */
    ret = evfio_irq_disable(&ctx->device, VFIO_PCI_MSI_IRQ_INDEX);

    /* Close all eventfds regardless of disable result */
    for (i = 0; i < ctx->msi_count; i++) {
        if (ctx->msi_vectors[i].event_fd >= 0) {
            close(ctx->msi_vectors[i].event_fd);
            ctx->msi_vectors[i].event_fd = -1;
        }
    }

    ctx->msi_count = 0;
    ctx->msi_enabled = 0;
    return ret;
}

/* ----------------------------------------------------------------
 *  evfio_handle_interrupt - Wait for interrupt on a vector
 * ---------------------------------------------------------------- */

int evfio_handle_interrupt(evfio_ctx_t *ctx, uint32_t vector)
{
    uint64_t count;
    ssize_t n;

    if (!ctx || !ctx->initialized)
        return EVFIO_ERR_INVAL;
    if (!ctx->msi_enabled || vector >= ctx->msi_count)
        return EVFIO_ERR_INVAL;
    if (ctx->msi_vectors[vector].event_fd < 0)
        return EVFIO_ERR_INVAL;

    /* Blocking read on eventfd - returns when interrupt fires */
    n = read(ctx->msi_vectors[vector].event_fd, &count, sizeof(count));
    if (n != (ssize_t)sizeof(count))
        return EVFIO_ERR_IOCTL;

    return EVFIO_OK;
}

/* ----------------------------------------------------------------
 *  evfio_dma_map - Map external memory for DMA (no allocation)
 * ---------------------------------------------------------------- */

int evfio_dma_map(evfio_ctx_t *ctx, evfio_dma_t *dma,
                  void *vaddr, uint64_t size, uint64_t iova)
{
    int ret;

    if (!ctx || !ctx->initialized || !dma || !vaddr || size == 0)
        return EVFIO_ERR_INVAL;

    size = evfio_page_align(size);

    ret = evfio_iommu_dma_map(ctx->container.fd, vaddr, size, iova);
    if (ret != EVFIO_OK)
        return ret;

    dma->vaddr = vaddr;
    dma->iova = iova;
    dma->size = size;
    return EVFIO_OK;
}

/* ----------------------------------------------------------------
 *  evfio_dma_unmap - Unmap DMA (no memory free)
 * ---------------------------------------------------------------- */

int evfio_dma_unmap(evfio_ctx_t *ctx, evfio_dma_t *dma)
{
    int ret;

    if (!ctx || !ctx->initialized || !dma || !dma->vaddr)
        return EVFIO_ERR_INVAL;

    ret = evfio_iommu_dma_unmap(ctx->container.fd, dma->iova, dma->size);

    dma->vaddr = NULL;
    dma->iova = 0;
    dma->size = 0;
    return ret;
}

/* ----------------------------------------------------------------
 *  evfio_dma_alloc_map - Allocate memory + create DMA mapping
 * ---------------------------------------------------------------- */

int evfio_dma_alloc_map(evfio_ctx_t *ctx, evfio_dma_t *dma,
                        uint64_t size, uint64_t iova)
{
    void *vaddr;
    int ret;

    if (!ctx || !ctx->initialized || !dma || size == 0)
        return EVFIO_ERR_INVAL;

    memset(dma, 0, sizeof(*dma));

    size = evfio_page_align(size);

    /* Allocate page-aligned memory */
    vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (vaddr == MAP_FAILED)
        return EVFIO_ERR_ALLOC;

    /* Create IOMMU mapping */
    ret = evfio_iommu_dma_map(ctx->container.fd, vaddr, size, iova);
    if (ret != EVFIO_OK) {
        munmap(vaddr, size);
        return ret;
    }

    dma->vaddr = vaddr;
    dma->iova = iova;
    dma->size = size;
    return EVFIO_OK;
}

/* ----------------------------------------------------------------
 *  evfio_dma_free_unmap - Unmap DMA + free allocated memory
 * ---------------------------------------------------------------- */

int evfio_dma_free_unmap(evfio_ctx_t *ctx, evfio_dma_t *dma)
{
    int ret;

    if (!ctx || !ctx->initialized || !dma || !dma->vaddr)
        return EVFIO_ERR_INVAL;

    ret = evfio_iommu_dma_unmap(ctx->container.fd, dma->iova, dma->size);

    /* Always free memory, even if unmap fails */
    munmap(dma->vaddr, dma->size);

    dma->vaddr = NULL;
    dma->iova = 0;
    dma->size = 0;
    return ret;
}
