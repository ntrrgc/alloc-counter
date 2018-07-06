#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main() {
    FILE* fp = fopen("/tmp/alloc-comm", "r+b");
    if (!fp) {
        perror("Could not open communication file");
        exit(1);
    }
    int32_t value = 1; // WatchState::Watching
    int written = fwrite(&value, sizeof(value), 1, fp);
    if (written != 1) {
        perror("Failed writing to communication file");
        exit(1);
    }
    fclose(fp);
    return 0;
}
