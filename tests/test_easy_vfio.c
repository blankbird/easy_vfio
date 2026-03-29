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

#include "easy_vfio.h"

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
    if (evfio_bdf_valid("0000:01:00.0"))
        PASS();
    else
        FAIL("expected valid");
}

static void test_bdf_valid_short_format(void)
{
    TEST("bdf_valid: short format (01:00.0)");
    if (evfio_bdf_valid("01:00.0"))
        PASS();
    else
        FAIL("expected valid");
}

static void test_bdf_valid_various(void)
{
    TEST("bdf_valid: various valid addresses");
    int ok = 1;
    ok &= evfio_bdf_valid("0000:00:00.0");
    ok &= evfio_bdf_valid("ffff:ff:1f.7");
    ok &= evfio_bdf_valid("0000:03:00.1");
    ok &= evfio_bdf_valid("00:1f.3");
    if (ok)
        PASS();
    else
        FAIL("expected all valid");
}

static void test_bdf_invalid_null(void)
{
    TEST("bdf_valid: NULL pointer");
    if (!evfio_bdf_valid(NULL))
        PASS();
    else
        FAIL("expected invalid");
}

static void test_bdf_invalid_empty(void)
{
    TEST("bdf_valid: empty string");
    if (!evfio_bdf_valid(""))
        PASS();
    else
        FAIL("expected invalid");
}

static void test_bdf_invalid_garbage(void)
{
    TEST("bdf_valid: garbage input");
    int ok = 1;
    ok &= !evfio_bdf_valid("hello");
    ok &= !evfio_bdf_valid("xx:yy.z");
    ok &= !evfio_bdf_valid("0000-01-00.0");
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
    ok &= !evfio_bdf_valid("0000:00:20.0");
    /* function > 7 */
    ok &= !evfio_bdf_valid("0000:00:00.8");
    if (ok)
        PASS();
    else
        FAIL("expected all invalid");
}

static void test_bdf_invalid_trailing(void)
{
    TEST("bdf_valid: trailing characters");
    if (!evfio_bdf_valid("0000:01:00.0extra"))
        PASS();
    else
        FAIL("expected invalid");
}

/* ---- Error string tests ---- */

static void test_strerror_known(void)
{
    TEST("strerror: known error codes");
    int ok = 1;
    ok &= (strcmp(evfio_strerror(EVFIO_OK), "Success") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_INVAL), "Invalid argument") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_OPEN), "Failed to open file or device") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_IOCTL), "ioctl failed") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_MMAP), "mmap failed") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_ALLOC), "Memory allocation failed") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_PERM), "Permission denied") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_NOGROUP), "IOMMU group not found") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_NOTVIABLE), "VFIO group not viable") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_BUSY), "Resource busy") == 0);
    ok &= (strcmp(evfio_strerror(EVFIO_ERR_NOSYS), "Not supported") == 0);
    if (ok)
        PASS();
    else
        FAIL("unexpected error string");
}

static void test_strerror_unknown(void)
{
    TEST("strerror: unknown error code");
    if (strcmp(evfio_strerror(-999), "Unknown error") == 0)
        PASS();
    else
        FAIL("expected 'Unknown error'");
}

/* ---- NULL-safety tests for API functions ---- */

