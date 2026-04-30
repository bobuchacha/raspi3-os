#include "filesystem/fat32.h"

#include "block_io.h"
#include "device.h"

namespace filesystem {

    namespace {

        enum : U8 {
            Fat32DirEntryUnused = 0x00U,
            Fat32DirEntryDeleted = 0xE5U,
            Fat32AttributeReadOnly = 0x01U,
            Fat32AttributeHidden = 0x02U,
            Fat32AttributeSystem = 0x04U,
            Fat32AttributeVolumeId = 0x08U,
            Fat32AttributeDirectory = 0x10U,
            Fat32AttributeArchive = 0x20U,
            Fat32AttributeLongName = 0x0FU,
        };

        enum : U32 {
            Fat32ClusterFree = 0x00000000U,
            Fat32ClusterBad = 0x0FFFFFF7U,
            Fat32ClusterEnd = 0x0FFFFFF8U,
            Fat32ClusterMask = 0x0FFFFFFFU,
            Fat32PartitionFat32Chs = 0x0BU,
            Fat32PartitionFat32Lba = 0x0CU,
            Fat32BootBytesPerSectorOffset = 11U,
            Fat32BootSectorsPerClusterOffset = 13U,
            Fat32BootReservedSectorsOffset = 14U,
            Fat32BootFatCountOffset = 16U,
            Fat32BootTotalSectors16Offset = 19U,
            Fat32BootTotalSectors32Offset = 32U,
            Fat32BootFatSize32Offset = 36U,
            Fat32BootRootClusterOffset = 44U,
            Fat32BootFsTypeOffset = 82U,
            Fat32DirAttributesOffset = 11U,
            Fat32DirClusterHiOffset = 20U,
            Fat32DirClusterLoOffset = 26U,
            Fat32DirSizeOffset = 28U,
            Fat32LfnOrderOffset = 0U,
            Fat32LfnName1Offset = 1U,
            Fat32LfnChecksumOffset = 13U,
            Fat32LfnName2Offset = 14U,
            Fat32LfnName3Offset = 28U,
            Fat32MbrEntrySize = 16U,
            Fat32MbrEntryTypeOffset = 4U,
            Fat32MbrEntryLbaOffset = 8U,
        };

        enum : Size {
            Fat32NodePrivateEntrySectorLba = 0U,
            Fat32NodePrivateEntrySectorOffset = 1U,
        };

        static constexpr Size Fat32SectorSize = 512U;
        static constexpr Size Fat32DirectoryEntrySize = 32U;
        inline constexpr Size Fat32AsyncReadThreshold = 4U * Fat32SectorSize;
        static constexpr Size Fat32ShortNameSize = 11U;
        static constexpr Size Fat32LongNameCapacity = 256U;
        static constexpr Size Fat32PathCapacity = 260U;

        typedef struct Fat32BootSectorInfo {
            U16 bytes_per_sector;

            U8 sectors_per_cluster;
            U16 reserved_sectors;
            U8 fat_count;
            U32 total_sectors;
            U32 fat_size_32;
            U32 root_cluster;
            char fs_type[8];
        } Fat32BootSectorInfo;

        typedef struct Fat32DirectoryEntry {
            char name[Fat32ShortNameSize];
            U8 attributes;
            U16 cluster_hi;
            U16 cluster_lo;
            U32 size;
        } Fat32DirectoryEntry;

        typedef struct Fat32DirectoryRecord {
            Fat32DirectoryEntry entry;
            char short_name[Fat32LongNameCapacity];
            char long_name[Fat32LongNameCapacity];
            bool has_long_name;
            U64 first_entry_index;
            U64 short_entry_index;
            U16 entry_count;
            U32 short_entry_sector_lba;
            U32 short_entry_sector_offset;
        } Fat32DirectoryRecord;

        typedef struct Fat32LfnState {
            bool active;
            U8 checksum;
            U8 expected_order;
            U16 entry_count;
            U64 first_entry_index;
            char name[Fat32LongNameCapacity];
        } Fat32LfnState;

        typedef struct Fat32MountState {
            bool mounted;
            Device* device;
            U32 partition_lba;
            U32 fat_lba;
            U32 data_lba;
            U32 cluster_size;
            U32 cluster_count;
            Fat32BootSectorInfo boot_sector;
        } Fat32MountState;

        typedef Status(*Fat32DirectoryVisitor)(const Fat32DirectoryRecord* record, void* context);

        typedef struct Fat32LookupContext {
            const char* component;
            Fat32DirectoryRecord* record;
            bool found;
        } Fat32LookupContext;

        typedef struct Fat32EnumerateContext {
            void* user_context;
            FilesystemEnumerateVisitor visitor;
        } Fat32EnumerateContext;

        Fat32MountState g_system_volume_state;

        Status fat32_read_fat_entry(const Fat32MountState* state, U32 cluster, U32* next_cluster_out);

        U16 fat32_read_le16(const U8* bytes) {
            return static_cast<U16>(bytes[0]) | (static_cast<U16>(bytes[1]) << 8);
        }

        U32 fat32_read_le32(const U8* bytes) {
            return static_cast<U32>(bytes[0]) |
                (static_cast<U32>(bytes[1]) << 8) |
                (static_cast<U32>(bytes[2]) << 16) |
                (static_cast<U32>(bytes[3]) << 24);
        }

        void fat32_write_le16(U8* bytes, U16 value) {
            bytes[0] = static_cast<U8>(value & 0xFFU);
            bytes[1] = static_cast<U8>((value >> 8) & 0xFFU);
        }

        void fat32_write_le32(U8* bytes, U32 value) {
            bytes[0] = static_cast<U8>(value & 0xFFU);
            bytes[1] = static_cast<U8>((value >> 8) & 0xFFU);
            bytes[2] = static_cast<U8>((value >> 16) & 0xFFU);
            bytes[3] = static_cast<U8>((value >> 24) & 0xFFU);
        }

        void fat32_memfill(void* buffer, U8 value, Size length) {
            U8* bytes = static_cast<U8*>(buffer);

            for (Size index = 0; index < length; ++index) {
                bytes[index] = value;
            }
        }

        int fat32_ascii_upper(int ch) {
            if ((ch >= 'a') && (ch <= 'z')) {
                return ch - ('a' - 'A');
            }

            return ch;
        }

        int fat32_ascii_lower(int ch) {
            if ((ch >= 'A') && (ch <= 'Z')) {
                return ch + ('a' - 'A');
            }

            return ch;
        }

        bool fat32_same_text_case_insensitive(const char* lhs, const char* rhs) {
            if (lhs == rhs) {
                return true;
            }
            if ((lhs == NULL) || (rhs == NULL)) {
                return false;
            }

            while ((*lhs != '\0') && (*rhs != '\0')) {
                if (fat32_ascii_upper(*lhs) != fat32_ascii_upper(*rhs)) {
                    return false;
                }
                ++lhs;
                ++rhs;
            }

            return *lhs == *rhs;
        }

