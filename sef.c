#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  JumpBoot[3];
    uint8_t  FileSystemName[8];
    uint8_t  MustBeZero[53];
    uint64_t PartitionOffset;
    uint64_t VolumeLength;
    uint32_t FatOffset;
    uint32_t FatLength;
    uint32_t ClusterHeapOffset;
    uint32_t ClusterCount;
    uint32_t FirstClusterOfRootDirectory;
    uint32_t VolumeSerialNumber;
    uint16_t FileSystemRevision;
    uint16_t VolumeFlags;
    uint8_t  BytesPerSectorShift;
    uint8_t  SectorsPerClusterShift;
    uint8_t  NumberOfFats;
    uint8_t  DriveSelect;
    uint8_t  PercentInUse;
    uint8_t  Reserved[7];
    uint8_t  BootCode[390];
    uint16_t BootSignature;
} ExFatBootSector;

typedef struct {
    uint8_t  EntryType;
    uint8_t  SecondaryCount;
    uint16_t SetChecksum;
    uint16_t FileAttributes;
    uint16_t Reserved1;
    uint32_t CreateTimestamp;
    uint32_t LastModifiedTimestamp;
    uint32_t LastAccessedTimestamp;
    uint8_t  Create10msIncrement;
    uint8_t  LastModified10msIncrement;
    uint8_t  CreateTimezoneOffset;
    uint8_t  LastModifiedTimezoneOffset;
    uint8_t  LastAccessedTimezoneOffset;
    uint8_t  Reserved2[7];
} ExFatFileDirectoryEntry;

typedef struct {
    uint8_t  EntryType;
    uint8_t  GeneralSecondaryFlags;
    uint8_t  Reserved1;
    uint8_t  NameLength;
    uint16_t NameHash;
    uint16_t Reserved2;
    uint64_t ValidDataLength;
    uint32_t Reserved3;
    uint32_t FirstCluster;
    uint64_t DataLength;
} ExFatStreamExtensionEntry;

typedef struct {
    uint8_t  EntryType;
    uint8_t  CustomDefinedFlags;
    uint16_t FileName[15];
} ExFatFileNameEntry;
#pragma pack(pop)

void print_layout(ExFatBootSector *bootSector)
{
    // Print the layout
    printf("Partition Offset: %lu\n", bootSector->PartitionOffset);
    printf("Volume Length: %lu\n", bootSector->VolumeLength);
    printf("FAT Offset: %u\n", bootSector->FatOffset);
    printf("FAT Length: %u\n", bootSector->FatLength);
    printf("Cluster Heap Offset: %u\n", bootSector->ClusterHeapOffset);
    printf("Total Clusters: %u\n", bootSector->ClusterCount);
    printf("First Cluster of Root Directory: %u\n", bootSector->FirstClusterOfRootDirectory);
    printf("Volume Serial Number: %u\n", bootSector->VolumeSerialNumber);
    printf("File System Revision: %u\n", bootSector->FileSystemRevision);
    printf("Volume Flags: %u\n", bootSector->VolumeFlags);
    printf("Bytes Per Sector: %u\n", (1 << bootSector->BytesPerSectorShift));
    printf("Sectors Per Cluster: %u\n", (1 << bootSector->SectorsPerClusterShift));
    printf("Number of FATs: %u\n", bootSector->NumberOfFats);
}

uint64_t get_cluster_offset1(ExFatBootSector *bootSector, uint64_t cluster)
{
    uint64_t offset = bootSector->PartitionOffset * (1 << bootSector->BytesPerSectorShift) +
                             bootSector->ClusterHeapOffset * (1 << bootSector->BytesPerSectorShift) +
                             (cluster - 2) * (1 << bootSector->SectorsPerClusterShift) * (1 << bootSector->BytesPerSectorShift);

    return offset;
}
uint64_t get_cluster_offset(ExFatBootSector *bootSector, uint64_t cluster)
{
    uint64_t offset = bootSector->ClusterHeapOffset * (1 << bootSector->BytesPerSectorShift) +
                 (cluster - 2) * (1 << bootSector->SectorsPerClusterShift) * (1 << bootSector->BytesPerSectorShift);

    return offset;
}

