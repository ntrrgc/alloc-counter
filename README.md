A lightweight memory tracer for non-interactive applications
============================================================

This is a small tool set for tracking memory increases in applications, especially those that happen in absence of user interaction (e.g. video playback). It is designed for minimum impact on run time even on big applications like web browsers and is designed with embedded devices in mind.

The main tool is `alloc-counter`, a `LD_PRELOAD`'able library that tracks the lifetime of memory allocations and reports those that exceed a configurable timespan without been freed or accessed as potential leaks.

alloc-counter only starts tracking allocations when the user gives a start signal by invoking `alloc-counter-start`, so allocations made during the startup of the application or the interaction of the user needed to start executing the leaky code have marginal overhead and don't generate false positives.

System requirements
-------------------

* Linux-based operating system.
* GCC compiler.
* A working libunwind backend (alternatively you can modify `StackTrace.cpp` to do your own unwinding).
* The software has been tested in x86_64 and ARM, but there is no platform-specific code.

Usage of alloc-counter
----------------------

1. Make sure the tool works with the provided examples. In particular, make sure stack unwinding works. More details are provided in the sections following.

2. Have a look at the application you are going to run alloc-counter with and run through this check list:

    * Make sure stack unwinding will work for your application. If you needed to add special flags for unwinding in alloc-counter, you'll likely need to rebuild your application **and all the libraries it may use (even transitively!) once given the start signal**. Also make sure that any special features of the application using assembly or JIT compilation (e.g. some JavaScript interpreters) don't interfere with stack unwinding.

      Depending on the unwinding method, the impact of mixing function calls unaware of the unwinding convention may range from the stack trace being truncated (e.g. with table-based unwinding) on the top-most unaware call frame to undefined behavior, including crashes (e.g. if unwinding is based on a frame pointer register that uncooperating code may mangle accidentally).

    * If the application makes use of memory pools or a custom memory allocator, disable it or make it passthrough so that one call to your custom allocation function always triggers one call to `malloc()` and one call to your custom free function always triggers, without delay a call to `free()` with the same pointer.

      Bear in mind that stack traces for suspicious allocations are recorded on `malloc()` calls; if the memory is later reused by a different code that in turn leaks the object, the leak stack trace will point to the creation of the first, completely unrelated object. Also, even though any initial user allocation could later be reused by the same leaky code, alloc-counter can't know this as in its eyes the allocation stack traces are completely different, so the leaks should have been caused by different code.

      This is even more notable in the case where your custom memory allocator gets a big chunk of memory from `malloc()` and places data structures inside to slice it for its clients. On eyes of alloc-counter the entire big chunk is a single allocation that is accessed very often, even if the user of the custom allocator is actually leaking inside that big chunk.

      C++ note: `new` and `delete` in GCC and many compilers use `malloc()` and `free()` internally (in addition to running constructors or destructors). You only need to worry about `new` or `delete` if they are overwritten in a problematic way like explained before.

      A rare exception to this rule is the case where you want to trace leaks/memory increases in the custom memory allocator itself.

3. Choose values for the configuration variables. **DO NOT EXPECT DEFAULT VALUES TO WORK FOR YOUR APPLICATION**. Bear in mind that different applications have hugely different timespans for their allocations. On one hand, when running certain number crunching algorithms we may consider any allocation alive and untouched for more than 5 seconds to be a definitive leak. On the other hand, a video player in a device with plenty of memory is expected to buffer video frames and other metadata for many minutes.

4. Run the application with the required environment variables. In particular, set `LD_PRELOAD` to the path of `liballoc-counter.so` and set all the necessary configuration environment variables.

5. If your application requires any interaction before the leak shows up, perform that interaction.

6. Once you know the application is continuously leaking, run `alloc-counter-start`.

7. The application should seem to continue running normally, but all the newly made allocations are being instrumented. From this point on two logs will be written:

    * `/tmp/alloc-report` is written every 5 seconds, reporting the average amount of malloc's and free's since the start signal was given and writing stack traces of potential leaks as soon as they are found. This gives you quick feedback on what's going on with alloc-counter and allows you to make sure it's running as expected.

    * `/tmp/leak-report` has a *leak report* written every 30 seconds (although this time interval is configurable). Every leak report ranks the found leaks per estimated memory loss and prints their stack traces, all together without noise. In general, at any time you are only interested in the last report found in this file. Now just let the application and alloc-counter running for enough time to find the leaks and then check this report.

How does it work?
-----------------

