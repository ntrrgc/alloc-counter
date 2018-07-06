#include "comm-memory.h"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>

// For now this is a pointer to dummy array. When the library is initialized, it's replaced to a mmap'ed file
// in /tmp/alloc-comm where the user can write an integer to enter Watching state.
static WatchState dummyCommMemory[] = { WatchState::NotWatching };
WatchState * __commMemory = dummyCommMemory;

void initCommMemory()
{
    int fd = open("/tmp/alloc-comm", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1)
        abort();
    int32_t value = static_cast<int32_t>(WatchState::NotWatching);
    if (sizeof(value) != write(fd, &value, sizeof(value)))
        abort();

    __commMemory = static_cast<WatchState*>(mmap(nullptr, 4, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0));
    if (!__commMemory)
        exit(errno);

    close(fd);
}
