//
// Created by Thang Cao on 5/23/24.
//
// void mmu_init();

void kernel_main(){
    // mmu_init();
    asm volatile("___breakpoint:");
    while(1);
}