uint64_t get_root_directory(ExFatBootSector *bootSector)
{
    uint64_t rootDirOffset = get_cluster_offset(bootSector, bootSector->FirstClusterOfRootDirectory);
    printf("Root directory starts at %0lx (%lu)\n", rootDirOffset, rootDirOffset);
    return rootDirOffset;
}

int is_valid_string(const char *str)
{
    while (*str) {
        if (!isalnum((unsigned char)*str) && *str != '_' && *str != '-' && *str != '.') {
            return 0;
        }
        str++;
    }
    return 1;
}

void parse_directory(FILE *imageFile, ExFatBootSector *bootSector, uint64_t dirOffset, int do_rescue, char *rescue_dir, int *files_saved)
{
    // Seek to the directory offset
    if (fseek(imageFile, dirOffset, SEEK_SET) != 0) {
        perror("Error seeking to the directory");
        return;
    }

    uint64_t cluster_size = (1 << bootSector->SectorsPerClusterShift) * (1 << bootSector->BytesPerSectorShift);
    // Read the directory entries
    ExFatFileDirectoryEntry fileEntry;
    ExFatStreamExtensionEntry streamEntry;
    ExFatFileNameEntry nameEntry;
    *files_saved = 0;
    int record_count = 0;
    while ((record_count < cluster_size / sizeof(fileEntry)) && fread(&fileEntry, sizeof(fileEntry), 1, imageFile) == 1) {
//	    printf("%x ", fileEntry.EntryType);
        record_count ++;
        if (fileEntry.EntryType == 0x85) { // File or directory entry
//            printf("possible directory at offset %llx\n", dirOffset);
            if (fread(&streamEntry, sizeof(streamEntry), 1, imageFile) != 1) {
                perror("Error reading directory entries");
                return;
            }
            if (streamEntry.EntryType != 0xC0)
                continue;
            char utf8FileName[256];
            memset(utf8FileName, 0, sizeof(utf8FileName));
            int name_length = streamEntry.NameLength;
            int length = 0;
            while (length < name_length) {
                if (fread(&nameEntry, sizeof(nameEntry), 1, imageFile) != 1) {
                    perror("Error reading directory entries");
                    return;
                }
                if (nameEntry.EntryType != 0xC1)
                    continue;
                int remaining_len = name_length - length;
                if (remaining_len > 15)
                    remaining_len = 15;
                for (int i=0; i<remaining_len; i++)
                    utf8FileName[length++] = (uint8_t)nameEntry.FileName[i];
//printf("%d %d %d - %x : %x %x", sizeof(fileEntry), sizeof(streamEntry), sizeof(nameEntry), nameEntry.EntryType, nameEntry.FileName[0], nameEntry.FileName[1]);
                // Convert the UTF-16 filename to UTF-8 for printing
//            int res = wcstombs(utf8FileName, (wchar_t *)nameEntry.FileName, 15);
            }
            printf("Name: %s\n", utf8FileName);
            printf("Size: %lu\n", streamEntry.ValidDataLength);
            printf("First Cluster: %u (offset: %lu)\n", streamEntry.FirstCluster, get_cluster_offset(bootSector, streamEntry.FirstCluster));

            // Save the file
            //  - if the file size is > 0
            //  - if the file name doesn't have special characters
            //  - and if it fits in one cluster, so valid FAT is not required
            //  - attempt to save files of the size 4 clusters of smaller - will succeed if they are consecutive
            if ((do_rescue == 1) && (streamEntry.ValidDataLength > 0) && is_valid_string(utf8FileName) && (streamEntry.ValidDataLength <= (4 * cluster_size))) {
                char fname[300];
                char *buf = malloc(streamEntry.ValidDataLength);
                snprintf(fname, sizeof(fname), "%s/%s.%d", rescue_dir, utf8FileName, streamEntry.FirstCluster);
//                strcpy(fname, "rescue.dir/");
//                strcat(fname, utf8FileName);
                printf("Saving %s ...\n", fname);

                long save_imagefile_fpos = ftell(imageFile);
                fseek(imageFile, get_cluster_offset(bootSector, streamEntry.FirstCluster), SEEK_SET);
                if (fread(buf, streamEntry.ValidDataLength, 1, imageFile) != 1) {
                    perror("Error reading file data");
                    fseek(imageFile, save_imagefile_fpos, SEEK_SET);
                    break;
                }
                fseek(imageFile, save_imagefile_fpos, SEEK_SET);

                FILE *outputFile = fopen(fname, "wb");
                if (outputFile == NULL) {
                    perror("Error opening rescue file");
                    break;
                }
                if (fwrite(buf, streamEntry.ValidDataLength, 1, outputFile) != 1) {
                    perror("Error writing file data");
                    fclose(outputFile);
                    break;
                }
                free(buf);
                fclose(outputFile);
                (*files_saved)++;
    	    }
        printf("\n");
        }
        else if (fileEntry.EntryType == 0x00) {
            break; // Last entry
        }
/*        else if (fileEntry.EntryType == 0x81 || fileEntry.EntryType == 0x82 || fileEntry.EntryType == 0x83
            || fileEntry.EntryType == 0x85 || fileEntry.EntryType == 0xa0 || fileEntry.EntryType == 0xa1
            || fileEntry.EntryType == 0xa2 || fileEntry.EntryType == 0xc0 || fileEntry.EntryType == 0xc1) {
	    // skip but keep reading
    	}
        else {
            break; // Stop if unknown
    	}*/

// Skip any remaining secondary entries
//        fseek(imageFile, fileEntry.SecondaryCount * sizeof(fileEntry), SEEK_CUR);
    } // loop over file entries
}