        Status fat32_read_exact(Device* device, U64 offset, void* buffer, Size length) {
            SSize result;

            if ((device == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }

            /*
             * The worker-backed path only pays off once the transfer is large
             * enough to amortize the extra queue, wake, and wait machinery.
             * FAT32 metadata walks and many boot-time file reads still issue
             * sector-sized exact reads, so keep those on the direct device path
             * until the lower layer can aggregate or run multiple requests.
             */
            if (length <= Fat32AsyncReadThreshold) {
                result = device->read(offset, buffer, length);
            }
            else {
                result = BlockIoService::read(device, offset, buffer, length);
            }
            if (result < 0) {
                return static_cast<Status>(result);
            }
            if (result != static_cast<SSize>(length)) {
                return StatusIoError;
            }

            return StatusOK;
        }

        Status fat32_write_exact(Device* device, U64 offset, const void* buffer, Size length) {
            SSize result;

            if ((device == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }

            result = device->write(offset, buffer, length);
            if (result < 0) {
                return static_cast<Status>(result);
            }
            if (result != static_cast<SSize>(length)) {
                return StatusIoError;
            }

            return StatusOK;
        }

        Status fat32_read_sector(const Fat32MountState* state, U32 lba, void* buffer) {
            if ((state == NULL) || (state->device == NULL)) {
                return StatusInvalidArgument;
            }

            return fat32_read_exact(state->device, static_cast<U64>(lba) * Fat32SectorSize, buffer, Fat32SectorSize);
        }

        Status fat32_write_sector(const Fat32MountState* state, U32 lba, const void* buffer) {
            if ((state == NULL) || (state->device == NULL)) {
                return StatusInvalidArgument;
            }

            return fat32_write_exact(state->device, static_cast<U64>(lba) * Fat32SectorSize, buffer, Fat32SectorSize);
        }

        Status fat32_parse_boot_sector(Fat32MountState* state, const U8* sector) {
            U16 total_sectors_16;
            U32 total_sectors_32;

            if ((state == NULL) || (sector == NULL)) {
                return StatusInvalidArgument;
            }

            state->boot_sector.bytes_per_sector = fat32_read_le16(sector + Fat32BootBytesPerSectorOffset);
            state->boot_sector.sectors_per_cluster = sector[Fat32BootSectorsPerClusterOffset];
            state->boot_sector.reserved_sectors = fat32_read_le16(sector + Fat32BootReservedSectorsOffset);
            state->boot_sector.fat_count = sector[Fat32BootFatCountOffset];
            total_sectors_16 = fat32_read_le16(sector + Fat32BootTotalSectors16Offset);
            total_sectors_32 = fat32_read_le32(sector + Fat32BootTotalSectors32Offset);
            state->boot_sector.total_sectors = (total_sectors_16 != 0U) ? total_sectors_16 : total_sectors_32;
            state->boot_sector.fat_size_32 = fat32_read_le32(sector + Fat32BootFatSize32Offset);
            state->boot_sector.root_cluster = fat32_read_le32(sector + Fat32BootRootClusterOffset);
            for (Size index = 0; index < COUNT_OF(state->boot_sector.fs_type); ++index) {
                state->boot_sector.fs_type[index] = static_cast<char>(sector[Fat32BootFsTypeOffset + index]);
            }

            return StatusOK;
        }

        bool fat32_is_boot_sector(const U8* sector) {
            return (sector != NULL) &&
                (sector[510] == 0x55U) &&
                (sector[511] == 0xAAU) &&
                (fat32_read_le16(sector + Fat32BootBytesPerSectorOffset) == Fat32SectorSize) &&
                (sector[Fat32BootSectorsPerClusterOffset] != 0U) &&
                (sector[Fat32BootFatCountOffset] != 0U) &&
                (fat32_read_le32(sector + Fat32BootFatSize32Offset) != 0U) &&
                (fat32_read_le32(sector + Fat32BootRootClusterOffset) >= 2U) &&
                (sector[Fat32BootFsTypeOffset + 0U] == 'F') &&
                (sector[Fat32BootFsTypeOffset + 1U] == 'A') &&
                (sector[Fat32BootFsTypeOffset + 2U] == 'T') &&
                (sector[Fat32BootFsTypeOffset + 3U] == '3') &&
                (sector[Fat32BootFsTypeOffset + 4U] == '2');
        }

        Status fat32_find_partition_lba(Device* device, U32* partition_lba_out) {
            U8 sector[Fat32SectorSize];

            if ((device == NULL) || (partition_lba_out == NULL)) {
                return StatusInvalidArgument;
            }

            if (fat32_read_exact(device, 0U, sector, sizeof(sector)) != StatusOK) {
                return StatusIoError;
            }
            if (fat32_is_boot_sector(sector)) {
                *partition_lba_out = 0U;
                return StatusOK;
            }

            for (Size index = 0; index < 4U; ++index) {
                const U8* entry = sector + 0x1BEU + (index * Fat32MbrEntrySize);
                U8 type = entry[Fat32MbrEntryTypeOffset];

                if ((type == Fat32PartitionFat32Chs) || (type == Fat32PartitionFat32Lba)) {
                    *partition_lba_out = fat32_read_le32(entry + Fat32MbrEntryLbaOffset);
                    return StatusOK;
                }
            }

            return StatusNotFound;
        }

        U32 fat32_cluster_to_lba(const Fat32MountState* state, U32 cluster) {
            return state->data_lba + ((cluster - 2U) * state->boot_sector.sectors_per_cluster);
        }

        U32 fat32_entry_cluster(const Fat32DirectoryEntry* entry) {
            return (static_cast<U32>(entry->cluster_hi) << 16) | static_cast<U32>(entry->cluster_lo);
        }

        void fat32_set_entry_cluster(Fat32DirectoryEntry* entry, U32 cluster) {
            entry->cluster_hi = static_cast<U16>((cluster >> 16) & 0xFFFFU);
            entry->cluster_lo = static_cast<U16>(cluster & 0xFFFFU);
        }

        void fat32_copy_directory_entry(Fat32DirectoryEntry* entry_out, const U8* entry_bytes) {
            for (Size index = 0; index < Fat32ShortNameSize; ++index) {
                entry_out->name[index] = static_cast<char>(entry_bytes[index]);
            }
            entry_out->attributes = entry_bytes[Fat32DirAttributesOffset];
            entry_out->cluster_hi = fat32_read_le16(entry_bytes + Fat32DirClusterHiOffset);
            entry_out->cluster_lo = fat32_read_le16(entry_bytes + Fat32DirClusterLoOffset);
            entry_out->size = fat32_read_le32(entry_bytes + Fat32DirSizeOffset);
        }

        U8 fat32_short_name_checksum(const char short_name[Fat32ShortNameSize]) {
            U8 checksum = 0U;

            for (Size index = 0; index < Fat32ShortNameSize; ++index) {
                checksum = static_cast<U8>(((checksum & 1U) << 7) + (checksum >> 1) + static_cast<U8>(short_name[index]));
            }

            return checksum;
        }

        void fat32_format_short_name(const Fat32DirectoryEntry* entry, char output[Fat32LongNameCapacity]) {
            Size write_index = 0U;
            Size name_length = 8U;
            Size extension_length = 3U;

            while ((name_length != 0U) && (entry->name[name_length - 1U] == ' ')) {
                --name_length;
            }
            while ((extension_length != 0U) && (entry->name[8U + extension_length - 1U] == ' ')) {
                --extension_length;
            }

            if (name_length == 0U) {
                output[0] = '\0';
                return;
            }

            for (Size index = 0; index < name_length; ++index) {
                output[write_index++] = static_cast<char>(fat32_ascii_lower(entry->name[index]));
            }
            if (extension_length != 0U) {
                output[write_index++] = '.';
                for (Size index = 0; index < extension_length; ++index) {
                    output[write_index++] = static_cast<char>(fat32_ascii_lower(entry->name[8U + index]));
                }
            }

            output[write_index] = '\0';
        }

        void fat32_reset_lfn_state(Fat32LfnState* state) {
            if (state != NULL) {
                memzero(state, sizeof(*state));
            }
        }

        Size fat32_string_length(const char* text) {
            Size length = 0U;

            if (text == NULL) {
                return 0U;
            }

            while (text[length] != '\0') {
                ++length;
            }

            return length;
        }

        char fat32_decode_lfn_character(U16 code_unit) {
            if ((code_unit == 0x0000U) || (code_unit == 0xFFFFU)) {
                return '\0';
            }
            if (code_unit <= 0x7FU) {
                return static_cast<char>(code_unit);
            }

            // The early kernel path interface is byte-oriented, so keep non-ASCII names visible
            // without pretending to offer a full UTF-16 to UTF-8 conversion layer yet.
            return '?';
        }

        void fat32_capture_lfn_field(Fat32LfnState* state, Size base_index, const U8* entry_bytes, Size field_offset, Size field_length) {
            for (Size offset = 0U; offset < field_length; offset += 2U) {
                Size character_index = base_index + (offset / 2U);
                U16 code_unit;
                char decoded;

                if ((character_index + 1U) >= Fat32LongNameCapacity) {
                    return;
                }

                code_unit = fat32_read_le16(entry_bytes + field_offset + offset);
                decoded = fat32_decode_lfn_character(code_unit);
                if (decoded == '\0') {
                    continue;
                }

                state->name[character_index] = decoded;
            }
        }

        void fat32_capture_lfn_entry(Fat32LfnState* state, const U8* entry_bytes, U64 entry_index) {
            U8 order = entry_bytes[Fat32LfnOrderOffset] & 0x1FU;
            bool starts_sequence = (entry_bytes[Fat32LfnOrderOffset] & 0x40U) != 0U;
            Size base_index;

            if ((state == NULL) || (entry_bytes == NULL) || (order == 0U)) {
                return;
            }

            if (starts_sequence) {
                fat32_reset_lfn_state(state);
                state->active = true;
                state->checksum = entry_bytes[Fat32LfnChecksumOffset];
                state->expected_order = order;
                state->first_entry_index = entry_index;
            }

            if (!state->active || (state->expected_order != order) || (state->checksum != entry_bytes[Fat32LfnChecksumOffset])) {
                fat32_reset_lfn_state(state);
                return;
            }

            base_index = static_cast<Size>(order - 1U) * 13U;
            fat32_capture_lfn_field(state, base_index, entry_bytes, Fat32LfnName1Offset, 10U);
            fat32_capture_lfn_field(state, base_index + 5U, entry_bytes, Fat32LfnName2Offset, 12U);
            fat32_capture_lfn_field(state, base_index + 11U, entry_bytes, Fat32LfnName3Offset, 4U);

            if (state->expected_order != 0U) {
                --state->expected_order;
            }
            ++state->entry_count;
        }

        bool fat32_lfn_matches_entry(const Fat32LfnState* lfn_state, const Fat32DirectoryEntry* entry) {
            if ((lfn_state == NULL) || (entry == NULL)) {
                return false;
            }
            if (!lfn_state->active || (lfn_state->expected_order != 0U) || (lfn_state->name[0] == '\0')) {
                return false;
            }

            return lfn_state->checksum == fat32_short_name_checksum(entry->name);
        }

        const char* fat32_record_display_name(const Fat32DirectoryRecord* record) {
            if ((record != NULL) && record->has_long_name && (record->long_name[0] != '\0')) {
                return record->long_name;
            }

            return (record != NULL) ? record->short_name : "";
        }

        bool fat32_record_is_relative_entry(const Fat32DirectoryRecord* record) {
            const char* name = fat32_record_display_name(record);

            return fat32_same_text_case_insensitive(name, ".") || fat32_same_text_case_insensitive(name, "..");
        }

        void fat32_build_directory_record(
            Fat32DirectoryRecord* record,
            const U8* entry_bytes,
            const Fat32LfnState* lfn_state,
            U64 entry_index,
            U32 sector_lba,
            U32 sector_offset) {
            memzero(record, sizeof(*record));
            fat32_copy_directory_entry(&record->entry, entry_bytes);
            fat32_format_short_name(&record->entry, record->short_name);
            if (fat32_lfn_matches_entry(lfn_state, &record->entry)) {
                memcopy(record->long_name, lfn_state->name, sizeof(record->long_name));
                record->has_long_name = true;
                record->first_entry_index = lfn_state->first_entry_index;
                record->entry_count = static_cast<U16>(lfn_state->entry_count + 1U);
            }
            else {
                record->first_entry_index = entry_index;
                record->entry_count = 1U;
            }
            record->short_entry_index = entry_index;
            record->short_entry_sector_lba = sector_lba;
            record->short_entry_sector_offset = sector_offset;
        }

        Status fat32_scan_directory(const Fat32MountState* state, U32 directory_cluster, void* context, Fat32DirectoryVisitor visitor) {
            U32 cluster = directory_cluster;
            U64 entry_index = 0U;
            Fat32LfnState lfn_state = {};

            if ((state == NULL) || (visitor == NULL)) {
                return StatusInvalidArgument;
            }
            if (cluster < 2U) {
                return StatusNotFound;
            }

            while ((cluster >= 2U) && (cluster < Fat32ClusterEnd)) {
                for (U32 offset = 0U; offset < state->cluster_size; offset += Fat32DirectoryEntrySize) {
                    U8 sector[Fat32SectorSize];
                    U32 sector_lba = fat32_cluster_to_lba(state, cluster) + (offset / Fat32SectorSize);
                    U32 sector_offset = offset % Fat32SectorSize;
                    const U8* entry_bytes;
                    Fat32DirectoryRecord record;
                    Status status;

                    status = fat32_read_sector(state, sector_lba, sector);
                    if (status != StatusOK) {
                        return status;
                    }

                    entry_bytes = sector + sector_offset;
                    if (entry_bytes[0] == Fat32DirEntryUnused) {
                        return StatusOK;
                    }
                    if (entry_bytes[0] == Fat32DirEntryDeleted) {
                        fat32_reset_lfn_state(&lfn_state);
                        ++entry_index;
                        continue;
                    }
                    if (entry_bytes[Fat32DirAttributesOffset] == Fat32AttributeLongName) {
                        fat32_capture_lfn_entry(&lfn_state, entry_bytes, entry_index);
                        ++entry_index;
                        continue;
                    }
                    if ((entry_bytes[Fat32DirAttributesOffset] & Fat32AttributeVolumeId) != 0U) {
                        fat32_reset_lfn_state(&lfn_state);
                        ++entry_index;
                        continue;
                    }

                    fat32_build_directory_record(&record, entry_bytes, &lfn_state, entry_index, sector_lba, sector_offset);
                    fat32_reset_lfn_state(&lfn_state);
                    ++entry_index;

                    status = visitor(&record, context);
                    if (status != StatusOK) {
                        return status;
                    }
                }

                {
                    U32 next_cluster;
                    Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                    if (status != StatusOK) {
                        return status;
                    }
                    if (next_cluster >= Fat32ClusterEnd) {
                        return StatusOK;
                    }
                    if ((next_cluster < 2U) || (next_cluster == Fat32ClusterBad)) {
                        return StatusIoError;
                    }

                    cluster = next_cluster;
                }
            }

            return StatusOK;
        }

        bool fat32_record_matches_component(const Fat32DirectoryRecord* record, const char* component) {
            if ((record == NULL) || (component == NULL)) {
                return false;
            }

            return fat32_same_text_case_insensitive(record->short_name, component)
                || (record->has_long_name && fat32_same_text_case_insensitive(record->long_name, component));
        }

        Status fat32_lookup_visitor(const Fat32DirectoryRecord* record, void* context) {
            Fat32LookupContext* lookup = static_cast<Fat32LookupContext*>(context);

            if ((lookup == NULL) || (lookup->component == NULL) || (lookup->record == NULL)) {
                return StatusInvalidArgument;
            }

            if (fat32_record_matches_component(record, lookup->component)) {
                *lookup->record = *record;
                lookup->found = true;
                return StatusAlreadyExists;
            }

            return StatusOK;
        }

        Status fat32_find_directory_entry(const Fat32MountState* state, U32 directory_cluster, const char* component, Fat32DirectoryRecord* record_out) {
            Fat32LookupContext lookup = {};
            Status status;

            if ((state == NULL) || (component == NULL) || (record_out == NULL) || (component[0] == '\0')) {
                return StatusInvalidArgument;
            }

            lookup.component = component;
            lookup.record = record_out;
            lookup.found = false;
            status = fat32_scan_directory(state, directory_cluster, &lookup, &fat32_lookup_visitor);
            if ((status == StatusAlreadyExists) && lookup.found) {
                return StatusOK;
            }
            if (status != StatusOK) {
                return status;
            }

            return StatusNotFound;
        }

        Status fat32_copy_next_component(const char* path, char component[Fat32LongNameCapacity], const char** next_component_out) {
            Size length = 0U;

            if ((path == NULL) || (component == NULL) || (next_component_out == NULL) || (path[0] == '\0')) {
                return StatusInvalidArgument;
            }

            while ((path[length] != '\0') && (path[length] != '/')) {
                if ((length + 1U) >= Fat32LongNameCapacity) {
                    return StatusNoSpace;
                }
                component[length] = path[length];
                ++length;
            }
            if (length == 0U) {
                return StatusInvalidArgument;
            }

            component[length] = '\0';
            *next_component_out = (path[length] == '/') ? (path + length + 1U) : (path + length);
            return StatusOK;
        }

        void fat32_populate_filesystem_node(FilesystemNode* node, const Fat32DirectoryRecord* record) {
            memzero(node, sizeof(*node));
            node->identifier = fat32_entry_cluster(&record->entry);
            node->size_bytes = record->entry.size;
            node->mode = record->entry.attributes;
            node->type = ((record->entry.attributes & Fat32AttributeDirectory) != 0U)
                ? FilesystemNodeTypeDirectory
                : FilesystemNodeTypeFile;
            node->private_data[Fat32NodePrivateEntrySectorLba] = record->short_entry_sector_lba;
            node->private_data[Fat32NodePrivateEntrySectorOffset] = record->short_entry_sector_offset;
        }

        Status fat32_read_fat_entry(const Fat32MountState* state, U32 cluster, U32* next_cluster_out) {
            U8 sector[Fat32SectorSize];
            U32 fat_offset;
            U32 sector_lba;
            U32 sector_offset;

            if ((state == NULL) || (next_cluster_out == NULL)) {
                return StatusInvalidArgument;
            }

            fat_offset = cluster * 4U;
            sector_lba = state->fat_lba + (fat_offset / Fat32SectorSize);
            sector_offset = fat_offset % Fat32SectorSize;
            if (fat32_read_sector(state, sector_lba, sector) != StatusOK) {
                return StatusIoError;
            }

            *next_cluster_out = fat32_read_le32(sector + sector_offset) & Fat32ClusterMask;
            return StatusOK;
        }

        Status fat32_write_fat_entry(const Fat32MountState* state, U32 cluster, U32 value) {
            U32 fat_offset;
            U32 sector_index;
            U32 sector_offset;

            if (state == NULL) {
                return StatusInvalidArgument;
            }

            fat_offset = cluster * 4U;
            sector_index = fat_offset / Fat32SectorSize;
            sector_offset = fat_offset % Fat32SectorSize;

            for (U32 fat_copy = 0U; fat_copy < state->boot_sector.fat_count; ++fat_copy) {
                U8 sector[Fat32SectorSize];
                U32 sector_lba = state->fat_lba + (fat_copy * state->boot_sector.fat_size_32) + sector_index;
                U32 existing_value;

                if (fat32_read_sector(state, sector_lba, sector) != StatusOK) {
                    return StatusIoError;
                }

                existing_value = fat32_read_le32(sector + sector_offset);
                fat32_write_le32(sector + sector_offset, (existing_value & 0xF0000000U) | (value & Fat32ClusterMask));
                if (fat32_write_sector(state, sector_lba, sector) != StatusOK) {
                    return StatusIoError;
                }
            }

            return StatusOK;
        }

        Status fat32_zero_cluster(const Fat32MountState* state, U32 cluster) {
            U8 zero_sector[Fat32SectorSize];

            fat32_memfill(zero_sector, 0U, sizeof(zero_sector));
            for (U32 sector_index = 0U; sector_index < state->boot_sector.sectors_per_cluster; ++sector_index) {
                Status status = fat32_write_sector(state, fat32_cluster_to_lba(state, cluster) + sector_index, zero_sector);

                if (status != StatusOK) {
                    return status;
                }
            }

            return StatusOK;
        }

        Status fat32_find_free_cluster(const Fat32MountState* state, U32* cluster_out) {
            if ((state == NULL) || (cluster_out == NULL)) {
                return StatusInvalidArgument;
            }

            for (U32 cluster = 2U; cluster < (state->cluster_count + 2U); ++cluster) {
                U32 entry_value;
                Status status = fat32_read_fat_entry(state, cluster, &entry_value);

                if (status != StatusOK) {
                    return status;
                }
                if (entry_value == Fat32ClusterFree) {
                    *cluster_out = cluster;
                    return StatusOK;
                }
            }

            return StatusNoSpace;
        }

        Status fat32_allocate_cluster(const Fat32MountState* state, U32* cluster_out) {
            U32 cluster;
            Status status;

            if ((state == NULL) || (cluster_out == NULL)) {
                return StatusInvalidArgument;
            }

            status = fat32_find_free_cluster(state, &cluster);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_write_fat_entry(state, cluster, Fat32ClusterMask);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_zero_cluster(state, cluster);
            if (status != StatusOK) {
                return status;
            }

            *cluster_out = cluster;
            return StatusOK;
        }

        Status fat32_append_cluster(const Fat32MountState* state, U32 first_cluster, U32 new_cluster) {
            U32 cluster = first_cluster;

            if ((state == NULL) || (first_cluster < 2U) || (new_cluster < 2U)) {
                return StatusInvalidArgument;
            }

            for (;;) {
                U32 next_cluster;
                Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                if (status != StatusOK) {
                    return status;
                }
                if (next_cluster == Fat32ClusterBad) {
                    return StatusIoError;
                }
                if ((next_cluster < 2U) || (next_cluster >= Fat32ClusterEnd)) {
                    return fat32_write_fat_entry(state, cluster, new_cluster);
                }

                cluster = next_cluster;
            }
        }

        Status fat32_locate_cluster(const Fat32MountState* state, U32 first_cluster, U64 offset, U32* cluster_out) {
            U64 target_cluster_index;
            U32 cluster = first_cluster;

            if ((state == NULL) || (cluster_out == NULL)) {
                return StatusInvalidArgument;
            }
            if (first_cluster < 2U) {
                return StatusNotFound;
            }

            target_cluster_index = offset / state->cluster_size;
            while (target_cluster_index-- > 0U) {
                U32 next_cluster;
                Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                if (status != StatusOK) {
                    return status;
                }
                if ((next_cluster < 2U) || (next_cluster >= Fat32ClusterEnd)) {
                    return StatusNotFound;
                }

                cluster = next_cluster;
            }

            *cluster_out = cluster;
            return StatusOK;
        }

        Status fat32_ensure_cluster_for_offset(const Fat32MountState* state, FilesystemNode* node, U64 offset, U32* cluster_out) {
            U64 target_cluster_index;
            U32 cluster;

            if ((state == NULL) || (node == NULL) || (cluster_out == NULL)) {
                return StatusInvalidArgument;
            }

            cluster = static_cast<U32>(node->identifier);
            if (cluster < 2U) {
                Status status = fat32_allocate_cluster(state, &cluster);

                if (status != StatusOK) {
                    return status;
                }
                node->identifier = cluster;
            }

            target_cluster_index = offset / state->cluster_size;
            while (target_cluster_index-- > 0U) {
                U32 next_cluster;
                Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                if (status != StatusOK) {
                    return status;
                }
                if (next_cluster == Fat32ClusterBad) {
                    return StatusIoError;
                }
                if ((next_cluster < 2U) || (next_cluster >= Fat32ClusterEnd)) {
                    status = fat32_allocate_cluster(state, &next_cluster);
                    if (status != StatusOK) {
                        return status;
                    }
                    status = fat32_append_cluster(state, cluster, next_cluster);
                    if (status != StatusOK) {
                        return status;
                    }
                }

                cluster = next_cluster;
            }

            *cluster_out = cluster;
            return StatusOK;
        }

        Status fat32_commit_file_node(const Fat32MountState* state, FilesystemNode* node) {
            U32 sector_lba;
            U32 sector_offset;
            U8 sector[Fat32SectorSize];
            Fat32DirectoryEntry entry;

            if ((state == NULL) || (node == NULL)) {
                return StatusInvalidArgument;
            }

            sector_lba = static_cast<U32>(node->private_data[Fat32NodePrivateEntrySectorLba]);
            sector_offset = static_cast<U32>(node->private_data[Fat32NodePrivateEntrySectorOffset]);
            if ((sector_lba == 0U) || ((sector_offset + Fat32DirectoryEntrySize) > Fat32SectorSize)) {
                return StatusInvalidArgument;
            }
            if (fat32_read_sector(state, sector_lba, sector) != StatusOK) {
                return StatusIoError;
            }

            fat32_copy_directory_entry(&entry, sector + sector_offset);
            fat32_set_entry_cluster(&entry, static_cast<U32>(node->identifier));
            entry.size = static_cast<U32>(node->size_bytes & 0xFFFFFFFFULL);

            fat32_write_le16(sector + sector_offset + Fat32DirClusterHiOffset, entry.cluster_hi);
            fat32_write_le16(sector + sector_offset + Fat32DirClusterLoOffset, entry.cluster_lo);
            fat32_write_le32(sector + sector_offset + Fat32DirSizeOffset, entry.size);
            return fat32_write_sector(state, sector_lba, sector);
        }

        Status fat32_write_file_span(const Fat32MountState* state, FilesystemNode* node, U64 offset, const U8* source, Size length, bool write_zeroes) {
            Size total_written = 0U;

            if ((state == NULL) || (node == NULL)) {
                return StatusInvalidArgument;
            }

            while (total_written < length) {
                U8 sector[Fat32SectorSize];
                U32 cluster;
                U32 cluster_offset;
                U32 sector_in_cluster;
                U32 sector_offset;
                U32 sector_lba;
                Size chunk;
                Status status = fat32_ensure_cluster_for_offset(state, node, offset + total_written, &cluster);

                if (status != StatusOK) {
                    return status;
                }

                cluster_offset = static_cast<U32>((offset + total_written) % state->cluster_size);
                sector_in_cluster = cluster_offset / Fat32SectorSize;
                sector_offset = cluster_offset % Fat32SectorSize;
                sector_lba = fat32_cluster_to_lba(state, cluster) + sector_in_cluster;
                status = fat32_read_sector(state, sector_lba, sector);
                if (status != StatusOK) {
                    return status;
                }

                chunk = Fat32SectorSize - sector_offset;
                if (chunk > (length - total_written)) {
                    chunk = length - total_written;
                }

                if (write_zeroes) {
                    fat32_memfill(sector + sector_offset, 0U, chunk);
                }
                else {
                    memcopy(sector + sector_offset, source + total_written, chunk);
                }

                status = fat32_write_sector(state, sector_lba, sector);
                if (status != StatusOK) {
                    return status;
                }

                total_written += chunk;
            }

            return StatusOK;
        }

        Status fat32_mount(void* fs_state, void* device_state) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            Device* device = static_cast<Device*>(device_state);
            U8 sector[Fat32SectorSize];
            U32 partition_lba;
            Status status;
            U32 data_sectors;

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (state->mounted) {
                return StatusAlreadyExists;
            }
            if (device == NULL) {
                return StatusInvalidArgument;
            }

            status = device->init();
            if ((status != StatusOK) && (status != StatusAlreadyExists)) {
                return status;
            }

            status = fat32_find_partition_lba(device, &partition_lba);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_read_exact(device, static_cast<U64>(partition_lba) * Fat32SectorSize, sector, sizeof(sector));
            if (status != StatusOK) {
                return status;
            }
            if (!fat32_is_boot_sector(sector)) {
                return StatusNotSupported;
            }

            memzero(state, sizeof(*state));
            state->device = device;
            state->partition_lba = partition_lba;
            fat32_parse_boot_sector(state, sector);
            if ((state->boot_sector.bytes_per_sector != Fat32SectorSize)
                || (state->boot_sector.sectors_per_cluster == 0U)
                || (state->boot_sector.total_sectors == 0U)
                || (state->boot_sector.fat_size_32 == 0U)
                || (state->boot_sector.root_cluster < 2U)) {
                return StatusNotSupported;
            }

            state->fat_lba = partition_lba + state->boot_sector.reserved_sectors;
            state->data_lba = state->fat_lba + (state->boot_sector.fat_count * state->boot_sector.fat_size_32);
            state->cluster_size = static_cast<U32>(state->boot_sector.sectors_per_cluster) * static_cast<U32>(Fat32SectorSize);
            data_sectors = state->boot_sector.total_sectors
                - (static_cast<U32>(state->boot_sector.reserved_sectors) + (state->boot_sector.fat_count * state->boot_sector.fat_size_32));
            state->cluster_count = data_sectors / state->boot_sector.sectors_per_cluster;
            if (state->cluster_count == 0U) {
                return StatusNotSupported;
            }

            state->mounted = true;
            return StatusOK;
        }

        Status fat32_unmount(void* fs_state) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);