static void test_container_null_safety(void)
{
    TEST("container: NULL safety");
    int ok = 1;
    ok &= (evfio_container_open(NULL) == EVFIO_ERR_INVAL);
    evfio_container_close(NULL); /* Should not crash */
    ok &= (evfio_container_set_iommu(NULL, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_container_check_extension(NULL, 0) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_container_invalid_fd(void)
{
    TEST("container: invalid fd safety");
    evfio_container_t c = { .fd = -1 };
    int ok = 1;
    ok &= (evfio_container_set_iommu(&c, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_container_check_extension(&c, 0) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("invalid fd safety failed");
}

static void test_group_null_safety(void)
{
    TEST("group: NULL safety");
    int ok = 1;
    evfio_container_t c = { .fd = 999 };
    ok &= (evfio_group_open(NULL, &c, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_group_open((evfio_group_t[1]){}, NULL, 0) == EVFIO_ERR_INVAL);
    evfio_group_close(NULL); /* Should not crash */
    ok &= (evfio_group_is_viable(NULL) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_device_null_safety(void)
{
    TEST("device: NULL safety");
    int ok = 1;
    evfio_group_t g = { .fd = 999 };
    ok &= (evfio_device_open(NULL, &g, "0000:01:00.0") == EVFIO_ERR_INVAL);
    ok &= (evfio_device_open((evfio_device_t[1]){}, NULL, "0000:01:00.0") == EVFIO_ERR_INVAL);
    ok &= (evfio_device_open((evfio_device_t[1]){}, &g, NULL) == EVFIO_ERR_INVAL);
    evfio_device_close(NULL); /* Should not crash */
    ok &= (evfio_device_reset(NULL) == EVFIO_ERR_INVAL);
    ok &= (evfio_device_get_region_info(NULL, 0, NULL) == EVFIO_ERR_INVAL);
    ok &= (evfio_device_get_irq_info(NULL, 0, NULL) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_device_invalid_bdf(void)
{
    TEST("device: invalid BDF rejected");
    evfio_device_t dev;
    evfio_group_t g = { .fd = 999 };
    if (evfio_device_open(&dev, &g, "invalid") == EVFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected EVFIO_ERR_INVAL");
}

static void test_region_null_safety(void)
{
    TEST("region: NULL safety");
    int ok = 1;
    evfio_device_t d = { .fd = 999 };
    ok &= (evfio_region_map(NULL, &d, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_region_map((evfio_region_t[1]){}, NULL, 0) == EVFIO_ERR_INVAL);
    evfio_region_unmap(NULL); /* Should not crash */
    ok &= (evfio_region_read(NULL, 0, (char[1]){}, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_region_read(&d, 0, NULL, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_region_write(NULL, 0, (char[1]){}, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_region_write(&d, 0, NULL, 1, 0) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_dma_null_safety(void)
{
    TEST("dma: NULL safety");
    int ok = 1;
    evfio_container_t c = { .fd = 999 };
    evfio_dma_t dma;
    ok &= (evfio_dma_map(NULL, &dma, 4096, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_dma_map(&c, NULL, 4096, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_dma_map(&c, &dma, 0, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_dma_unmap(NULL, &dma) == EVFIO_ERR_INVAL);
    ok &= (evfio_dma_unmap(&c, NULL) == EVFIO_ERR_INVAL);
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
    ok &= (evfio_irq_enable(NULL, 0, &dummy_fd, 1) == EVFIO_ERR_INVAL);
    ok &= (evfio_irq_disable(NULL, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_irq_unmask_intx(NULL) == EVFIO_ERR_INVAL);

    evfio_device_t d = { .fd = 999 };
    ok &= (evfio_irq_enable(&d, 0, NULL, 1) == EVFIO_ERR_INVAL);
    ok &= (evfio_irq_enable(&d, 0, &dummy_fd, 0) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_pci_config_null_safety(void)
{
    TEST("pci_config: NULL safety");
    int ok = 1;
    evfio_device_t d = { .fd = 999 };
    ok &= (evfio_pci_config_read(NULL, (char[1]){}, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_config_read(&d, NULL, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_config_read(&d, (char[1]){}, 0, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_config_write(NULL, (char[1]){}, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_config_write(&d, NULL, 1, 0) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_config_write(&d, (char[1]){}, 0, 0) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("NULL safety failed");
}

static void test_iommu_group_invalid_bdf(void)
{
    TEST("get_iommu_group: invalid BDF");
    if (evfio_get_iommu_group("garbage") == EVFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected EVFIO_ERR_INVAL");
}

static void test_bind_invalid_bdf(void)
{
    TEST("bind_device: invalid BDF");
    if (evfio_bind_device("invalid") == EVFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected EVFIO_ERR_INVAL");
}

static void test_unbind_invalid_bdf(void)
{
    TEST("unbind_device: invalid BDF");
    if (evfio_unbind_device("invalid") == EVFIO_ERR_INVAL)
        PASS();
    else
        FAIL("expected EVFIO_ERR_INVAL");
}

static void test_pci_get_ids_invalid(void)
{
    TEST("pci_get_ids: invalid arguments");
    uint16_t vid, did;
    int ok = 1;
    ok &= (evfio_pci_get_ids(NULL, &vid, &did) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_get_ids("0000:01:00.0", NULL, &did) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_get_ids("0000:01:00.0", &vid, NULL) == EVFIO_ERR_INVAL);
    ok &= (evfio_pci_get_ids("invalid", &vid, &did) == EVFIO_ERR_INVAL);
    if (ok)
        PASS();
    else
        FAIL("expected EVFIO_ERR_INVAL for all");
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

    printf("\n====================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
