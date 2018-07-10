#pragma once
#include <functional>
#include <mutex>
#include <set>
#include <signal.h>
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>
#include "environment.h"
using namespace std;

class PointerRange {
public:
    void* start;
    void* end;
    std::function<void()> onAccess;

    bool operator<(const PointerRange& other) const {
        return start < other.start;
    }

    size_t size() const {
        return (char*)end - (char*)start;
    }
};

class PointerRangeList: public std::set<PointerRange> {
public:
    iterator findContainingPointer(void* pointer) {
        for (iterator i = begin(); i != end(); ++i) {
            const PointerRange& range = *i;
            if (range.start <= pointer && pointer < range.end)
                return i;
        }
        return end();
    }
    bool contains(void* pointer) const {
        for (const PointerRange& range : *this) {
            if (range.start <= pointer && pointer < range.end)
                return true;
        }
        return false;
    }
};

class MemoryProtector {
public:
    static void initialize() {
        assert(!s_instance);
        s_instance = new MemoryProtector();
    }
    static MemoryProtector& instance() {
        assert(s_instance);
        return *s_instance;
    }

    // Called only from patrol thread.
    void watchRange(void* _start, size_t size, function<void()> onAccess) {
        char* start = (char*) _start;
        assert((uintptr_t) start == ((uintptr_t) start & ~(environment.pageSize - 1))); // page aligned
        {
            lock_guard<mutex> lock(mutex);
            // No intersections:
            assert(!m_watchedPages.contains(start));
            assert(!m_watchedPages.contains(start + size));
            PointerRange allocationRange { start, start + size, onAccess };
            m_watchedPages.insert(allocationRange);

            if (0 != mprotect(start, size, PROT_NONE)) {
                perror("watchRange");
                abort();
            }
        }
    }

    // Called only from patrol thread.
    void removeWatch(void* start) {
        lock_guard<mutex> lock(mutex);
        PointerRangeList::iterator rangeIter = m_watchedPages.findContainingPointer(start);
        if (rangeIter != m_watchedPages.end()) {
            if (0 != mprotect(rangeIter->start, rangeIter->size(), PROT_READ | PROT_WRITE)) {
                perror("removeWatch");
                abort();
            }
            m_watchedPages.erase(rangeIter);
        }
    }

private:
    static MemoryProtector* s_instance;
    mutex m_mutex;
    PointerRangeList m_watchedPages;
    struct sigaction oldSigAction;

    MemoryProtector() {
        struct sigaction newSigAction;
        newSigAction.sa_sigaction = MemoryProtector::segfaultHandlerWrapper;
        newSigAction.sa_flags = SA_SIGINFO | SA_NODEFER;

        // If our segfault handler has a bug, we want to catch it as usual,
        // but otherwise we want no signals to interrupt the signal handler.
        sigfillset(&newSigAction.sa_mask);
        sigdelset(&newSigAction.sa_mask, SIGILL);
        sigdelset(&newSigAction.sa_mask, SIGBUS);
        sigdelset(&newSigAction.sa_mask, SIGFPE);
        sigdelset(&newSigAction.sa_mask, SIGSEGV);
        sigdelset(&newSigAction.sa_mask, SIGPIPE);
        sigdelset(&newSigAction.sa_mask, SIGSTKFLT);

        if (0 != sigaction(SIGSEGV, &newSigAction, &oldSigAction)) {
            perror("MemoryUsageWatcher: could not set up signal handler");
            abort();
        }
    }

public:
    void segfaultHandler(void* accessedAddress) {
        static thread_local bool insideSegfaultHandler = false;
        static thread_local void* accessedAddressParentHandler = nullptr;
        if (insideSegfaultHandler) {
            // Segfault on segfault, this either is caused by:
            if (accessedAddress != accessedAddressParentHandler) {
                // a) A bug in this signal handler, who accessed an invalid pointer accidentally.
                static const char msg[] = "MemoryUsageWatcher: Internal segmentation fault\n";
                write(STDERR_FILENO, msg, sizeof(msg));
            } else {
                // b) A bug in the application, who accessed an invalid pointer accidentally,
                //    reaching this handler, who after ensuring that the pointer was not covered
                //    but a watched, mprotect()'ed range, decided to access it to check whether
                //    it was because the region was already unprotected by another thread that
                //    got the lock just before us or it was in fact invalid memory and turned out
                //    to be the latter.
            }
            // Either way, we have to abort for real.
            sigaction(SIGSEGV, &oldSigAction, nullptr);
            raise(SIGSEGV);
        }
        insideSegfaultHandler = true;
        accessedAddressParentHandler = accessedAddress;

        // TODO Check if we inside of real malloc in this thread. If that's the
        // case, abort immediately before more damage is done (e.g. by the code
        // following, which may use malloc()/free().

        // The watched pages table can't be read and modified at the same time.
        // Also, if two threads access the same page simultaneously, this
        // ensures that only one executes the `onAccess()` callback.
        lock_guard<mutex> lock(mutex);
        PointerRangeList::iterator rangeIter = m_watchedPages.findContainingPointer(accessedAddress);
        if (rangeIter != m_watchedPages.end()) {
            if (0 != mprotect(rangeIter->start, rangeIter->size(), PROT_READ | PROT_WRITE)) {
                perror("segfaultHandler");
                abort();
            }
            rangeIter->onAccess();
            m_watchedPages.erase(rangeIter);
        } else {
            // The address is not in the table of watched ranges. It may have
            // been removed by another thread that acquired the lock before us,
            // or maybe it's just a buggy pointer from the application.
            // How can we know? Just access the pointer. In the former case, it
            // will do nothing, in the latter, it will segfault again.
            volatile char *pointer = (char*) accessedAddress;
            *pointer;
        }

        insideSegfaultHandler = false;
    }

    static void segfaultHandlerWrapper(int signum, siginfo_t* siginfo, void*) {
        instance().segfaultHandler(siginfo->si_addr);
    }
};
