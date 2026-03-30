/*
 * easy_vfio - Basic usage example (High-Level API)
 *
 * Demonstrates the simplified context-based API workflow.
 *
 * Usage: ./example_basic <BDF>
 * Example: ./example_basic 0000:01:00.0
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfio_internal.h"

int main(int argc, char *argv[])
{
    vfio_ctx_t *ctx = NULL;
    vfio_dma_t dma;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <BDF>\n", argv[0]);
        fprintf(stderr, "Example: %s 0000:01:00.0\n", argv[0]);
        return 1;
    }

    const char *bdf = argv[1];

    printf("easy_vfio example - accessing device %s\n\n", bdf);

    /* Step 1: Load VFIO driver (unbind current driver, bind vfio-pci) */
    printf("[1] Loading VFIO driver for %s...\n", bdf);
    ret = vfio_load_vfio_driver(bdf);
    if (ret != VFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", vfio_strerror(ret));
        return 1;
    }
    printf("    VFIO driver loaded\n");

    /* Step 2: Open device (container + group + IOMMU + device in one call) */
    printf("[2] Opening VFIO device...\n");
    ret = vfio_open(&ctx, bdf);
    if (ret != VFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", vfio_strerror(ret));
        return 1;
    }
    printf("    Device opened (group=%d, regions=%u, irqs=%u)\n",
           ctx->iommu_group_id,
           ctx->device.num_regions,
           ctx->device.num_irqs);

    /* Step 3: Enable MSI interrupts (1 vector) */
    printf("[3] Enabling MSI interrupt (1 vector)...\n");
    ret = vfio_msi_enable(ctx, 1);
    if (ret == VFIO_OK) {
        printf("    MSI enabled, eventfd=%d\n",
               ctx->msi_vectors[0].event_fd);
    } else {
        printf("    MSI enable: %s (continuing without MSI)\n",
               vfio_strerror(ret));
    }

    /* Step 4: Allocate + Map DMA buffer */
    printf("[4] Allocating DMA buffer (4KB at IOVA 0x100000)...\n");
    ret = vfio_dma_alloc_map(ctx, &dma, 4096, 0x100000);
    if (ret == VFIO_OK) {
        printf("    DMA: vaddr=%p, iova=0x%lx, size=%lu\n",
               dma.vaddr, (unsigned long)dma.iova,
               (unsigned long)dma.size);

        memset(dma.vaddr, 0xAB, 64);
        printf("    Wrote test pattern to DMA buffer\n");

        vfio_dma_free_unmap(ctx, &dma);
        printf("    DMA freed\n");
    } else {
        printf("    DMA alloc failed: %s\n", vfio_strerror(ret));
    }

    /* Step 5: Map BAR0 (using low-level API for MMIO access) */
    printf("[5] Mapping BAR0 (low-level API)...\n");
    vfio_region_t bar0;
    ret = vfio_region_map(&bar0, &ctx->device, VFIO_PCI_BAR0_REGION_INDEX);
    if (ret == VFIO_OK) {
        uint32_t reg0 = vfio_mmio_read32(&bar0, 0);
        printf("    BAR0[0x00] = 0x%08x\n", reg0);
        vfio_region_unmap(&bar0);
    } else {
        printf("    BAR0 not mappable: %s\n", vfio_strerror(ret));
    }

    /* Step 6: Cleanup (closes everything including MSI) */
    printf("\n[Cleanup]\n");
    vfio_close(ctx);
    printf("    All resources released\n");

    printf("\nDone.\n");
    return 0;
}
