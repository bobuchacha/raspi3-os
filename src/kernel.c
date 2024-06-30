//
// Created by Thang Cao on 5/23/24.
//
#include "../include/arch/cortex-a53/boot/uart1.h"
#include "../include/printf.h"
#include "../include/device.h"
#include "../include/irq.h"            // some code in device irq or arch irq
#include "../include/log.h"
#include "../include/task.h"
#include "../include/timer.h"
#include "../include/memory.h"
#include "hal/hal.h"
#include "filesystem/filesystem.h"
#include "filesystem/vfs/vfs.h"
#include "elf.h"

// file.c
void file_close(PFileDesc f);
Buffer file_read(PFileDesc f, int position, int length);
PFileDesc file_open(char* path, int mode);

extern int file_last_status;

void ls(unsigned char *path){
    struct FileDesc dir;
    struct DirectoryEntry dirent;
    if (vfs_dir_open(&dir, path) == SUCCESS){
        kprint("\n\n====================== Directory Content: [%s] ====================\n", path);
        UByte *n;
        int i = 0;
        while (vfs_dir_read_ex(&dir, &dirent)){
            if (!str_from_utf16(dirent.long_name, 255, &n)) {
                kprint("%3d   ", i++);
                kprint("%2d   ", dirent.attr);
                kprint("%02d/%02d/%04d ", dirent.create_date.month, dirent.create_date.day, dirent.create_date.year + 1980);
                kprint("%02d:%02d:%02d   ", dirent.create_time.hour, dirent.create_time.minute, dirent.create_time.second);
                kprint("%6d   ", dirent.size);
                kprint("%s\n", n);
                kfree((Address)n);
            }
        };
        kprint("\n");
    }else{
        kerror("Folder not found");
    }

}
void kernel_load_user_elf(){
    ls("/");

    _trace("Reading file deadbeef.txt");
    PFileDesc f = file_open("/bar.txt", O_READ);
    if (!f) {
        log_error("Error opening file. Error %d", file_last_status);
        return;
    }
    Buffer content = file_read(f, 0, f->size);
    kdump_size(content, f->size);


    // _trace("Starting process\n");

    // _trace("Opening file\n");    

	// struct FileDesc dir;
	// struct FileDesc fd;
	// if (vfs_fd_open(&fd, "/user.elf", O_READ) < 0){
	// 	kerror("File User.elf not found");
	// }
    // _trace("file opened %d\n", 0);

    // Elf64_Ehdr elf;
	// if (vfs_fd_read(&fd, (char*)&elf, sizeof(elf)) != sizeof(elf)) {
	// 	vfs_fd_close(&fd);
    //     kerror("Can not read ELF header");
	// }

    // // we got our entry at *entry = elf.entry;
    // Elf64_Phdr ph;
	// unsigned int sz = 0;
	// for (unsigned int off = elf.e_phoff; off < elf.e_phoff + elf.e_phnum * sizeof(ph);
	// 	 off += sizeof(ph)) {
	// 	if (vfs_fd_seek(&fd, off, SEEK_SET) < 0) {
	// 		vfs_fd_close(&fd);
	// 		kerror("Can not read pheader");
    //         while(1);
	// 	}
	// 	if (vfs_fd_read(&fd, (char*)&ph, sizeof(ph)) != sizeof(ph)) {
    //         kerror("Can not read pheader 2");
	// 		vfs_fd_close(&fd);
	// 	}
	// 	if (ph.p_type != PT_LOAD) {
	// 		continue;
	// 	}
    //     kprint("Program header  %d\n", off);
    //     kprint(" - Align:       0x%lX\n", ph.p_align);
    //     kprint(" - File Size:   %d\n", ph.p_filesz);
    //     kprint(" - Mem Size:    %d\n", ph.p_memsz);
    //     kprint(" - Offset:      %d\n", ph.p_offset);
    //     kprint(" - Phys Addr:   0x%lX\n", ph.p_paddr);
    //     kprint(" - Virt Addr:   0x%lX\n", ph.p_vaddr);
    //     kprint(" - Flags:       %d\n", ph.p_flags);


//    ls ("/");
//    // // // make dir here
//    // _trace("Making new firestory");
//    int ret = vfs_file_remove("/test.txt");
//    kprint("Removing file returned %d", ret);
//
//
//    struct FileDesc fr;
//    ret = vfs_fd_open(&fr, "/test file with a very long name.txt", O_READ | O_WRITE | O_APPEND);
//    kprint("Creating file returned %d\n", ret);
//
//    ret = vfs_fd_write(&fr, "Welcome to ROS\n", 16);
//    char buf[512];
//
//    vfs_fd_seek(&fr, 0, SEEK_SET);
//    vfs_fd_read(&fr, buf, fr.size);
//    kprint("Write to file returned %d\n", ret);
//
//    vfs_fd_close(&fr);
//
//    ls ("/");
//    kdump(buf);

    // struct FileDesc fd;
    
    // if (vfs_fd_open(&fd, "/This is a long name Directory/My Content.txt", O_READ) != SUCCESS) {
    //     kerror("Can not find /This is a long name Directory/My Content.txt");
    // }else {

    //     unsigned char buf[256];
    //     vfs_fd_read(&fd, buf, fd.size);
    //     kdump_size(buf, fd.size);
    // }


    // long name conversion
	// unsigned char* filename = "Cao Duc Thang is number 1.txt";
    // UWord longname[255];
	// int name_length = strlen(filename);
	// int lfn_entry_count = (name_length / 13)+ (name_length % 13 != 0);
	// utf8_to_utf16(filename, name_length, longname, 255);
	
    // kdebug("name length: %d, entry count: %d", name_length, lfn_entry_count);


		// if (allocuvm(pgdir, base + ph.vaddr - ph.vaddr % PGSIZE, base + ph.vaddr + ph.memsz,
		// 			 (ph.flags & ELF_PROG_FLAG_WRITE) ? PTE_W : 0) == 0) {
		// 	vfs_fd_close(&fd);
		// 	return -1;
		// }
		// if (ph.vaddr + ph.memsz > sz) {
		// 	sz = ph.vaddr + ph.memsz;
		// }
		// if (loaduvm(pgdir, (char*)base + ph.vaddr - ph.vaddr % PGSIZE, &fd,
		// 			ph.off - ph.vaddr % PGSIZE, ph.vaddr % PGSIZE + ph.filesz) < 0) {
		// 	vfs_fd_close(&fd);
		// 	return -1;
		// }
	// }
    

}

void do_test();

void kernel_main(){
    hal_init();        
    
    // need to initialize device before output anything
    device_init();

    // init memory management
    log_info("Enabling memory management...\n");
    init_memory_management();

    // init file system
    filesystem_init();

    // test
    do_test();

    // create thread
    process_copy_thread(PF_KTHREAD, (Address)&kernel_load_user_elf, 0);

//    extern void *user_begin, *user_end;
//    create_user_process((Address)(&user_begin), &user_end-&user_begin);

    while(1){
         kinfo("Printing from thread %s. Yeilding...", current_task->name);
        
        // cleanup_zombie_processes();
        schedler_schedule();
        // delay(1000000000);
    }
}