**Wrapping:** Since liballoc-counter.so is loaded with `LD_PRELOAD`, its symbols take precedence, so all calls to malloc() and similar functions are attended by it. The first time it receives such call, it uses `dlsym` to get the `malloc` function from the next library in precedence order (usually glibc's).

**Library context:** A thread-local singleton, `LibraryContext` ensures that allocations made internally by alloc-counter algorithms are forwarded immediately to the underlying allocator without being instrumented or entering infinite recursion.

**Patrol thread:** The search for potential leaks and reporting is done in a separate thread spawned on startup. This *patrol thread* scans the allocation tables every 5 seconds. Reports are written in the same loop, after the allocation table mutex has been unlocked.

### Callstack fingerprints

Unfortunately, getting stack traces (even just return pointers) can be quite costly in calling conventions that don't use traversable frame pointers. But alloc-counter needs to be able to tell if two allocations come from the same code path in order to know how much memory is leaked by that code path.

A compromise is found with callstack fingerprints: unreliable but fast to compute numbers that are computed as a hash of the position of the top of the stack at the moment of the allocation call, the return pointer and the mapping of the requested allocation size to a size class.

Two allocations with different callstack fingerprints are assumed to be caused by different code paths. On the other hand, two allocations with the same callstack fingerprint *may or may not* come from the same code path.

The gain comes from the assumption that leaky code will leak again, with the same call tree and therefore the same callstack fingerprint. The first time an allocation is determined to be suspicious, its callstack fingerprint is marked as suspicious too. From then on, new allocations coming from code paths with the same callstack fingerprint will have their full stack trace captured.

Since most code does not leak and callstack fingerprints are able to differentiate at worst hundreds of allocations, much fewer unwinding operations are required, so it's no longer a problem if unwinding is a bit slow.

### Kinds of allocations

When a memory allocation is done in alloc-counter, there are three possible levels of instrumentation it may receive:

* No instrumentation at all: This is the case for all allocations before the start signal is emitted. It's also the case when it is determined that observing an allocation will not lead to new data or doing so would surpass the limit of closely watched allocations (explained later).

* Light allocations: They are forwarded to the underlying allocator without changes, but in addition to that, an ancillary record is stored in `AllocationTable::m_lightAllocations`. This record contains the memory pointer, the requested size, a deadline and a callstack fingerprint. This record is erased when the memory is freed. If the thread patrol find such a record still exists past its deadline, its callstack fingerprint is marked as suspicious -- but not yet declared a leak.

* Closely watched allocations: When the application requests again an allocation with a callstack fingerprint that has been reported suspicious a closely watched allocation is used instead of a light allocation. A full stack trace is required. The allocation size is bumped to the next multiple of memory page (usually 4096 bytes). The ancillary record is stored in `AllocationTable::m_closelyWatchedAllocations` and contains the memory pointer, the requested size (less or equal to the actual size), a deadline, a stack trace and a suspicion state.

After finding the callstack fingerprint to be suspicious, a stack trace is computed and searched for existing matches. For every stack trace recorded in the investigation a `WatchedStackTraceInfo` object is created. This is the only object storing the stack trace other than as a temporary. This object records the outcomes of allocations coming from that stack trace. This way, if after `ALLOC_ENOUGH_SAMPLES_TO_PROVE_NO_LEAK` tracked allocations all of them were successfully freed, the stack trace is considered non-leaky and further allocations made from it will not be instrumented, freeing resources to be used in other more suspicious allocations.

Since closely watched allocations are expensive there are limits to how many of them can there be in existence at the same time, both for each stack trace (`ALLOC_MAX_CLOSELY_WATCHED`) and globally (`ALLOC_GLOBAL_MAX_CLOSELY_WATCHED`). Should an allocation be made while the limit has been reached, it will not be instrumented, but it will still be counted and an extrapolation will be made in the leak report. For instance, if the stack trace limit for a given stack trace is 30 and 90 allocations of 1 MiB each are made in series, only the first 30 will be closely watched; but the report will still state that 90 MiB were allocated. Should half of the 30 closely watched be deemed leaked and the other half not leaky, 45 MiB will be reported as leaked.

### The life of a closely watched allocation

The end of a closely watched allocation is to either prove not to be leaked by being freed, or to be proven a potential leak for not being freed nor accessed in a long time.

The first part is quite easy: should a `free()` occur, it's definitively not leaked. It's the second part that is more tricky. We can't continually monitor the memory as the performance hit would be unacceptable, so instead there are two states a closely allocation can be in:

* Not (yet) suspicious: The allocation is born in this state. When the deadline is hit, it becomes suspicious. The initial deadline is set to `ALLOC_TIME_SUSPICIOUS` seconds. Successive entrances in this state get `ALLOC_REST_TIME` seconds instead.

* Suspicious: A suspicious allocation is watched by the memory protector. Should a read or write access occur in the memory area of the allocation during this state, the protection is cleared and the allocation becomes unsuspicious again. The deadline is set to `ALLOC_MAX_ACCESS_INTERVAL` seconds; if hit, the allocation is declared a potential leak.

The memory protection consists on using `mprotect()` on the memory areas covered by the allocation. Successive accesses trigger a SIGSEGV signal that is handled by alloc-counter, who updates the allocation info, changes the protection status of the memory and allows the code to continue.

**WIP Note:** Memory protection has not been completely tested in WebKit and remains [in a branch](https://github.com/ntrrgc/alloc-counter/tree/memory-protector). When working in a branch without this feature, suspicious allocations will always be declared leaks.
