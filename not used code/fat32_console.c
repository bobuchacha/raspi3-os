#include "ros.h"
#include "fat32_console.h"
#include "filesystem/FAT/fat32.h"
#include "../kernel/hardware/uart/uart.h"
#include "../kernel/memory/paging.h"
#include "../kernel/lib/string.h"

void fat32_console(f32 *fs) {
    printf("\n\nStarting up Fat32 console...\n");
    printf("Reading root directory... ");
    struct directory dir;
    populate_root_dir(fs, &dir);
    printf("Done.\n");

    unsigned int bufflen = 24;

    if (dir.num_entries > 0) {
        printf("Root directory:\n");
        print_directory(fs, &dir);
    }
    else {
        printf("Root directory empty.\n");
    }

    printf("Welcome to the Version 1 Fat32 console.\n");
    printf("Type 'help' to see available commands.\n");

    char buffer[bufflen + 1];
    while (1) {
        printf(">> ");
        unsigned int i;
        for (i = 0; i < bufflen; i++) {
            char c = uart_getc();
            if (c == 8) {            // Backspace
                if (i == 0) {
                    i--;
                    continue;
                }
                uart_send(c);
                i -= 2;
                continue;
            }

            if (c == 4 || c == 27) {        // end of trans or ESC
                i--;
                continue;
            }

            uart_send(c);

            buffer[i] = c;
            if (c == '\n') break;
        }

        if (i == bufflen) {
            printf("\nERROR: Input too long. Press enter to continue.\n");
            while (uart_getc() != '\n');
            continue;
        }
        buffer[i] = 0;

        // If it's just a return, print the current directory.
        if (strlen(buffer) == 0) {
            print_directory(fs, &dir);
            continue;
        }

        unsigned int x;
        int scanned = coerce_int(buffer, &x);
        if(scanned == 0) {
            int command_ret = handle_commands(fs, &dir, buffer);
            if(!command_ret) {
                printf("Invalid input. Enter a number or command.\n");
            }
            else if(command_ret == -1) {
                break;
            }
            continue;
        }

        if(dir.num_entries <= x) {
            printf("Invalid selection.\n");
            continue;
        }

        if(dir.entries[x].dir_attrs & DIRECTORY) {
            // It's a directory. chdir to that one.
            unsigned int cluster = dir.entries[x].first_cluster;
            if(cluster == 0) cluster = 2;
            free_directory(fs, &dir);
            populate_dir(fs, &dir, cluster);
            print_directory(fs, &dir);
            continue;
        }
        else {
            unsigned char *file = readFile(fs, &dir.entries[x]);
//            for(i = 0; i < dir.entries[x].file_size; i++) {
//                printf("%c", file[i]);
//            }
            if (dir.entries[x].file_size > 512) uart_dump(file);
            else uart_dump_size(file, dir.entries[x].file_size);
            kfree(file);
        }
    }
}

int handle_commands(f32 *fs, struct directory *dir, char *buffer) {

}