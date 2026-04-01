/*
 * easy_vfio - High-level context API implementation
 *
 * Provides a simplified, context-based interface to VFIO operations.
 * Each function operates on a vfio_ctx_t handle that encapsulates
 * all resources for a single PCIe device.
 *
 * Supports two backends:
 *   - Legacy (pre-6.8): container + group + TYPE1v2 IOMMU
 *   - New (>= 6.8):     iommufd + device cdev
 *
 * vfio_open() tries the iommufd path first; on failure it falls
 * back to the legacy container/group path transparently.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <linux/pci_regs.h>

#include "vfio_internal.h"

/* ----------------------------------------------------------------
 *  vfio_load_vfio_driver - Idempotent VFIO driver bind
 * ---------------------------------------------------------------- */

int vfio_load_vfio_driver(const char *bdf)
{
    if (!bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* Already bound to vfio-pci? Nothing to do. */
    if (vfio_is_bound_to_vfio(bdf))
        return VFIO_OK;

    return vfio_bind_device(bdf);
}

/* ----------------------------------------------------------------
 *  Internal: open device via iommufd/cdev (new VFIO path)
 *
 *  1. Open /dev/iommu and allocate IOAS
 *  2. Open device cdev via sysfs
 *  3. Bind device to iommufd
 *  4. Attach device to IOAS
 *  5. Query device info
 * ---------------------------------------------------------------- */

static int vfio_open_iommufd(vfio_ctx_t *c, const char *bdf)
{
    int dev_fd;
    int ret;

    /* Step 1: open iommufd and allocate IOAS */
    ret = vfio_iommufd_open(&c->iommufd);
    if (ret != VFIO_OK)
        return ret;

    /* Step 2: open the device character device */
    dev_fd = vfio_iommufd_device_open(bdf);
    if (dev_fd < 0) {
        vfio_iommufd_close(&c->iommufd);
        return dev_fd;
    }

    /* Step 3: bind device to iommufd */
    ret = vfio_iommufd_device_bind(&c->iommufd, dev_fd);
    if (ret != VFIO_OK) {
        close(dev_fd);
        vfio_iommufd_close(&c->iommufd);
        return ret;
    }

    /* Step 4: attach device to IOAS */
    ret = vfio_iommufd_device_attach(&c->iommufd, dev_fd);
    if (ret != VFIO_OK) {
        close(dev_fd);
        vfio_iommufd_close(&c->iommufd);
        return ret;
    }

    /* Step 5: initialize device info from the open fd */
    ret = vfio_device_init_from_fd(&c->device, dev_fd, bdf);
    if (ret != VFIO_OK) {
        vfio_iommufd_close(&c->iommufd);
        return ret;
    }

    c->mode = VFIO_MODE_IOMMUFD;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  Internal: open device via legacy container/group path
 *
 *  1. Look up IOMMU group for the BDF
 *  2. Open container (/dev/vfio/vfio)
 *  3. Open group (/dev/vfio/<gid>) and attach to container
 *  4. Set IOMMU type (TYPE1v2)
 *  5. Get device fd from group
 * ---------------------------------------------------------------- */

static int vfio_open_legacy(vfio_ctx_t *c, const char *bdf)
{
    int ret;
    int group_id;

    /* Step 1: look up IOMMU group */
    group_id = vfio_get_iommu_group(bdf);
    if (group_id < 0)
        return group_id;
    c->iommu_group_id = group_id;

    /* Step 2: open container */
    ret = vfio_container_open(&c->container);
    if (ret != VFIO_OK)
        return ret;

    /* Step 3: open group and attach to container */
    ret = vfio_group_open(&c->group, &c->container, group_id);
    if (ret != VFIO_OK) {
        vfio_container_close(&c->container);
        return ret;
    }

    /* Step 4: set IOMMU type */
    ret = vfio_container_set_iommu(&c->container, VFIO_TYPE1v2_IOMMU);
    if (ret != VFIO_OK) {
        vfio_group_close(&c->group);
        vfio_container_close(&c->container);
        return ret;
    }

    /* Step 5: open device via group */
    ret = vfio_device_open(&c->device, &c->group, bdf);
    if (ret != VFIO_OK) {
        vfio_group_close(&c->group);
        vfio_container_close(&c->container);
        return ret;
    }

    c->mode = VFIO_MODE_LEGACY;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_open - Create and initialize a device context
 *
 *  Tries the iommufd/cdev path first (new VFIO). If that fails,
 *  falls back to the legacy container/group path.
 * ---------------------------------------------------------------- */

int vfio_open(vfio_ctx_t **ctx, const char *bdf)
{
    vfio_ctx_t *c;
    int ret;
    uint32_t i;

    if (!ctx || !bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* Idempotent: already initialized */
    if (*ctx != NULL && (*ctx)->initialized)
        return VFIO_OK;

    /* Allocate context */
    c = calloc(1, sizeof(*c));
    if (!c)
        return VFIO_ERR_ALLOC;

    /* Initialize all fds to -1 */
    c->container.fd = -1;
    c->group.fd = -1;
    c->device.fd = -1;
    c->iommufd.fd = -1;
    for (i = 0; i < VFIO_MAX_MSI_VECTORS; i++)
        c->msi_vectors[i].event_fd = -1;

    snprintf(c->bdf, VFIO_BDF_MAX_LEN, "%s", bdf);

    /* Try new VFIO (iommufd/cdev) first */
    ret = vfio_open_iommufd(c, bdf);
    if (ret != VFIO_OK) {
        /* Fallback to legacy container/group path */
        ret = vfio_open_legacy(c, bdf);
    }

    if (ret != VFIO_OK) {
        free(c);
        return ret;
    }

    c->initialized = 1;
    *ctx = c;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_close - Destroy context and release all resources
 * ---------------------------------------------------------------- */

void vfio_close(vfio_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* Disable MSI if still active */
    if (ctx->msi_enabled)
        vfio_msi_disable(ctx);

    /* Close device fd (shared between both modes) */
    vfio_device_close(&ctx->device);

    /* Release backend-specific resources */
    if (ctx->mode == VFIO_MODE_IOMMUFD) {
        vfio_iommufd_close(&ctx->iommufd);
    } else {
        vfio_group_close(&ctx->group);
        vfio_container_close(&ctx->container);
    }

    ctx->initialized = 0;
    free(ctx);
}

/* ----------------------------------------------------------------
 *  vfio_msi_enable - Enable MSI interrupts with eventfds
 * ---------------------------------------------------------------- */

int vfio_msi_enable(vfio_ctx_t *ctx, uint32_t num_vectors)
{
    int fds[VFIO_MAX_MSI_VECTORS];
    uint32_t i;
    int ret;

    if (!ctx || !ctx->initialized)
        return VFIO_ERR_INVAL;
    if (num_vectors == 0 || num_vectors > VFIO_MAX_MSI_VECTORS)
        return VFIO_ERR_INVAL;

    /* Already enabled with same config? */
    if (ctx->msi_enabled && ctx->msi_count == num_vectors)
        return VFIO_OK;

    /* If previously enabled with different count, disable first */
    if (ctx->msi_enabled)
        vfio_msi_disable(ctx);

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
            return VFIO_ERR_OPEN;
        }
        ctx->msi_vectors[i].event_fd = efd;
        fds[i] = efd;
    }

    /* Enable MSI via VFIO */
    ret = vfio_irq_enable(&ctx->device, VFIO_PCI_MSI_IRQ_INDEX,
                           fds, num_vectors);
    if (ret != VFIO_OK) {
        /* Cleanup eventfds on failure */
        for (i = 0; i < num_vectors; i++) {
            close(ctx->msi_vectors[i].event_fd);
            ctx->msi_vectors[i].event_fd = -1;
        }
        return ret;
    }

    ctx->msi_count = num_vectors;
    ctx->msi_enabled = 1;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_msi_disable - Disable MSI interrupts and close eventfds
 * ---------------------------------------------------------------- */

int vfio_msi_disable(vfio_ctx_t *ctx)
{
    uint32_t i;
    int ret;

    if (!ctx || !ctx->initialized)
        return VFIO_ERR_INVAL;

    /* Not enabled? Nothing to do. */
    if (!ctx->msi_enabled)
        return VFIO_OK;

    /* Disable IRQ in VFIO */
    ret = vfio_irq_disable(&ctx->device, VFIO_PCI_MSI_IRQ_INDEX);

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
 *  vfio_handle_interrupt - Wait for interrupt on a vector
 * ---------------------------------------------------------------- */

int vfio_handle_interrupt(vfio_ctx_t *ctx, uint32_t vector)
{
    uint64_t count;
    ssize_t n;

    if (!ctx || !ctx->initialized)
        return VFIO_ERR_INVAL;
    if (!ctx->msi_enabled || vector >= ctx->msi_count)
        return VFIO_ERR_INVAL;
    if (ctx->msi_vectors[vector].event_fd < 0)
        return VFIO_ERR_INVAL;

    /* Blocking read on eventfd - returns when interrupt fires */
    n = read(ctx->msi_vectors[vector].event_fd, &count, sizeof(count));
    if (n != (ssize_t)sizeof(count))
        return VFIO_ERR_IOCTL;

    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_dma_map - Map external memory for DMA (no allocation)
 *
 *  Dispatches to iommufd or legacy container based on ctx->mode.
 * ---------------------------------------------------------------- */

int vfio_dma_map(vfio_ctx_t *ctx, vfio_dma_t *dma, uint64_t iova)
{
    int ret;
    uint64_t size;

    if (!ctx || !ctx->initialized || !dma || !dma->vaddr || dma->size == 0)
        return VFIO_ERR_INVAL;

    size = vfio_page_align(dma->size);

    /* Dispatch based on backend mode */
    if (ctx->mode == VFIO_MODE_IOMMUFD)
        ret = vfio_iommufd_dma_map(&ctx->iommufd, dma->vaddr, size, iova);
    else
        ret = vfio_iommu_dma_map(ctx->container.fd, dma->vaddr, size, iova);

    if (ret != VFIO_OK)
        return ret;

    dma->iova = iova;
    dma->size = size;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_dma_unmap - Unmap DMA (no memory free)
 * ---------------------------------------------------------------- */

int vfio_dma_unmap(vfio_ctx_t *ctx, vfio_dma_t *dma)
{
    int ret;

    if (!ctx || !ctx->initialized || !dma || !dma->vaddr)
        return VFIO_ERR_INVAL;

    /* Dispatch based on backend mode */
    if (ctx->mode == VFIO_MODE_IOMMUFD)
        ret = vfio_iommufd_dma_unmap(&ctx->iommufd, dma->iova, dma->size);
    else
        ret = vfio_iommu_dma_unmap(ctx->container.fd, dma->iova, dma->size);

    dma->vaddr = NULL;
    dma->iova = 0;
    dma->size = 0;
    return ret;
}

/* ----------------------------------------------------------------
 *  vfio_dma_alloc_map - Allocate memory + create DMA mapping
 * ---------------------------------------------------------------- */

int vfio_dma_alloc_map(vfio_ctx_t *ctx, vfio_dma_t *dma,
                        uint64_t size, uint64_t iova)
{
    void *vaddr;
    int ret;

    if (!ctx || !ctx->initialized || !dma || size == 0)
        return VFIO_ERR_INVAL;

    memset(dma, 0, sizeof(*dma));

    size = vfio_page_align(size);

    /* Allocate page-aligned memory */
    vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (vaddr == MAP_FAILED)
        return VFIO_ERR_ALLOC;

    /* Create IOMMU mapping via the appropriate backend */
    if (ctx->mode == VFIO_MODE_IOMMUFD)
        ret = vfio_iommufd_dma_map(&ctx->iommufd, vaddr, size, iova);
    else
        ret = vfio_iommu_dma_map(ctx->container.fd, vaddr, size, iova);

    if (ret != VFIO_OK) {
        munmap(vaddr, size);
        return ret;
    }

    dma->vaddr = vaddr;
    dma->iova = iova;
    dma->size = size;
    return VFIO_OK;
}

/* ----------------------------------------------------------------
 *  vfio_dma_free_unmap - Unmap DMA + free allocated memory
 * ---------------------------------------------------------------- */

int vfio_dma_free_unmap(vfio_ctx_t *ctx, vfio_dma_t *dma)
{
    int ret;

    if (!ctx || !ctx->initialized || !dma || !dma->vaddr)
        return VFIO_ERR_INVAL;

    /* Unmap via the appropriate backend */
    if (ctx->mode == VFIO_MODE_IOMMUFD)
        ret = vfio_iommufd_dma_unmap(&ctx->iommufd, dma->iova, dma->size);
    else
        ret = vfio_iommu_dma_unmap(ctx->container.fd, dma->iova, dma->size);

    /* Always free memory, even if unmap fails */
    munmap(dma->vaddr, dma->size);

    dma->vaddr = NULL;
    dma->iova = 0;
    dma->size = 0;
    return ret;
}

/* ----------------------------------------------------------------
 *  vfio_msi_get_config - Read MSI configuration from sysfs PCI config
 *
 *  After VFIO takes over a device, reading MSI capability data through
 *  the VFIO config region may not return valid information. Instead,
 *  read directly from the sysfs config file at:
 *    /sys/bus/pci/devices/<bdf>/config
 *  using standard PCI register offsets from <linux/pci_regs.h>.
 * ---------------------------------------------------------------- */

int vfio_msi_get_config(vfio_ctx_t *ctx, vfio_msi_config_t *config)
{
    char config_path[PATH_MAX];
    int fd;
    uint8_t cap_ptr;
    uint8_t cap_id;
    uint16_t msi_control;
    ssize_t n;
    int max_caps = 48; /* Safety limit to avoid infinite loops */

    if (!ctx || !ctx->initialized || !config)
        return VFIO_ERR_INVAL;

    if (!ctx->msi_enabled)
        return VFIO_ERR_INVAL;

    memset(config, 0, sizeof(*config));

    /* Open sysfs PCI config file for this BDF */
    snprintf(config_path, sizeof(config_path),
             "/sys/bus/pci/devices/%s/config", ctx->bdf);

    fd = open(config_path, O_RDONLY);
    if (fd < 0)
        return VFIO_ERR_OPEN;

    /* Read capability pointer from PCI config space */
    n = pread(fd, &cap_ptr, sizeof(cap_ptr), PCI_CAPABILITY_LIST);
    if (n != (ssize_t)sizeof(cap_ptr)) {
        close(fd);
        return VFIO_ERR_IOCTL;
    }

    /* Walk the capability list to find MSI capability */
    cap_id = 0;
    while (cap_ptr != 0 && max_caps-- > 0) {
        /* Capability pointers are dword-aligned */
        cap_ptr &= 0xFC;
        if (cap_ptr == 0)
            break;

        n = pread(fd, &cap_id, sizeof(cap_id), cap_ptr);
        if (n != (ssize_t)sizeof(cap_id)) {
            close(fd);
            return VFIO_ERR_IOCTL;
        }

        if (cap_id == PCI_CAP_ID_MSI)
            break;

        /* Read next capability pointer (offset + 1) */
        n = pread(fd, &cap_ptr, sizeof(cap_ptr), cap_ptr + 1);
        if (n != (ssize_t)sizeof(cap_ptr)) {
            close(fd);
            return VFIO_ERR_IOCTL;
        }
    }

    if (cap_id != PCI_CAP_ID_MSI) {
        close(fd);
        return VFIO_ERR_NOSYS;
    }

    /* Read MSI Message Control register */
    n = pread(fd, &msi_control, sizeof(msi_control),
              cap_ptr + PCI_MSI_FLAGS);
    if (n != (ssize_t)sizeof(msi_control)) {
        close(fd);
        return VFIO_ERR_IOCTL;
    }

    config->control = msi_control;

    /* Read Message Address (lower 32 bits) */
    n = pread(fd, &config->addr_low, sizeof(config->addr_low),
              cap_ptr + PCI_MSI_ADDRESS_LO);
    if (n != (ssize_t)sizeof(config->addr_low)) {
        close(fd);
        return VFIO_ERR_IOCTL;
    }

    /* Check if 64-bit capable */
    if (msi_control & PCI_MSI_FLAGS_64BIT) {
        /* Read upper 32 bits of address */
        n = pread(fd, &config->addr_high, sizeof(config->addr_high),
                  cap_ptr + PCI_MSI_ADDRESS_HI);
        if (n != (ssize_t)sizeof(config->addr_high)) {
            close(fd);
            return VFIO_ERR_IOCTL;
        }

        /* Read Message Data at 64-bit offset */
        n = pread(fd, &config->raw_data, sizeof(config->raw_data),
                  cap_ptr + PCI_MSI_DATA_64);
    } else {
        config->addr_high = 0;
        /* Read Message Data at 32-bit offset */
        n = pread(fd, &config->raw_data, sizeof(config->raw_data),
                  cap_ptr + PCI_MSI_DATA_32);
    }

    if (n != (ssize_t)sizeof(config->raw_data)) {
        close(fd);
        return VFIO_ERR_IOCTL;
    }

    close(fd);
    return VFIO_OK;
}
