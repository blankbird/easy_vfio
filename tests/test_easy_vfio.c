/*
 * easy_vfio - Unit tests
 *
 * Tests for utility functions and API validation that can run without
 * actual VFIO hardware.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "vfio_internal.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s", name); \
    fflush(stdout); \
} while (0)

#define PASS() do { \
    tests_passed++; \
    printf("[PASS]\n"); \
} while (0)

#define FAIL(msg) do { \
    printf("[FAIL] %s\n", msg); \
} while (0)

/* ---- BDF validation tests ---- */

static void test_bdf_valid_full_format(void)
{
    TEST("bdf_valid: full format (0000:01:00.0)");
    if (vfio_bdf_valid("0000:01:00.0"))
        PASS();
    else
        FAIL("expected valid");
}

static void test_bdf_valid_short_format(void)
{
    TEST("bdf_valid: short format (01:00.0)");
    if (vfio_bdf_valid("01:00.0"))
        PASS();
    else
        FAIL("expected valid");
}

static void test_bdf_valid_various(void)
{
    TEST("bdf_valid: various valid addresses");
    int ok = 1;
    ok &= vfio_bdf_valid("0000:00:00.0");
    ok &= vfio_bdf_valid("ffff:ff:1f.7");
    ok &= vfio_bdf_valid("0000:03:00.1");
    ok &= vfio_bdf_valid("00:1f.3");
    if (ok)
        PASS();
    else
        FAIL("expected all valid");
}

static void test_bdf_invalid_null(void)
{
    TEST("bdf_valid: NULL pointer");
    if (!vfio_bdf_valid(NULL))
        PASS();
    else
        FAIL("expected invalid");
}

static void test_bdf_invalid_empty(void)
{
    TEST("bdf_valid: empty string");
    if (!vfio_bdf_valid(""))
        PASS();
    else
        FAIL("expected invalid");
}

static void test_bdf_invalid_garbage(void)
{
    TEST("bdf_valid: garbage input");
    int ok = 1;
    ok &= !vfio_bdf_valid("hello");
    ok &= !vfio_bdf_valid("xx:yy.z");
    ok &= !vfio_bdf_valid("0000-01-00.0");
    if (ok)
        PASS();
    else
        FAIL("expected all invalid");
}

static void test_bdf_invalid_out_of_range(void)
{
    TEST("bdf_valid: out-of-range values");
    int ok = 1;
    /* device > 0x1F */
    ok &= !vfio_bdf_valid("0000:00:20.0");
    /* function > 7 */
    ok &= !vfio_bdf_valid("0000:00:00.8");
    if (ok)
        PASS();
    else
        FAIL("expected all invalid");
}

static void test_bdf_invalid_trailing(void)
{
    TEST("bdf_valid: trailing characters");
    if (!vfio_bdf_valid("0000:01:00.0extra"))
        PASS();
    else
        FAIL("expected invalid");
}

/* ---- Error string tests ---- */

static void test_strerror_known(void)
{
    TEST("strerror: known error codes");
    int ok = 1;
    ok &= (strcmp(vfio_strerror(VFIO_OK), "Success") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_INVAL), "Invalid argument") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_OPEN), "Failed to open file or device") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_IOCTL), "ioctl failed") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_MMAP), "mmap failed") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_ALLOC), "Memory allocation failed") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_PERM), "Permission denied") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_NOGROUP), "IOMMU group not found") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_NOTVIABLE), "VFIO group not viable") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_BUSY), "Resource busy") == 0);
    ok &= (strcmp(vfio_strerror(VFIO_ERR_NOSYS), "Not supported") == 0);
    if (ok)
        PASS();
    else
        FAIL("unexpected error string");
}

static void test_strerror_unknown(void)
{
    TEST("strerror: unknown error code");
    if (strcmp(vfio_strerror(-999), "Unknown error") == 0)
        PASS();
    else
        FAIL("expected 'Unknown error'");
}

/* ---- NULL-safety tests for API functions ---- */

