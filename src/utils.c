/*
 * easy_vfio - Utility functions
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

#include "easy_vfio.h"

/*
 * Validate a PCI BDF address string.
 * Expected formats: "DDDD:BB:DD.F" (full) or "BB:DD.F" (short, assumes domain 0000).
 * Each hex digit group must be within valid range.
 */
int evfio_bdf_valid(const char *bdf)
{
    unsigned int domain, bus, dev, func;
    int n;

    if (!bdf)
        return 0;

    /* Try full format: DDDD:BB:DD.F */
    n = 0;
    if (sscanf(bdf, "%x:%x:%x.%x%n", &domain, &bus, &dev, &func, &n) == 4 &&
        bdf[n] == '\0') {
        if (domain > 0xFFFF || bus > 0xFF || dev > 0x1F || func > 0x7)
            return 0;
        /* Verify format has correct separators */
        const char *p = bdf;
        /* Must have exactly the right pattern */
        while (*p && isxdigit((unsigned char)*p)) p++;
        if (*p != ':') return 0;
        return 1;
    }

    /* Try short format: BB:DD.F */
    n = 0;
    if (sscanf(bdf, "%x:%x.%x%n", &bus, &dev, &func, &n) == 3 &&
        bdf[n] == '\0') {
        if (bus > 0xFF || dev > 0x1F || func > 0x7)
            return 0;
        return 1;
    }

    return 0;
}

int evfio_get_iommu_group(const char *bdf)
{
    char link_path[PATH_MAX];
    char resolved[PATH_MAX];
    char *group_str;
    long group_id;
    char *endptr;

    if (!bdf || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    snprintf(link_path, sizeof(link_path),
             "/sys/bus/pci/devices/%s/iommu_group", bdf);

    if (realpath(link_path, resolved) == NULL)
        return EVFIO_ERR_NOGROUP;

    /* Extract the group number from the path (last component) */
    group_str = strrchr(resolved, '/');
    if (!group_str)
        return EVFIO_ERR_NOGROUP;

    group_str++; /* skip the '/' */

    errno = 0;
    group_id = strtol(group_str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || group_id < 0 || group_id > INT_MAX)
        return EVFIO_ERR_NOGROUP;

    return (int)group_id;
}

/*
 * Write a string to a sysfs file. Returns 0 on success, negative on failure.
 */
static int sysfs_write(const char *path, const char *value)
{
    FILE *fp;

    fp = fopen(path, "w");
    if (!fp)
        return EVFIO_ERR_PERM;

    if (fprintf(fp, "%s", value) < 0) {
        fclose(fp);
        return EVFIO_ERR_IOCTL;
    }

    fclose(fp);
    return EVFIO_OK;
}

/*
 * Read a line from a sysfs file. Returns 0 on success, negative on failure.
 */
static int sysfs_read(const char *path, char *buf, size_t buflen)
{
    FILE *fp;

    fp = fopen(path, "r");
    if (!fp)
        return EVFIO_ERR_OPEN;

    if (!fgets(buf, (int)buflen, fp)) {
        fclose(fp);
        return EVFIO_ERR_IOCTL;
    }

    fclose(fp);

    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return EVFIO_OK;
}

int evfio_bind_device(const char *bdf)
{
    char path[PATH_MAX];
    int ret;

    if (!bdf || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    /* First unbind from current driver */
    ret = evfio_unbind_device(bdf);
    /* Ignore error - device may not be bound to anything */
    (void)ret;

    /* Write vendor/device id to vfio-pci new_id to make it claim the device */
    uint16_t vendor_id, device_id;
    ret = evfio_pci_get_ids(bdf, &vendor_id, &device_id);
    if (ret != EVFIO_OK)
        return ret;

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%04x %04x", vendor_id, device_id);

    ret = sysfs_write("/sys/bus/pci/drivers/vfio-pci/new_id", id_str);
    if (ret != EVFIO_OK) {
        /* If new_id fails, the driver may already know about this ID.
         * Try binding directly. */
        snprintf(path, sizeof(path),
                 "/sys/bus/pci/drivers/vfio-pci/bind");
        ret = sysfs_write(path, bdf);
        if (ret != EVFIO_OK)
            return ret;
    }

    return EVFIO_OK;
}

int evfio_unbind_device(const char *bdf)
{
    char path[PATH_MAX];
    char driver_link[PATH_MAX];

    if (!bdf || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    /* Check if device is currently bound to a driver */
    snprintf(driver_link, sizeof(driver_link),
             "/sys/bus/pci/devices/%s/driver", bdf);

    if (access(driver_link, F_OK) != 0)
        return EVFIO_OK; /* Not bound to any driver */

    /* Unbind from current driver */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/driver/unbind", bdf);

    return sysfs_write(path, bdf);
}

int evfio_pci_get_ids(const char *bdf, uint16_t *vendor_id,
                      uint16_t *device_id)
{
    char path[PATH_MAX];
    char buf[32];
    int ret;
    unsigned long val;
    char *endptr;

    if (!bdf || !vendor_id || !device_id || !evfio_bdf_valid(bdf))
        return EVFIO_ERR_INVAL;

    /* Read vendor ID */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/vendor", bdf);
    ret = sysfs_read(path, buf, sizeof(buf));
    if (ret != EVFIO_OK)
        return ret;

    errno = 0;
    val = strtoul(buf, &endptr, 0);
    if (errno != 0 || *endptr != '\0' || val > 0xFFFF)
        return EVFIO_ERR_INVAL;
    *vendor_id = (uint16_t)val;

    /* Read device ID */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/device", bdf);
    ret = sysfs_read(path, buf, sizeof(buf));
    if (ret != EVFIO_OK)
        return ret;

    errno = 0;
    val = strtoul(buf, &endptr, 0);
    if (errno != 0 || *endptr != '\0' || val > 0xFFFF)
        return EVFIO_ERR_INVAL;
    *device_id = (uint16_t)val;

    return EVFIO_OK;
}

const char *evfio_strerror(int err)
{
    switch (err) {
    case EVFIO_OK:          return "Success";
    case EVFIO_ERR_INVAL:   return "Invalid argument";
    case EVFIO_ERR_OPEN:    return "Failed to open file or device";
    case EVFIO_ERR_IOCTL:   return "ioctl failed";
    case EVFIO_ERR_MMAP:    return "mmap failed";
    case EVFIO_ERR_ALLOC:   return "Memory allocation failed";
    case EVFIO_ERR_PERM:    return "Permission denied";
    case EVFIO_ERR_NOGROUP: return "IOMMU group not found";
    case EVFIO_ERR_NOTVIABLE: return "VFIO group not viable";
    case EVFIO_ERR_BUSY:    return "Resource busy";
    case EVFIO_ERR_NOSYS:   return "Not supported";
    default:                return "Unknown error";
    }
}
