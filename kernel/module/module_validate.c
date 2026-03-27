#include "filesystem/vfs/vfs.h"
#include "log.h"
#include "module/module_format.h"
#include "utils.h"

/**
 * Read an exact byte count from a file descriptor.
 *
 * Args:
 *   fd: Open file descriptor to read from.
 *   buffer: Destination buffer for the requested bytes.
 *   size: Number of bytes to read.
 *
 * Behavior:
 *   Wraps `vfs_fd_read` and converts short reads into a uniform failure code.
 *
 * Returns:
 *   `0` on success, or `-1` when the read was short or invalid.
 */
static int module_read_exact(struct FileDesc* fd, void* buffer, unsigned int size) {
    return vfs_fd_read(fd, buffer, size) == (int)size ? 0 : -1;
}

/**
 * Read a module bundle header from the start of a file.
 *
 * Args:
 *   fd: Open module file descriptor.
 *   header: Destination bundle header buffer.
 *
 * Behavior:
 *   Seeks to offset zero before reading so callers can reuse descriptors that
 *   have already been advanced.
 *
 * Returns:
 *   `0` on success, or `-1` on seek/read failure.
 */
int module_read_header(struct FileDesc* fd, ModuleBundleHeader* header) {
    if (!fd || !header) {
        return -1;
    }
    // Always restart from the beginning so header parsing is deterministic.
    if (vfs_fd_seek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    return module_read_exact(fd, header, sizeof(*header));
}

/**
 * Check whether a value is a power of two.
 *
 * Args:
 *   value: Candidate value to test.
 *
 * Behavior:
 *   Rejects zero explicitly because the bit trick alone would treat it as a
 *   false positive.
 *
 * Returns:
 *   `true` when the value is a non-zero power of two, otherwise `false`.
 */
static Bool module_is_power_of_two(UInt value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

/**
 * Validate the top-level bundle header of a packed `.sys` image.
 *
 * Args:
 *   header: Parsed bundle header to verify.
 *
 * Behavior:
 *   Verifies magic numbers, versioning, payload sizes, lifecycle callback
 *   metadata, and export table bounds before the loader touches the payload.
 *
 * Returns:
 *   `0` when the bundle metadata is structurally valid, or `-1` when any field
 *   is inconsistent.
 */
int module_validate_header(const ModuleBundleHeader* header) {
    UInt index;

    if (!header) {
        return -1;
    }
    if (header->magic[0] != MODULE_BUNDLE_MAGIC_0 ||
        header->magic[1] != MODULE_BUNDLE_MAGIC_1 ||
        header->magic[2] != MODULE_BUNDLE_MAGIC_2 ||
        header->magic[3] != MODULE_BUNDLE_MAGIC_3 ||
        header->magic[4] != MODULE_BUNDLE_MAGIC_4 ||
        header->magic[5] != MODULE_BUNDLE_MAGIC_5 ||
        header->magic[6] != MODULE_BUNDLE_MAGIC_6 ||
        header->magic[7] != MODULE_BUNDLE_MAGIC_7) {
        return -1;
    }
    if (header->header_size != MODULE_BUNDLE_HEADER_SIZE) {
        return -1;
    }
    if (header->version != MODULE_BUNDLE_VERSION) {
        return -1;
    }
    if (header->abi_version != MODULE_BUNDLE_VERSION) {
        return -1;
    }
    if (!module_is_power_of_two(header->payload_align)) {
        return -1;
    }
    if (header->manifest_size == 0 || header->module_size == 0) {
        return -1;
    }
    if (header->name[0] == '\0') {
        return -1;
    }
    if (header->init_section == MOD_INVALID_SECTION) {
        return -1;
    }
    if ((header->shutdown_section == MOD_INVALID_SECTION) != (header->shutdown_offset == 0)) {
        return -1;
    }
    if ((header->idle_section == MOD_INVALID_SECTION) != (header->idle_offset == 0)) {
        return -1;
    }
    if (header->export_count > MODULE_BUNDLE_MAX_EXPORTS) {
        return -1;
    }
    // Validate only populated export entries and ignore the padded tail.
    for (index = 0; index < header->export_count; index++) {
        if (header->exports[index].name[0] == '\0') {
            return -1;
        }
        if (header->exports[index].section == MOD_INVALID_SECTION) {
            return -1;
        }
    }
    return 0;
}
