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

#include "vfio_internal.h"

/*
 * Validate a PCI BDF address string.
 * Expected formats: "DDDD:BB:DD.F" (full) or "BB:DD.F" (short, assumes domain 0000).
 * Each hex digit group must be within valid range.
 */
int vfio_bdf_valid(const char *bdf)
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

int vfio_get_iommu_group(const char *bdf)
{
    char link_path[PATH_MAX];
    char resolved[PATH_MAX];
    char *group_str;
    long group_id;
    char *endptr;

    if (!bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    snprintf(link_path, sizeof(link_path),
             "/sys/bus/pci/devices/%s/iommu_group", bdf);

    if (realpath(link_path, resolved) == NULL)
        return VFIO_ERR_NOGROUP;

    /* Extract the group number from the path (last component) */
    group_str = strrchr(resolved, '/');
    if (!group_str)
        return VFIO_ERR_NOGROUP;

    group_str++; /* skip the '/' */

    errno = 0;
    group_id = strtol(group_str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || group_id < 0 || group_id > INT_MAX)
        return VFIO_ERR_NOGROUP;

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
        return VFIO_ERR_PERM;

    if (fprintf(fp, "%s", value) < 0) {
        fclose(fp);
        return VFIO_ERR_IOCTL;
    }

    fclose(fp);
    return VFIO_OK;
}

/*
 * Read a line from a sysfs file. Returns 0 on success, negative on failure.
 */
static int sysfs_read(const char *path, char *buf, size_t buflen)
{
    FILE *fp;

    fp = fopen(path, "r");
    if (!fp)
        return VFIO_ERR_OPEN;

    if (!fgets(buf, (int)buflen, fp)) {
        fclose(fp);
        return VFIO_ERR_IOCTL;
    }

    fclose(fp);

    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return VFIO_OK;
}

int vfio_bind_device(const char *bdf)
{
    char path[PATH_MAX];
    int ret;

    if (!bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* First unbind from current driver */
    ret = vfio_unbind_device(bdf);
    /* Ignore error - device may not be bound to anything */
    (void)ret;

    /* Write vendor/device id to vfio-pci new_id to make it claim the device */
    uint16_t vendor_id, device_id;
    ret = vfio_pci_get_ids(bdf, &vendor_id, &device_id);
    if (ret != VFIO_OK)
        return ret;

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%04x %04x", vendor_id, device_id);

    ret = sysfs_write("/sys/bus/pci/drivers/vfio-pci/new_id", id_str);
    if (ret != VFIO_OK) {
        /* If new_id fails, the driver may already know about this ID.
         * Try binding directly. */
        snprintf(path, sizeof(path),
                 "/sys/bus/pci/drivers/vfio-pci/bind");
        ret = sysfs_write(path, bdf);
        if (ret != VFIO_OK)
            return ret;
    }

    return VFIO_OK;
}

int vfio_unbind_device(const char *bdf)
{
    char path[PATH_MAX];
    char driver_link[PATH_MAX];

    if (!bdf || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* Check if device is currently bound to a driver */
    snprintf(driver_link, sizeof(driver_link),
             "/sys/bus/pci/devices/%s/driver", bdf);

    if (access(driver_link, F_OK) != 0)
        return VFIO_OK; /* Not bound to any driver */

    /* Unbind from current driver */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/driver/unbind", bdf);

    return sysfs_write(path, bdf);
}

int vfio_pci_get_ids(const char *bdf, uint16_t *vendor_id,
                      uint16_t *device_id)
{
    char path[PATH_MAX];
    char buf[32];
    int ret;
    unsigned long val;
    char *endptr;

    if (!bdf || !vendor_id || !device_id || !vfio_bdf_valid(bdf))
        return VFIO_ERR_INVAL;

    /* Read vendor ID */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/vendor", bdf);
    ret = sysfs_read(path, buf, sizeof(buf));
    if (ret != VFIO_OK)
        return ret;

    errno = 0;
    val = strtoul(buf, &endptr, 0);
    if (errno != 0 || *endptr != '\0' || val > 0xFFFF)
        return VFIO_ERR_INVAL;
    *vendor_id = (uint16_t)val;

    /* Read device ID */
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%s/device", bdf);
    ret = sysfs_read(path, buf, sizeof(buf));
    if (ret != VFIO_OK)
        return ret;

    errno = 0;
    val = strtoul(buf, &endptr, 0);
    if (errno != 0 || *endptr != '\0' || val > 0xFFFF)
        return VFIO_ERR_INVAL;
    *device_id = (uint16_t)val;

    return VFIO_OK;
}

int vfio_is_bound_to_vfio(const char *bdf)
{
    char driver_link[PATH_MAX];
    char resolved[PATH_MAX];
    char *driver_name;

    if (!bdf || !vfio_bdf_valid(bdf))
        return 0;

    snprintf(driver_link, sizeof(driver_link),
             "/sys/bus/pci/devices/%s/driver", bdf);

    if (realpath(driver_link, resolved) == NULL)
        return 0; /* Not bound to any driver */

    driver_name = strrchr(resolved, '/');
    if (!driver_name)
        return 0;

    driver_name++; /* Skip '/' */

    return (strcmp(driver_name, "vfio-pci") == 0);
}

const char *vfio_strerror(int err)
{
    switch (err) {
    case VFIO_OK:          return "Success";
    case VFIO_ERR_INVAL:   return "Invalid argument";
    case VFIO_ERR_OPEN:    return "Failed to open file or device";
    case VFIO_ERR_IOCTL:   return "ioctl failed";
    case VFIO_ERR_MMAP:    return "mmap failed";
    case VFIO_ERR_ALLOC:   return "Memory allocation failed";
    case VFIO_ERR_PERM:    return "Permission denied";
    case VFIO_ERR_NOGROUP: return "IOMMU group not found";
    case VFIO_ERR_NOTVIABLE: return "VFIO group not viable";
    case VFIO_ERR_BUSY:    return "Resource busy";
    case VFIO_ERR_NOSYS:   return "Not supported";
    default:                return "Unknown error";
    }
}