            if (state == NULL) {
                return StatusInvalidArgument;
            }

            memzero(state, sizeof(*state));
            return StatusOK;
        }

        Status fat32_lookup(void* fs_state, const char* path, FilesystemNode* node) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            U32 current_cluster;

            if ((state == NULL) || (path == NULL) || (node == NULL)) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }

            memzero(node, sizeof(*node));
            if (path[0] == '\0') {
                node->identifier = state->boot_sector.root_cluster;
                node->mode = Fat32AttributeDirectory;
                node->type = FilesystemNodeTypeDirectory;
                return StatusOK;
            }

            current_cluster = state->boot_sector.root_cluster;
            for (const char* cursor = path; cursor[0] != '\0';) {
                char component[Fat32LongNameCapacity];
                const char* next_component;
                Fat32DirectoryRecord record;
                Status status = fat32_copy_next_component(cursor, component, &next_component);

                if (status != StatusOK) {
                    return status;
                }
                status = fat32_find_directory_entry(state, current_cluster, component, &record);
                if (status != StatusOK) {
                    return status;
                }

                fat32_populate_filesystem_node(node, &record);
                current_cluster = static_cast<U32>(node->identifier);
                cursor = next_component;
                if ((cursor[0] != '\0') && (node->type != FilesystemNodeTypeDirectory)) {
                    return StatusNotFound;
                }
            }

            return StatusOK;
        }

        bool fat32_component_name_is_valid(const char* component) {
            if ((component == NULL) || (component[0] == '\0')) {
                return false;
            }
            if (fat32_same_text_case_insensitive(component, ".") || fat32_same_text_case_insensitive(component, "..")) {
                return false;
            }

            for (Size index = 0U; component[index] != '\0'; ++index) {
                if (component[index] == '/') {
                    return false;
                }
            }

            return true;
        }

        bool fat32_short_name_character_is_valid(int ch) {
            return ((ch >= 'A') && (ch <= 'Z'))
                || ((ch >= '0') && (ch <= '9'))
                || (ch == '$')
                || (ch == '%')
                || (ch == '\'')
                || (ch == '-')
                || (ch == '_')
                || (ch == '@')
                || (ch == '~')
                || (ch == '`')
                || (ch == '!')
                || (ch == '(')
                || (ch == ')')
                || (ch == '{')
                || (ch == '}')
                || (ch == '^')
                || (ch == '#')
                || (ch == '&');
        }

        char fat32_short_name_character(int ch) {
            ch = fat32_ascii_upper(ch);
            if (fat32_short_name_character_is_valid(ch)) {
                return static_cast<char>(ch);
            }

            return '_';
        }

        void fat32_split_name_parts(
            const char* component,
            const char** base_start_out,
            Size* base_length_out,
            const char** extension_start_out,
            Size* extension_length_out) {
            Size length = fat32_string_length(component);
            Size last_dot = length;

            for (Size index = 1U; index + 1U < length; ++index) {
                if (component[index] == '.') {
                    last_dot = index;
                }
            }

            if ((last_dot < length) && (component[last_dot] == '.')) {
                *base_start_out = component;
                *base_length_out = last_dot;
                *extension_start_out = component + last_dot + 1U;
                *extension_length_out = length - last_dot - 1U;
                return;
            }

            *base_start_out = component;
            *base_length_out = length;
            *extension_start_out = component + length;
            *extension_length_out = 0U;
        }

        void fat32_build_short_name_with_suffix(const char* component, U32 suffix, char short_name[Fat32ShortNameSize]) {
            const char* base_start;
            const char* extension_start;
            Size base_length;
            Size extension_length;
            Size base_write = 0U;
            Size extension_write = 0U;
            char suffix_digits[10];
            Size suffix_digit_count = 0U;
            Size base_limit = 8U;

            fat32_memfill(short_name, ' ', Fat32ShortNameSize);
            fat32_split_name_parts(component, &base_start, &base_length, &extension_start, &extension_length);

            if (suffix != 0U) {
                U32 value = suffix;

                do {
                    suffix_digits[suffix_digit_count++] = static_cast<char>('0' + (value % 10U));
                    value /= 10U;
                } while ((value != 0U) && (suffix_digit_count < COUNT_OF(suffix_digits)));

                if ((suffix_digit_count + 1U) < 8U) {
                    base_limit = 8U - (suffix_digit_count + 1U);
                }
                else {
                    base_limit = 1U;
                }
            }

            for (Size index = 0U; (index < base_length) && (base_write < base_limit); ++index) {
                if (base_start[index] == '.') {
                    continue;
                }
                short_name[base_write++] = fat32_short_name_character(base_start[index]);
            }
            if (base_write == 0U) {
                short_name[base_write++] = '_';
            }
            if (suffix != 0U) {
                short_name[base_write++] = '~';
                while ((suffix_digit_count != 0U) && (base_write < 8U)) {
                    short_name[base_write++] = suffix_digits[--suffix_digit_count];
                }
            }

            for (Size index = 0U; (index < extension_length) && (extension_write < 3U); ++index) {
                if (extension_start[index] == '.') {
                    continue;
                }
                short_name[8U + extension_write++] = fat32_short_name_character(extension_start[index]);
            }
        }

        typedef struct Fat32ShortNameSearchContext {
            const char* short_name;
            bool found;
        } Fat32ShortNameSearchContext;

        Status fat32_short_name_exists_visitor(const Fat32DirectoryRecord* record, void* context) {
            Fat32ShortNameSearchContext* search = static_cast<Fat32ShortNameSearchContext*>(context);

            if ((record == NULL) || (search == NULL) || (search->short_name == NULL)) {
                return StatusInvalidArgument;
            }

            for (Size index = 0U; index < Fat32ShortNameSize; ++index) {
                if (record->entry.name[index] != search->short_name[index]) {
                    return StatusOK;
                }
            }

            search->found = true;
            return StatusAlreadyExists;
        }

        Status fat32_short_name_exists(const Fat32MountState* state, U32 directory_cluster, const char short_name[Fat32ShortNameSize], bool* exists_out) {
            Fat32ShortNameSearchContext search = {};
            Status status;

            if ((state == NULL) || (short_name == NULL) || (exists_out == NULL)) {
                return StatusInvalidArgument;
            }

            search.short_name = short_name;
            search.found = false;
            status = fat32_scan_directory(state, directory_cluster, &search, &fat32_short_name_exists_visitor);
            if ((status == StatusAlreadyExists) && search.found) {
                *exists_out = true;
                return StatusOK;
            }
            if (status != StatusOK) {
                return status;
            }

            *exists_out = false;
            return StatusOK;
        }

        Status fat32_generate_short_name(const Fat32MountState* state, U32 directory_cluster, const char* component, char short_name[Fat32ShortNameSize], bool* needs_long_name_out) {
            for (U32 suffix = 0U; suffix < 1000000U; ++suffix) {
                Fat32DirectoryEntry short_entry = {};
                char canonical_name[Fat32LongNameCapacity];
                bool exists;
                Status status;

                fat32_build_short_name_with_suffix(component, suffix, short_name);
                status = fat32_short_name_exists(state, directory_cluster, short_name, &exists);
                if (status != StatusOK) {
                    return status;
                }
                if (exists) {
                    continue;
                }

                memcopy(short_entry.name, short_name, Fat32ShortNameSize);
                fat32_format_short_name(&short_entry, canonical_name);
                *needs_long_name_out = !fat32_same_text_case_insensitive(component, canonical_name);
                return StatusOK;
            }

            return StatusNoSpace;
        }

        Status fat32_split_parent_path(const char* path, char parent_path[Fat32PathCapacity], char component[Fat32LongNameCapacity]) {
            Size length = fat32_string_length(path);
            Size last_separator = length;

            if ((path == NULL) || (path[0] == '\0') || (parent_path == NULL) || (component == NULL)) {
                return StatusInvalidArgument;
            }

            for (Size index = 0U; index < length; ++index) {
                if (path[index] == '/') {
                    last_separator = index;
                }
            }
            if ((length != 0U) && (path[length - 1U] == '/')) {
                return StatusInvalidArgument;
            }

            if (last_separator == length) {
                if (length >= Fat32LongNameCapacity) {
                    return StatusNoSpace;
                }
                parent_path[0] = '\0';
                memcopy(component, path, length + 1U);
            }
            else {
                Size parent_length = last_separator;
                Size component_length = length - last_separator - 1U;

                if ((parent_length >= Fat32PathCapacity) || (component_length == 0U) || (component_length >= Fat32LongNameCapacity)) {
                    return StatusInvalidArgument;
                }

                memcopy(parent_path, path, parent_length);
                parent_path[parent_length] = '\0';
                memcopy(component, path + last_separator + 1U, component_length);
                component[component_length] = '\0';
            }

            if (!fat32_component_name_is_valid(component)) {
                return StatusInvalidArgument;
            }

            return StatusOK;
        }

        Status fat32_resolve_directory_cluster(const Fat32MountState* state, const char* path, U32* cluster_out) {
            FilesystemNode node;
            Status status;

            if ((state == NULL) || (cluster_out == NULL)) {
                return StatusInvalidArgument;
            }
            if ((path == NULL) || (path[0] == '\0')) {
                *cluster_out = state->boot_sector.root_cluster;
                return StatusOK;
            }

            status = fat32_lookup(const_cast<Fat32MountState*>(state), path, &node);
            if (status != StatusOK) {
                return status;
            }
            if (node.type != FilesystemNodeTypeDirectory) {
                return StatusNotFound;
            }

            *cluster_out = (node.identifier < 2U) ? state->boot_sector.root_cluster : static_cast<U32>(node.identifier);
            return StatusOK;
        }

        Status fat32_directory_slot_location(
            const Fat32MountState* state,
            U32 directory_cluster,
            U64 entry_index,
            bool allow_extend,
            U32* sector_lba_out,
            U32* sector_offset_out) {
            FilesystemNode directory_node = {};
            U32 cluster;
            U32 cluster_offset;
            U32 sector_in_cluster;
            Status status;

            if ((state == NULL) || (directory_cluster < 2U) || (sector_lba_out == NULL) || (sector_offset_out == NULL)) {
                return StatusInvalidArgument;
            }

            directory_node.identifier = directory_cluster;
            status = allow_extend
                ? fat32_ensure_cluster_for_offset(state, &directory_node, entry_index * Fat32DirectoryEntrySize, &cluster)
                : fat32_locate_cluster(state, directory_cluster, entry_index * Fat32DirectoryEntrySize, &cluster);
            if (status != StatusOK) {
                return status;
            }

            cluster_offset = static_cast<U32>((entry_index * Fat32DirectoryEntrySize) % state->cluster_size);
            sector_in_cluster = cluster_offset / Fat32SectorSize;
            *sector_lba_out = fat32_cluster_to_lba(state, cluster) + sector_in_cluster;
            *sector_offset_out = cluster_offset % Fat32SectorSize;
            return StatusOK;
        }

        Status fat32_write_directory_slot(const Fat32MountState* state, U32 directory_cluster, U64 entry_index, const U8 entry_bytes[Fat32DirectoryEntrySize]) {
            U8 sector[Fat32SectorSize];
            U32 sector_lba;
            U32 sector_offset;
            Status status;

            status = fat32_directory_slot_location(state, directory_cluster, entry_index, true, &sector_lba, &sector_offset);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_read_sector(state, sector_lba, sector);
            if (status != StatusOK) {
                return status;
            }

            memcopy(sector + sector_offset, entry_bytes, Fat32DirectoryEntrySize);
            return fat32_write_sector(state, sector_lba, sector);
        }

        Status fat32_mark_directory_slots_deleted(const Fat32MountState* state, U32 directory_cluster, U64 first_entry_index, Size entry_count) {
            for (Size index = 0U; index < entry_count; ++index) {
                U8 sector[Fat32SectorSize];
                U32 sector_lba;
                U32 sector_offset;
                Status status = fat32_directory_slot_location(state, directory_cluster, first_entry_index + index, false, &sector_lba, &sector_offset);

                if (status != StatusOK) {
                    return status;
                }
                status = fat32_read_sector(state, sector_lba, sector);
                if (status != StatusOK) {
                    return status;
                }

                sector[sector_offset] = Fat32DirEntryDeleted;
                status = fat32_write_sector(state, sector_lba, sector);
                if (status != StatusOK) {
                    return status;
                }
            }

            return StatusOK;
        }

        Status fat32_find_free_directory_slots(const Fat32MountState* state, U32 directory_cluster, Size required_entries, U64* first_entry_index_out) {
            U32 cluster = directory_cluster;
            U64 entry_index = 0U;
            U64 candidate_index = 0U;
            Size free_count = 0U;

            if ((state == NULL) || (directory_cluster < 2U) || (required_entries == 0U) || (first_entry_index_out == NULL)) {
                return StatusInvalidArgument;
            }

            while ((cluster >= 2U) && (cluster < Fat32ClusterEnd)) {
                for (U32 offset = 0U; offset < state->cluster_size; offset += Fat32DirectoryEntrySize) {
                    U8 sector[Fat32SectorSize];
                    U32 sector_lba = fat32_cluster_to_lba(state, cluster) + (offset / Fat32SectorSize);
                    U32 sector_offset = offset % Fat32SectorSize;
                    Status status = fat32_read_sector(state, sector_lba, sector);

                    if (status != StatusOK) {
                        return status;
                    }

                    if (sector[sector_offset] == Fat32DirEntryDeleted) {
                        if (free_count == 0U) {
                            candidate_index = entry_index;
                        }
                        ++free_count;
                        if (free_count >= required_entries) {
                            *first_entry_index_out = candidate_index;
                            return StatusOK;
                        }
                    }
                    else if (sector[sector_offset] == Fat32DirEntryUnused) {
                        if (free_count == 0U) {
                            candidate_index = entry_index;
                        }
                        *first_entry_index_out = candidate_index;
                        return StatusOK;
                    }
                    else {
                        free_count = 0U;
                    }

                    ++entry_index;
                }

                {
                    U32 next_cluster;
                    Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                    if (status != StatusOK) {
                        return status;
                    }
                    if (next_cluster >= Fat32ClusterEnd) {
                        break;
                    }
                    if ((next_cluster < 2U) || (next_cluster == Fat32ClusterBad)) {
                        return StatusIoError;
                    }

                    cluster = next_cluster;
                }
            }

            *first_entry_index_out = entry_index;
            return StatusOK;
        }

        void fat32_write_lfn_code_unit(U8 entry_bytes[Fat32DirectoryEntrySize], Size character_index, U16 code_unit) {
            Size byte_offset;

            if (character_index < 5U) {
                byte_offset = Fat32LfnName1Offset + (character_index * 2U);
            }
            else if (character_index < 11U) {
                byte_offset = Fat32LfnName2Offset + ((character_index - 5U) * 2U);
            }
            else {
                byte_offset = Fat32LfnName3Offset + ((character_index - 11U) * 2U);
            }

            fat32_write_le16(entry_bytes + byte_offset, code_unit);
        }

        void fat32_build_short_entry_bytes(U8 entry_bytes[Fat32DirectoryEntrySize], const char short_name[Fat32ShortNameSize], FilesystemNodeType type, U32 cluster) {
            fat32_memfill(entry_bytes, 0U, Fat32DirectoryEntrySize);
            memcopy(entry_bytes, short_name, Fat32ShortNameSize);
            entry_bytes[Fat32DirAttributesOffset] = (type == FilesystemNodeTypeDirectory) ? Fat32AttributeDirectory : Fat32AttributeArchive;
            fat32_write_le16(entry_bytes + Fat32DirClusterHiOffset, static_cast<U16>((cluster >> 16) & 0xFFFFU));
            fat32_write_le16(entry_bytes + Fat32DirClusterLoOffset, static_cast<U16>(cluster & 0xFFFFU));
            fat32_write_le32(entry_bytes + Fat32DirSizeOffset, 0U);
        }

        Status fat32_build_lfn_entries(
            const char* component,
            const char short_name[Fat32ShortNameSize],
            U8 entry_bytes[20][Fat32DirectoryEntrySize],
            Size* entry_count_out) {
            constexpr Size Fat32MaxLfnEntryCount = 20U;
            Size name_length = fat32_string_length(component);
            Size entry_count = (name_length + 12U) / 13U;
            U8 checksum = fat32_short_name_checksum(short_name);

            if ((component == NULL) || (entry_count_out == NULL) || (name_length == 0U)) {
                return StatusInvalidArgument;
            }
            if (entry_count > Fat32MaxLfnEntryCount) {
                return StatusNoSpace;
            }

            for (Size index = 0U; index < entry_count; ++index) {
                Size order = entry_count - index;

                fat32_memfill(entry_bytes[index], 0xFFU, Fat32DirectoryEntrySize);
                entry_bytes[index][Fat32LfnOrderOffset] = static_cast<U8>(order);
                if (index == 0U) {
                    entry_bytes[index][Fat32LfnOrderOffset] |= 0x40U;
                }
                entry_bytes[index][Fat32DirAttributesOffset] = Fat32AttributeLongName;
                entry_bytes[index][12] = 0U;
                entry_bytes[index][Fat32LfnChecksumOffset] = checksum;
                entry_bytes[index][26] = 0U;
                entry_bytes[index][27] = 0U;

                for (Size character = 0U; character < 13U; ++character) {
                    Size source_index = (order - 1U) * 13U + character;
                    U16 code_unit = 0xFFFFU;

                    if (source_index < name_length) {
                        U8 byte = static_cast<U8>(component[source_index]);
                        code_unit = (byte <= 0x7FU) ? byte : static_cast<U16>('?');
                    }
                    else if (source_index == name_length) {
                        code_unit = 0x0000U;
                    }

                    fat32_write_lfn_code_unit(entry_bytes[index], character, code_unit);
                }
            }

            *entry_count_out = entry_count;
            return StatusOK;
        }

        Status fat32_initialize_directory_cluster(const Fat32MountState* state, U32 directory_cluster, U32 parent_cluster) {
            U8 dot_entry[Fat32DirectoryEntrySize];
            U8 dotdot_entry[Fat32DirectoryEntrySize];
            char dot_name[Fat32ShortNameSize];
            char dotdot_name[Fat32ShortNameSize];
            Status status;

            fat32_memfill(dot_name, ' ', sizeof(dot_name));
            fat32_memfill(dotdot_name, ' ', sizeof(dotdot_name));
            dot_name[0] = '.';
            dotdot_name[0] = '.';
            dotdot_name[1] = '.';

            status = fat32_zero_cluster(state, directory_cluster);
            if (status != StatusOK) {
                return status;
            }

            fat32_build_short_entry_bytes(dot_entry, dot_name, FilesystemNodeTypeDirectory, directory_cluster);
            fat32_build_short_entry_bytes(dotdot_entry, dotdot_name, FilesystemNodeTypeDirectory, parent_cluster);
            status = fat32_write_directory_slot(state, directory_cluster, 0U, dot_entry);
            if (status != StatusOK) {
                return status;
            }

            return fat32_write_directory_slot(state, directory_cluster, 1U, dotdot_entry);
        }

        Status fat32_free_cluster_chain(const Fat32MountState* state, U32 first_cluster) {
            U32 cluster = first_cluster;

            if (state == NULL) {
                return StatusInvalidArgument;
            }
            if (cluster < 2U) {
                return StatusOK;
            }

            while ((cluster >= 2U) && (cluster < Fat32ClusterEnd)) {
                U32 next_cluster;
                Status status = fat32_read_fat_entry(state, cluster, &next_cluster);

                if (status != StatusOK) {
                    return status;
                }
                status = fat32_write_fat_entry(state, cluster, Fat32ClusterFree);
                if (status != StatusOK) {
                    return status;
                }
                if ((next_cluster < 2U) || (next_cluster >= Fat32ClusterEnd)) {
                    return StatusOK;
                }
                if (next_cluster == Fat32ClusterBad) {
                    return StatusIoError;
                }

                cluster = next_cluster;
            }

            return StatusOK;
        }

        Status fat32_directory_empty_visitor(const Fat32DirectoryRecord* record, void* context) {
            (void)context;

            if ((record == NULL) || fat32_record_is_relative_entry(record)) {
                return StatusOK;
            }

            return StatusBusy;
        }

        Status fat32_directory_is_empty(const Fat32MountState* state, U32 directory_cluster) {
            Status status = fat32_scan_directory(state, directory_cluster, NULL, &fat32_directory_empty_visitor);

            if (status == StatusBusy) {
                return StatusBusy;
            }

            return status;
        }

        Status fat32_create(void* fs_state, const char* path, FilesystemNodeType type, FilesystemNode* node) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            char parent_path[Fat32PathCapacity];
            char component[Fat32LongNameCapacity];
            char short_name[Fat32ShortNameSize];
            U8 short_entry[Fat32DirectoryEntrySize];
            U8 lfn_entries[20][Fat32DirectoryEntrySize];
            Size lfn_entry_count = 0U;
            Size written_entries = 0U;
            bool needs_long_name;
            U32 parent_cluster;
            U32 first_cluster = 0U;
            U64 first_entry_index;
            U32 short_entry_sector_lba;
            U32 short_entry_sector_offset;
            Status status;

            if ((state == NULL) || (path == NULL)) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }
            if ((type != FilesystemNodeTypeFile) && (type != FilesystemNodeTypeDirectory)) {
                return StatusInvalidArgument;
            }

            status = fat32_split_parent_path(path, parent_path, component);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_resolve_directory_cluster(state, parent_path, &parent_cluster);
            if (status != StatusOK) {
                return status;
            }

            {
                Fat32DirectoryRecord existing_record;

                status = fat32_find_directory_entry(state, parent_cluster, component, &existing_record);
                if (status == StatusOK) {
                    return StatusAlreadyExists;
                }
                if (status != StatusNotFound) {
                    return status;
                }
            }

            status = fat32_generate_short_name(state, parent_cluster, component, short_name, &needs_long_name);
            if (status != StatusOK) {
                return status;
            }
            if (needs_long_name) {
                status = fat32_build_lfn_entries(component, short_name, lfn_entries, &lfn_entry_count);
                if (status != StatusOK) {
                    return status;
                }
            }
            if (type == FilesystemNodeTypeDirectory) {
                status = fat32_allocate_cluster(state, &first_cluster);
                if (status != StatusOK) {
                    return status;
                }
                status = fat32_initialize_directory_cluster(state, first_cluster, parent_cluster);
                if (status != StatusOK) {
                    (void)fat32_free_cluster_chain(state, first_cluster);
                    return status;
                }
            }

            fat32_build_short_entry_bytes(short_entry, short_name, type, first_cluster);
            status = fat32_find_free_directory_slots(state, parent_cluster, lfn_entry_count + 1U, &first_entry_index);
            if (status != StatusOK) {
                if (type == FilesystemNodeTypeDirectory) {
                    (void)fat32_free_cluster_chain(state, first_cluster);
                }
                return status;
            }

            for (Size index = 0U; index < lfn_entry_count; ++index) {
                status = fat32_write_directory_slot(state, parent_cluster, first_entry_index + index, lfn_entries[index]);
                if (status != StatusOK) {
                    (void)fat32_mark_directory_slots_deleted(state, parent_cluster, first_entry_index, written_entries);
                    if (type == FilesystemNodeTypeDirectory) {
                        (void)fat32_free_cluster_chain(state, first_cluster);
                    }
                    return status;
                }
                ++written_entries;
            }

            status = fat32_write_directory_slot(state, parent_cluster, first_entry_index + lfn_entry_count, short_entry);
            if (status != StatusOK) {
                (void)fat32_mark_directory_slots_deleted(state, parent_cluster, first_entry_index, written_entries);
                if (type == FilesystemNodeTypeDirectory) {
                    (void)fat32_free_cluster_chain(state, first_cluster);
                }
                return status;
            }

            if (node != NULL) {
                status = fat32_directory_slot_location(
                    state,
                    parent_cluster,
                    first_entry_index + lfn_entry_count,
                    false,
                    &short_entry_sector_lba,
                    &short_entry_sector_offset);
                if (status != StatusOK) {
                    return status;
                }

                memzero(node, sizeof(*node));
                node->identifier = first_cluster;
                node->size_bytes = 0U;
                node->mode = short_entry[Fat32DirAttributesOffset];
                node->type = type;
                node->private_data[Fat32NodePrivateEntrySectorLba] = short_entry_sector_lba;
                node->private_data[Fat32NodePrivateEntrySectorOffset] = short_entry_sector_offset;
            }

            return StatusOK;
        }

        Status fat32_remove(void* fs_state, const char* path) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            char parent_path[Fat32PathCapacity];
            char component[Fat32LongNameCapacity];
            Fat32DirectoryRecord record;
            U32 parent_cluster;
            U32 target_cluster;
            Status status;

            if ((state == NULL) || (path == NULL)) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }

            status = fat32_split_parent_path(path, parent_path, component);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_resolve_directory_cluster(state, parent_path, &parent_cluster);
            if (status != StatusOK) {
                return status;
            }
            status = fat32_find_directory_entry(state, parent_cluster, component, &record);
            if (status != StatusOK) {
                return status;
            }
            if (fat32_record_is_relative_entry(&record)) {
                return StatusInvalidArgument;
            }

            target_cluster = fat32_entry_cluster(&record.entry);
            if ((record.entry.attributes & Fat32AttributeDirectory) != 0U) {
                status = fat32_directory_is_empty(state, target_cluster);
                if (status != StatusOK) {
                    return status;
                }
            }

            status = fat32_free_cluster_chain(state, target_cluster);
            if (status != StatusOK) {
                return status;
            }

            return fat32_mark_directory_slots_deleted(state, parent_cluster, record.first_entry_index, record.entry_count);
        }

        SSize fat32_read(void* fs_state, FilesystemNode* node, U64 offset, void* buffer, Size length) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            U8* output = static_cast<U8*>(buffer);
            Size total_read = 0U;
            Size remaining;

            if ((state == NULL) || (node == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }
            if (node->type != FilesystemNodeTypeFile) {
                return StatusInvalidArgument;
            }
            if ((length == 0U) || (offset >= node->size_bytes)) {
                return 0;
            }

            remaining = length;
            if (remaining > (node->size_bytes - offset)) {
                remaining = static_cast<Size>(node->size_bytes - offset);
            }
            if (node->identifier < 2U) {
                return 0;
            }

            while (remaining > 0U) {
                U8 sector[Fat32SectorSize];
                U32 cluster;
                U32 cluster_offset;
                U32 sector_in_cluster;
                U32 sector_offset;
                U32 sector_lba;
                Size aligned_chunk;
                Size chunk;
                Status status = fat32_locate_cluster(state, static_cast<U32>(node->identifier), offset + total_read, &cluster);

                if (status != StatusOK) {
                    return (total_read != 0U) ? static_cast<SSize>(total_read) : status;
                }

                cluster_offset = static_cast<U32>((offset + total_read) % state->cluster_size);
                sector_in_cluster = cluster_offset / Fat32SectorSize;
                sector_offset = cluster_offset % Fat32SectorSize;
                sector_lba = fat32_cluster_to_lba(state, cluster) + sector_in_cluster;

                /*
                 * Sequential file loads usually start on sector boundaries and
                 * consume many full sectors in one run. Reading the whole aligned
                 * span directly avoids paying one worker handoff and one FAT32
                 * stack frame per 512-byte sector while still falling back to the
                 * old sector-buffer path for misaligned heads or tails.
                 */
                aligned_chunk = state->cluster_size - cluster_offset;
                if (aligned_chunk > remaining) {
                    aligned_chunk = remaining;
                }
                if (sector_offset == 0U) {
                    aligned_chunk -= (aligned_chunk % Fat32SectorSize);
                    if (aligned_chunk != 0U) {
                        status = fat32_read_exact(
                            state->device,
                            static_cast<U64>(sector_lba) * Fat32SectorSize,
                            output + total_read,
                            aligned_chunk);
                        if (status != StatusOK) {
                            return (total_read != 0U) ? static_cast<SSize>(total_read) : status;
                        }

                        total_read += aligned_chunk;
                        remaining -= aligned_chunk;
                        continue;
                    }
                }

                status = fat32_read_sector(state, sector_lba, sector);
                if (status != StatusOK) {
                    return (total_read != 0U) ? static_cast<SSize>(total_read) : status;
                }

                chunk = Fat32SectorSize - sector_offset;
                if (chunk > remaining) {
                    chunk = remaining;
                }

                memcopy(output + total_read, sector + sector_offset, chunk);
                total_read += chunk;
                remaining -= chunk;
            }

            return static_cast<SSize>(total_read);
        }

        SSize fat32_write(void* fs_state, FilesystemNode* node, U64 offset, const void* buffer, Size length) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            const U8* bytes = static_cast<const U8*>(buffer);
            U64 original_size;
            U64 original_identifier;
            U64 final_size;
            Status status;

            if ((state == NULL) || (node == NULL) || ((buffer == NULL) && (length != 0U))) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }
            if (node->type != FilesystemNodeTypeFile) {
                return StatusInvalidArgument;
            }
            if ((node->mode & Fat32AttributeReadOnly) != 0U) {
                return StatusNotSupported;
            }
            if (length == 0U) {
                return 0;
            }

            original_size = node->size_bytes;
            original_identifier = node->identifier;
            if (offset > original_size) {
                // FAT32 has no sparse files, so writes beyond EOF must materialize zero bytes in the gap
                // instead of leaving old disk contents visible through the expanded file range.
                status = fat32_write_file_span(state, node, original_size, NULL, static_cast<Size>(offset - original_size), true);
                if (status != StatusOK) {
                    return status;
                }
            }

            status = fat32_write_file_span(state, node, offset, bytes, length, false);
            if (status != StatusOK) {
                return status;
            }

            final_size = original_size;
            if (offset + length > final_size) {
                final_size = offset + length;
            }
            node->size_bytes = final_size;
            if ((node->identifier != original_identifier) || (node->size_bytes != original_size)) {
                status = fat32_commit_file_node(state, node);
                if (status != StatusOK) {
                    return status;
                }
            }

            return static_cast<SSize>(length);
        }

        Status fat32_enumerate_visitor(const Fat32DirectoryRecord* record, void* context) {
            Fat32EnumerateContext* enumerate = static_cast<Fat32EnumerateContext*>(context);
            FilesystemNode child_node;

            if ((enumerate == NULL) || (enumerate->visitor == NULL) || (record == NULL)) {
                return StatusInvalidArgument;
            }
            if (fat32_record_is_relative_entry(record)) {
                return StatusOK;
            }

            fat32_populate_filesystem_node(&child_node, record);
            return enumerate->visitor(fat32_record_display_name(record), &child_node, enumerate->user_context);
        }

        Status fat32_enumerate(void* fs_state, FilesystemNode* directory, void* context, FilesystemEnumerateVisitor visitor) {
            Fat32MountState* state = static_cast<Fat32MountState*>(fs_state);
            Fat32EnumerateContext enumerate = {};
            U32 directory_cluster;

            if ((state == NULL) || (directory == NULL) || (visitor == NULL)) {
                return StatusInvalidArgument;
            }
            if (!state->mounted) {
                return StatusNotFound;
            }
            if (directory->type != FilesystemNodeTypeDirectory) {
                return StatusInvalidArgument;
            }

            directory_cluster = static_cast<U32>(directory->identifier);
            if (directory_cluster < 2U) {
                directory_cluster = state->boot_sector.root_cluster;
            }

            enumerate.user_context = context;
            enumerate.visitor = visitor;
            return fat32_scan_directory(state, directory_cluster, &enumerate, &fat32_enumerate_visitor);
        }

        const FilesystemOps g_fat32_ops = {
            &fat32_mount,
            &fat32_unmount,
            &fat32_lookup,
            &fat32_create,
            &fat32_remove,
            &fat32_read,
            &fat32_write,
            &fat32_enumerate,
        };

        FilesystemDriver g_fat32_driver = {
            "fat32",
            &g_fat32_ops,
            NULL,
        };

    } // namespace

    FilesystemDriver* Fat32Filesystem::driver(void) {
        return &g_fat32_driver;
    }

    void* Fat32Filesystem::system_volume_state(void) {
        return &g_system_volume_state;
    }

} // namespace filesystem