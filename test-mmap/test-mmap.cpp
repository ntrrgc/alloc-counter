#include <sys/mman.h>
#include <stdio.h>
#include <cassert>

int main() {
    printf("Hi\n");
    void* addr = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    printf("Mapped region: %p\n", addr);
    assert(addr);
    munmap(addr, 4096);
    printf("Bye\n");
    return 0;
}