static void test_container_null_safety(void)
{
    TEST("container: NULL safety");
    int ok = 1;
    ok &= (vfio_container_open(NULL) == VFIO_ERR_INVAL);
    vfio_container_close(NULL); /* Should not crash */
    ok &= (vfio_container_set_iommu(NULL, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_container_check_extension(NULL, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_container_invalid_fd(void)
{
    TEST("container: invalid fd safety");
    vfio_container_t c = { .fd = -1 };
    int ok = 1;
    ok &= (vfio_container_set_iommu(&c, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_container_check_extension(&c, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("invalid fd safety failed");
}

static void test_group_null_safety(void)
{
    TEST("group: NULL safety");
    int ok = 1;
    vfio_container_t c = { .fd = 999 };
    ok &= (vfio_group_open(NULL, &c, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_group_open((vfio_group_t[1]){}, NULL, 0) == VFIO_ERR_INVAL);
    vfio_group_close(NULL); /* Should not crash */
    ok &= (vfio_group_is_viable(NULL) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_device_null_safety(void)
{
    TEST("device: NULL safety");
    int ok = 1;
    vfio_group_t g = { .fd = 999 };
    ok &= (vfio_device_open(NULL, &g, "0000:01:00.0") == VFIO_ERR_INVAL);
    ok &= (vfio_device_open((vfio_device_t[1]){}, NULL, "0000:01:00.0") == VFIO_ERR_INVAL);
    ok &= (vfio_device_open((vfio_device_t[1]){}, &g, NULL) == VFIO_ERR_INVAL);
    vfio_device_close(NULL); /* Should not crash */
    ok &= (vfio_device_reset(NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_device_get_region_info(NULL, 0, NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_device_get_irq_info(NULL, 0, NULL) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_device_invalid_bdf(void)
{
    TEST("device: invalid BDF rejected");
    vfio_device_t dev;
    vfio_group_t g = { .fd = 999 };
    if (vfio_device_open(&dev, &g, "invalid") == VFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL");
}

static void test_region_null_safety(void)
{
    TEST("region: NULL safety");
    int ok = 1;
    vfio_device_t d = { .fd = 999 };
    ok &= (vfio_region_map(NULL, &d, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_region_map((vfio_region_t[1]){}, NULL, 0) == VFIO_ERR_INVAL);
    vfio_region_unmap(NULL); /* Should not crash */
    ok &= (vfio_region_read(NULL, 0, (char[1]){}, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_region_read(&d, 0, NULL, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_region_write(NULL, 0, (char[1]){}, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_region_write(&d, 0, NULL, 1, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_dma_null_safety(void)
{
    TEST("dma: NULL safety (low-level IOMMU)");
    int ok = 1;
    ok &= (vfio_iommu_dma_map(-1, (void *)(uintptr_t)0x1000, 4096, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_iommu_dma_map(999, NULL, 4096, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_iommu_dma_map(999, (void *)(uintptr_t)0x1000, 0, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_iommu_dma_unmap(-1, 0, 4096) == VFIO_ERR_INVAL);
    ok &= (vfio_iommu_dma_unmap(999, 0, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_irq_null_safety(void)
{
    TEST("irq: NULL safety");
    int ok = 1;
    int dummy_fd = 0;
    ok &= (vfio_irq_enable(NULL, 0, &dummy_fd, 1) == VFIO_ERR_INVAL);
    ok &= (vfio_irq_disable(NULL, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_irq_unmask_intx(NULL) == VFIO_ERR_INVAL);

    vfio_device_t d = { .fd = 999 };
    ok &= (vfio_irq_enable(&d, 0, NULL, 1) == VFIO_ERR_INVAL);
    ok &= (vfio_irq_enable(&d, 0, &dummy_fd, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_pci_config_null_safety(void)
{
    TEST("pci_config: NULL safety");
    int ok = 1;
    vfio_device_t d = { .fd = 999 };
    ok &= (vfio_pci_config_read(NULL, (char[1]){}, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_config_read(&d, NULL, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_config_read(&d, (char[1]){}, 0, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_config_write(NULL, (char[1]){}, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_config_write(&d, NULL, 1, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_config_write(&d, (char[1]){}, 0, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_iommu_group_invalid_bdf(void)
{
    TEST("get_iommu_group: invalid BDF");
    if (vfio_get_iommu_group("garbage") == VFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL");
}

static void test_bind_invalid_bdf(void)
{
    TEST("bind_device: invalid BDF");
    if (vfio_bind_device("invalid") == VFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL");
}

static void test_unbind_invalid_bdf(void)
{
    TEST("unbind_device: invalid BDF");
    if (vfio_unbind_device("invalid") == VFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL");
}

static void test_pci_get_ids_invalid(void)
{
    TEST("pci_get_ids: invalid arguments");
    uint16_t vid, did;
    int ok = 1;
    ok &= (vfio_pci_get_ids(NULL, &vid, &did) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_get_ids("0000:01:00.0", NULL, &did) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_get_ids("0000:01:00.0", &vid, NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_pci_get_ids("invalid", &vid, &did) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL for all");
}

/* ---- High-level API tests ---- */

static void test_load_vfio_driver_null(void)
{
    TEST("load_vfio_driver: NULL/invalid BDF");
    int ok = 1;
    ok &= (vfio_load_vfio_driver(NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_load_vfio_driver("") == VFIO_ERR_INVAL);
    ok &= (vfio_load_vfio_driver("garbage") == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("expected VFIO_ERR_INVAL for all");
}

static void test_open_null_safety(void)
{
    TEST("vfio_open: NULL safety");
    int ok = 1;
    vfio_ctx_t *ctx = NULL;
    ok &= (vfio_open(NULL, "0000:01:00.0") == VFIO_ERR_INVAL);
    ok &= (vfio_open(&ctx, NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_open(&ctx, "") == VFIO_ERR_INVAL);
    ok &= (vfio_open(&ctx, "garbage") == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_close_null_safety(void)
{
    TEST("vfio_close: NULL safety");
    vfio_close(NULL); /* Should not crash */
    PASS();
}

static void test_msi_enable_null_safety(void)
{
    TEST("vfio_msi_enable: NULL/invalid safety");
    int ok = 1;
    ok &= (vfio_msi_enable(NULL, 1) == VFIO_ERR_INVAL);
    /* Intentionally test with uninitialized (zero) context */
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_msi_enable(&dummy, 1) == VFIO_ERR_INVAL);
    /* Zero vectors */
    dummy.initialized = 1;
    ok &= (vfio_msi_enable(&dummy, 0) == VFIO_ERR_INVAL);
    /* Too many vectors */
    ok &= (vfio_msi_enable(&dummy, VFIO_MAX_MSI_VECTORS + 1) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_msi_disable_null_safety(void)
{
    TEST("vfio_msi_disable: NULL/invalid safety");
    int ok = 1;
    ok &= (vfio_msi_disable(NULL) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_msi_disable(&dummy) == VFIO_ERR_INVAL);
    /* Not enabled - should be OK */
    dummy.initialized = 1;
    ok &= (vfio_msi_disable(&dummy) == VFIO_OK);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_handle_interrupt_null_safety(void)
{
    TEST("vfio_handle_interrupt: NULL/invalid safety");
    int ok = 1;
    ok &= (vfio_handle_interrupt(NULL, 0) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_handle_interrupt(&dummy, 0) == VFIO_ERR_INVAL);
    /* Initialized but MSI not enabled */
    dummy.initialized = 1;
    ok &= (vfio_handle_interrupt(&dummy, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_dma_map_null_safety(void)
{
    TEST("vfio_dma_map: NULL/invalid safety");
    int ok = 1;
    vfio_dma_t dma;
    memset(&dma, 0, sizeof(dma));
    dma.vaddr = (void *)(uintptr_t)0x1000;
    dma.size = 4096;
    ok &= (vfio_dma_map(NULL, &dma, 0) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_dma_map(&dummy, &dma, 0) == VFIO_ERR_INVAL);
    dummy.initialized = 1;
    ok &= (vfio_dma_map(&dummy, NULL, 0) == VFIO_ERR_INVAL);
    /* dma with NULL vaddr */
    vfio_dma_t dma_null;
    memset(&dma_null, 0, sizeof(dma_null));
    dma_null.size = 4096;
    ok &= (vfio_dma_map(&dummy, &dma_null, 0) == VFIO_ERR_INVAL);
    /* dma with zero size */
    vfio_dma_t dma_zero;
    memset(&dma_zero, 0, sizeof(dma_zero));
    dma_zero.vaddr = (void *)(uintptr_t)0x1000;
    ok &= (vfio_dma_map(&dummy, &dma_zero, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_dma_unmap_null_safety(void)
{
    TEST("vfio_dma_unmap: NULL/invalid safety");
    int ok = 1;
    vfio_dma_t dma;
    memset(&dma, 0, sizeof(dma));
    ok &= (vfio_dma_unmap(NULL, &dma) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_dma_unmap(&dummy, &dma) == VFIO_ERR_INVAL);
    dummy.initialized = 1;
    ok &= (vfio_dma_unmap(&dummy, NULL) == VFIO_ERR_INVAL);
    /* dma with NULL vaddr */
    ok &= (vfio_dma_unmap(&dummy, &dma) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_dma_alloc_map_null_safety(void)
{
    TEST("vfio_dma_alloc_map: NULL/invalid safety");
    int ok = 1;
    vfio_dma_t dma;
    ok &= (vfio_dma_alloc_map(NULL, &dma, 4096, 0) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_dma_alloc_map(&dummy, &dma, 4096, 0) == VFIO_ERR_INVAL);
    dummy.initialized = 1;
    ok &= (vfio_dma_alloc_map(&dummy, NULL, 4096, 0) == VFIO_ERR_INVAL);
    ok &= (vfio_dma_alloc_map(&dummy, &dma, 0, 0) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_dma_free_unmap_null_safety(void)
{
    TEST("vfio_dma_free_unmap: NULL/invalid safety");
    int ok = 1;
    vfio_dma_t dma;
    memset(&dma, 0, sizeof(dma));
    ok &= (vfio_dma_free_unmap(NULL, &dma) == VFIO_ERR_INVAL);
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_dma_free_unmap(&dummy, &dma) == VFIO_ERR_INVAL);
    dummy.initialized = 1;
    ok &= (vfio_dma_free_unmap(&dummy, NULL) == VFIO_ERR_INVAL);
    ok &= (vfio_dma_free_unmap(&dummy, &dma) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

static void test_is_bound_to_vfio_invalid(void)
{
    TEST("is_bound_to_vfio: invalid/non-existent BDF");
    int ok = 1;
    ok &= (vfio_is_bound_to_vfio(NULL) == 0);
    ok &= (vfio_is_bound_to_vfio("garbage") == 0);
    ok &= (vfio_is_bound_to_vfio("0000:ff:1f.7") == 0);
    if (ok)
        PASS();
    else
        FAIL("expected 0 for all");
}

static void test_msi_get_config_null_safety(void)
{
    TEST("vfio_msi_get_config: NULL/invalid safety");
    int ok = 1;
    vfio_msi_config_t config;
    ok &= (vfio_msi_get_config(NULL, &config) == VFIO_ERR_INVAL);
    /* Uninitialized context */
    vfio_ctx_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    ok &= (vfio_msi_get_config(&dummy, &config) == VFIO_ERR_INVAL);
    /* Initialized but MSI not enabled */
    dummy.initialized = 1;
    ok &= (vfio_msi_get_config(&dummy, &config) == VFIO_ERR_INVAL);
    /* NULL config output */
    dummy.msi_enabled = 1;
    ok &= (vfio_msi_get_config(&dummy, NULL) == VFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL/invalid safety failed");
}

/* ---- Run all tests ---- */

int main(void)
{
    printf("easy_vfio unit tests\n");
    printf("====================\n\n");

    printf("[BDF Validation]\n");
    test_bdf_valid_full_format();
    test_bdf_valid_short_format();
    test_bdf_valid_various();
    test_bdf_invalid_null();
    test_bdf_invalid_empty();
    test_bdf_invalid_garbage();
    test_bdf_invalid_out_of_range();
    test_bdf_invalid_trailing();

    printf("\n[Error Strings]\n");
    test_strerror_known();
    test_strerror_unknown();

    printf("\n[NULL Safety]\n");
    test_container_null_safety();
    test_container_invalid_fd();
    test_group_null_safety();
    test_device_null_safety();
    test_device_invalid_bdf();
    test_region_null_safety();
    test_dma_null_safety();
    test_irq_null_safety();
    test_pci_config_null_safety();

    printf("\n[Utility Functions]\n");
    test_iommu_group_invalid_bdf();
    test_bind_invalid_bdf();
    test_unbind_invalid_bdf();
    test_pci_get_ids_invalid();

    printf("\n[High-Level API]\n");
    test_load_vfio_driver_null();
    test_open_null_safety();
    test_close_null_safety();
    test_msi_enable_null_safety();
    test_msi_disable_null_safety();
    test_handle_interrupt_null_safety();
    test_dma_map_null_safety();
    test_dma_unmap_null_safety();
    test_dma_alloc_map_null_safety();
    test_dma_free_unmap_null_safety();
    test_is_bound_to_vfio_invalid();
    test_msi_get_config_null_safety();

    printf("\n====================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