void print_root_directory(FILE *imageFile, ExFatBootSector *bootSector)
{
    parse_directory(imageFile, bootSector, get_root_directory(bootSector), 0, NULL);
}

void print_usage()
{
    fprintf(stderr, "Usage: exfat [-s] [-r] [-d rescue_dir] image_file\n");
}

int main(int argc, char **argv)
{
    int flags, opt;
    int do_search = 0;
    int do_rescue = 0;
    char rescue_dir[256] = "rescue.dir";

    while ((opt = getopt(argc, argv, "srd:"))!= -1) {
        switch(opt) {
        case 's':
            do_search = 1;
            break;
        case 'r':
            do_rescue = 1;
            break;
        case 'd':
            strncpy(rescue_dir, optarg, sizeof(rescue_dir));
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }
    if (optind >= argc) {
        print_usage();
        exit(EXIT_FAILURE);
    }
    const char *imagePath = argv[optind];
    FILE *imageFile = fopen(imagePath, "rb");
    if (imageFile == NULL) {
        perror("Error opening disk image");
        exit(EXIT_FAILURE);
    }

    ExFatBootSector bootSector;
    size_t bytesRead = fread(&bootSector, sizeof(ExFatBootSector), 1, imageFile);
    if (bytesRead != 1) {
        perror("Error reading boot sector");
        fclose(imageFile);
        exit(EXIT_FAILURE);
    }

    // Check the boot signature
    if (bootSector.BootSignature != 0xAA55) {
        fprintf(stderr, "Invalid boot sector signature\n");
        fclose(imageFile);
        exit(EXIT_FAILURE);
    }

    print_layout(&bootSector);
    // Print the root directory contents
    print_root_directory(imageFile, &bootSector);
    if (do_search == 0)
        exit(EXIT_SUCCESS);

    // Search for any directory records
    printf("Searching for any directory entries...\n");
    int files_saved = 0;
    uint64_t cluster = bootSector.FirstClusterOfRootDirectory;
    while(cluster < bootSector.ClusterCount) {
        uint64_t offset = get_cluster_offset(&bootSector, cluster);
        if (cluster % 512 == 0)
            printf("%lu [%lx] (%.3f%%) files saved: %d\n", cluster, offset, (float)cluster / (float)bootSector.ClusterCount * 100.0, files_saved);
        parse_directory(imageFile, &bootSector, offset, do_rescue, rescue_dir, &files_saved);
        cluster++;
    }

    fclose(imageFile);
    return 0;
}

