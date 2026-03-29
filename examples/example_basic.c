/*
 * easy_vfio - Basic usage example
 *
 * This example demonstrates the typical workflow for accessing a PCIe device
 * via VFIO. It must be run as root (or with appropriate permissions) and
 * the target device must be bound to the vfio-pci driver.
 *
 * Usage: ./example_basic <BDF>
 * Example: ./example_basic 0000:01:00.0
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "easy_vfio.h"

int main(int argc, char *argv[])
{
    evfio_container_t container;
    evfio_group_t group;
    evfio_device_t device;
    evfio_region_t bar0;
    evfio_dma_t dma;
    int ret;
    int group_id;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <BDF>\n", argv[0]);
        fprintf(stderr, "Example: %s 0000:01:00.0\n", argv[0]);
        return 1;
    }

    const char *bdf = argv[1];

    /* Validate BDF */
    if (!evfio_bdf_valid(bdf)) {
        fprintf(stderr, "Error: Invalid BDF address '%s'\n", bdf);
        return 1;
    }

    printf("easy_vfio example - accessing device %s\n\n", bdf);

    /* Step 1: Look up IOMMU group */
    printf("[1] Looking up IOMMU group for %s...\n", bdf);
    group_id = evfio_get_iommu_group(bdf);
    if (group_id < 0) {
        fprintf(stderr, "    Failed: %s\n", evfio_strerror(group_id));
        fprintf(stderr, "    Hint: Is the device bound to vfio-pci?\n");
        return 1;
    }
    printf("    IOMMU group: %d\n", group_id);

    /* Step 2: Open VFIO container */
    printf("[2] Opening VFIO container...\n");
    ret = evfio_container_open(&container);
    if (ret != EVFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", evfio_strerror(ret));
        return 1;
    }
    printf("    Container fd: %d\n", container.fd);

    /* Step 3: Open VFIO group and attach to container */
    printf("[3] Opening VFIO group %d...\n", group_id);
    ret = evfio_group_open(&group, &container, group_id);
    if (ret != EVFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", evfio_strerror(ret));
        evfio_container_close(&container);
        return 1;
    }
    printf("    Group fd: %d\n", group.fd);

    /* Step 4: Set IOMMU type */
    printf("[4] Setting IOMMU type...\n");
    ret = evfio_container_set_iommu(&container, VFIO_TYPE1v2_IOMMU);
    if (ret != EVFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", evfio_strerror(ret));
        evfio_group_close(&group);
        evfio_container_close(&container);
        return 1;
    }
    printf("    IOMMU set to TYPE1v2\n");

    /* Step 5: Get device fd */
    printf("[5] Opening device %s...\n", bdf);
    ret = evfio_device_open(&device, &group, bdf);
    if (ret != EVFIO_OK) {
        fprintf(stderr, "    Failed: %s\n", evfio_strerror(ret));
        evfio_group_close(&group);
        evfio_container_close(&container);
        return 1;
    }
    printf("    Device fd: %d\n", device.fd);
    printf("    Regions: %u, IRQs: %u\n", device.num_regions, device.num_irqs);

    /* Step 6: Read PCI config space (vendor/device ID) */
    printf("[6] Reading PCI config space...\n");
    uint16_t vendor_id = 0, device_id = 0;
    ret = (int)evfio_pci_config_read(&device, &vendor_id, sizeof(vendor_id), 0);
    if (ret > 0)
        ret = (int)evfio_pci_config_read(&device, &device_id, sizeof(device_id), 2);
    if (ret > 0) {
        printf("    Vendor ID: 0x%04x\n", vendor_id);
        printf("    Device ID: 0x%04x\n", device_id);
    } else {
        printf("    Warning: Could not read config space\n");
    }

    /* Step 7: Map BAR0 */
    printf("[7] Mapping BAR0...\n");
    ret = evfio_region_map(&bar0, &device, VFIO_PCI_BAR0_REGION_INDEX);
    if (ret == EVFIO_OK) {
        printf("    BAR0 mapped at %p, size=%lu bytes\n",
               bar0.addr, (unsigned long)bar0.size);

        /* Read first 32-bit register */
        uint32_t reg0 = evfio_mmio_read32(&bar0, 0);
        printf("    BAR0[0x00] = 0x%08x\n", reg0);

        evfio_region_unmap(&bar0);
        printf("    BAR0 unmapped\n");
    } else {
        printf("    BAR0 not mappable (may not support mmap): %s\n",
               evfio_strerror(ret));
    }

    /* Step 8: DMA mapping example */
    printf("[8] Creating DMA mapping (4KB at IOVA 0x100000)...\n");
    ret = evfio_dma_map(&container, &dma, 4096, 0x100000);
    if (ret == EVFIO_OK) {
        printf("    DMA mapped: vaddr=%p, iova=0x%lx, size=%lu\n",
               dma.vaddr, (unsigned long)dma.iova, (unsigned long)dma.size);

        /* Write something to the DMA buffer */
        memset(dma.vaddr, 0xAB, 64);
        printf("    Wrote test pattern to DMA buffer\n");

        evfio_dma_unmap(&container, &dma);
        printf("    DMA unmapped\n");
    } else {
        printf("    DMA mapping failed: %s\n", evfio_strerror(ret));
    }

    /* Step 9: Reset device */
    printf("[9] Resetting device...\n");
    ret = evfio_device_reset(&device);
    if (ret == EVFIO_OK)
        printf("    Device reset successful\n");
    else
        printf("    Device reset: %s\n", evfio_strerror(ret));

    /* Cleanup */
    printf("\n[Cleanup]\n");
    evfio_device_close(&device);
    printf("    Device closed\n");
    evfio_group_close(&group);
    printf("    Group closed\n");
    evfio_container_close(&container);
    printf("    Container closed\n");

    printf("\nDone.\n");
    return 0;
}
