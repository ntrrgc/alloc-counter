import sys, os, re
import unittest
from collections import namedtuple
from typing import List

from sortedcontainers import SortedDict

Map = namedtuple("Map", ["time", "pointer", "size", "stack_trace_id", "line_number"])
Unmap = namedtuple("Unmap", ["time", "pointer", "size", "line_number"])

re_map = re.compile(r"(\d+\.?\d*) MAP: 0x([0-9a-fA-F]+) \((\d+) bytes\) stackTrace=(\d+)")
re_unmap = re.compile(r"(\d+\.?\d*) UNMAP: 0x([0-9a-fA-F]+) \((\d+) bytes\)")


def last(iter):
    value = None
    for value in iter:
        pass
    return value


def greatest_less_than(sorted_dict, key):
    # bisect_left() finds the index where the key would be inserted to maintain order, picking the left in case of an
    # exact match.
    # This corresponds to the smallest item that has a key larger or equal than `key`, or the length of the dictionary
    # if no such item exists (in which case the item would be added at the end).
    index_where_key_would_be_inserted = sorted_dict.bisect_left(key)
    # The greatest item less than `key` is the immediately preceding one.
    index = index_where_key_would_be_inserted - 1
    if index < 0:
        # There is no preceding item, therefore there is no element less than `key`.
        return None
    key = sorted_dict.keys()[index]
    return sorted_dict[key]


def round_up_page(size):
    return size + (4096 - 1) & ~(4096 - 1)


assert round_up_page(1) == 4096
assert round_up_page(4096) == 4096
assert round_up_page(7500) == 2 * 4096


def parse_mmap_log(log_file):
    for line_number, line in enumerate(log_file.readlines(), 1):
        assert line[-1] == "\n"
        line = line[:-1]
        if " MAP:" in line:
            g = re_map.match(line).groups()
            time = float(g[0])
            map = Map(time, int(g[1], 16), round_up_page(int(g[2])), int(g[3]), line_number)
            assert map.size & (4096 - 1) == 0, map.size
            assert map.size != 0
            yield map
        elif " UNMAP:" in line:
            g = re_unmap.match(line).groups()
            time = float(g[0])
            unmap = Unmap(time, int(g[1], 16), round_up_page(int(g[2])), line_number)
            assert unmap.size & (4096 - 1) == 0, unmap.size
            assert unmap.size != 0
            yield unmap
        else:
            raise RuntimeError


class MMapAllocation:
    def __init__(self, original_start, original_size, stack_trace_id):
        self.original_start = original_start
        self.original_size = original_size
        self.stack_trace_id = stack_trace_id

    def original_end(self):
        return self.original_start + self.original_size


class MemorySlice:
    def __init__(self, start, size, allocation: MMapAllocation):
        self.start = start
        self.size = size
        self.allocation = allocation

    def end(self):
        return self.start + self.size


class MemoryMap:
    def __init__(self):
        self.dict: SortedDict[int, MemorySlice] = SortedDict()

    def register_map(self, allocation: MMapAllocation):
        start = allocation.original_start
        # mmap() can overwrite existing mappings when MAP_FIXED is set (dangerous, but used by WebKit anyway)
        erased_slices = self.register_unmap(allocation.original_start, allocation.original_size)
        added_slice = MemorySlice(start, allocation.original_size, allocation)
        self.dict[start] = added_slice
        # Return (added_slice, erased_slices)
        return added_slice, erased_slices

    def register_unmap(self, start, size) -> List[MemorySlice]:
        end = start + size
        self._split_at(start)
        self._split_at(end)

        keys = list(self.dict.irange(minimum=start, maximum=end, inclusive=(True, False)))
        erased_slices = []
        for key in keys:
            erased_slices.append(self.dict[key])
            del self.dict[key]
        return erased_slices

    def _split_at(self, pointer):
        candidate: MemorySlice = greatest_less_than(self.dict, pointer)
        if candidate is None:
            return
        assert candidate.start < pointer
        if pointer < candidate.end():
            assert pointer not in self.dict

            left_side_size = pointer - candidate.start
            right_side_size = candidate.end() - pointer

            candidate.size = left_side_size
            self.dict[pointer] = MemorySlice(pointer, right_side_size, candidate.allocation)


def generate_memory_log(event_log_file, output_file, stack_filter=lambda event: True):
    print("Time\tMemory", file=output_file)
    memory_map = MemoryMap()
    accum_memory = 0
    last_time = 0

    for event in parse_mmap_log(event_log_file):
        # Print only one line per time value
        if event.time > last_time:
            print(f"{last_time}\t{accum_memory}", file=output_file)
            last_time = event.time

        if isinstance(event, Map):
            added_slice, erased_slices = memory_map.register_map(MMapAllocation(event.pointer, event.size, event.stack_trace_id))
            accum_memory += (added_slice.size if stack_filter(added_slice.allocation.stack_trace_id) else 0) - sum(
                x.size for x in erased_slices if stack_filter(x.allocation.stack_trace_id))
        elif isinstance(event, Unmap):
            erased_slices = memory_map.register_unmap(event.pointer, event.size)
            # At least one slice should have been deleted... Otherwise the event should not have been logged.
            assert len(erased_slices) > 0
            assert sum(x.size for x in erased_slices) <= event.size
            accum_memory -= sum(x.size for x in erased_slices if stack_filter(x.allocation.stack_trace_id))
        else:
            raise RuntimeError
    # Per the rule above, we've yet to print the memory for the last time mark:
    print(f"{last_time}\t{accum_memory}", file=output_file)


def calculate_memory_by_stack_id(memory_map):
    memory_by_stack_id = {}
    for memory_slice in memory_map.dict.values():
        memory_by_stack_id.setdefault(memory_slice.allocation.stack_trace_id, 0)
        memory_by_stack_id[memory_slice.allocation.stack_trace_id] += memory_slice.size
    return memory_by_stack_id


def generate_ranking(event_log_file):
    memory_map = MemoryMap()

    memory_by_stack_id_baseline = None
    for event in parse_mmap_log(event_log_file):
        if event.time >= 2 * 3600 and not memory_by_stack_id_baseline:
            memory_by_stack_id_baseline = calculate_memory_by_stack_id(memory_map)

        if isinstance(event, Map):
            memory_map.register_map(MMapAllocation(event.pointer, event.size, event.stack_trace_id))
        elif isinstance(event, Unmap):
            erased_slices = memory_map.register_unmap(event.pointer, event.size)
            # At least one slice should have been deleted... Otherwise the event should not have been logged.
            assert len(erased_slices) > 0
            assert sum(x.size for x in erased_slices) <= event.size
        else:
            raise RuntimeError

    memory_by_stack_id_end = calculate_memory_by_stack_id(memory_map)

    memory_increase_by_stack_id = {}
    for stack_id, memory_usage_end in memory_by_stack_id_end.items():
        memory_usage_baseline = memory_by_stack_id_baseline.get(stack_id, 0)
        memory_increase_by_stack_id[stack_id] = memory_usage_end - memory_usage_baseline

    worst_stacks = sorted(memory_increase_by_stack_id.items(), key=lambda x: -x[1])
    for stack_id, memory_usage in worst_stacks:
        print(f"{stack_id}\t{memory_usage} ({memory_usage/1e6} MB)")


def main():
    generate_memory_log(open("mmap-event-log", "r"), open("mmap-memory-usage.tsv", "w"))
    generate_memory_log(open("mmap-event-log", "r"), open("mmap-memory-usage-filtered.tsv", "w"),
                        stack_filter=lambda s: s in {125})
    generate_ranking(open("mmap-event-log", "r"))


class TestSortedDict(unittest.TestCase):
    def test_insert_and_delete(self):
        d = SortedDict()
        self.assertEqual(0, len(d))
        d[1] = "a"
        self.assertEqual(1, len(d))
        with self.assertRaises(KeyError):
            del d[2]
        del d[1]
        self.assertEqual(0, len(d))

    def test_sorted_search(self):
        d = SortedDict([(10, "10"), (20, "20"), (30, "30")])
        self.assertEqual("10", d[10])
        self.assertEqual([20], list(d.irange(minimum=15, maximum=30, inclusive=(True, False))))
        self.assertEqual("30", greatest_less_than(d, 31))
        self.assertEqual("20", greatest_less_than(d, 30))
        self.assertEqual("20", greatest_less_than(d, 25))
        self.assertEqual("10", greatest_less_than(d, 11))
        self.assertEqual(None, greatest_less_than(d, 10))
        self.assertEqual(None, greatest_less_than(d, 9))
        self.assertEqual(None, greatest_less_than(SortedDict(), 10))


class TestMemoryMap(unittest.TestCase):
    def test_ranges_are_stored(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        self.assertIn(10, m.dict)
        self.assertEqual(10, m.dict[10].start)
        self.assertEqual(20, m.dict[10].size)
        self.assertEqual(30, m.dict[10].end())

    def test_splitting_in_the_middle(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m._split_at(20)
        self.assertEqual(10, m.dict[10].start)
        self.assertEqual(20, m.dict[10].end())
        self.assertEqual(20, m.dict[20].start)
        self.assertEqual(30, m.dict[20].end())

    def test_splitting_outside(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m._split_at(5)
        self.assertEqual(1, len(m.dict))
        m._split_at(35)
        self.assertEqual(1, len(m.dict))
        m._split_at(30)
        self.assertEqual(1, len(m.dict))
        m._split_at(10)
        self.assertEqual(1, len(m.dict))

    def test_simple_deallocation(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m.register_unmap(10, 20)
        self.assertEqual(0, len(m.dict))

    def test_partial_deallocation_middle(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m.register_unmap(15, 5)
        self.assertEqual(2, len(m.dict))
        self.assertEqual(10, m.dict[10].start)
        self.assertEqual(15, m.dict[10].end())
        self.assertEqual(20, m.dict[20].start)
        self.assertEqual(30, m.dict[20].end())

    def test_partial_deallocation_start(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m.register_unmap(10, 5)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(15, m.dict[15].start)
        self.assertEqual(30, m.dict[15].end())

    def test_partial_deallocation_end(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m.register_unmap(25, 5)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(10, m.dict[10].start)
        self.assertEqual(25, m.dict[10].end())

    def test_outer_deallocation(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        self.assertEqual(1, len(m.dict))
        m.register_unmap(5, 30)
        self.assertEqual(0, len(m.dict))

    def test_double_deallocation(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        m.register_map(MMapAllocation(0, 10, 1))
        self.assertEqual(2, len(m.dict))
        m.register_unmap(0, 30)
        self.assertEqual(0, len(m.dict))

    def test_outer_double_deallocation(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(20, 20, 1))
        m.register_map(MMapAllocation(10, 10, 1))
        m.register_map(MMapAllocation(1, 5, 1))
        self.assertEqual(3, len(m.dict))
        m.register_unmap(7, 50)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(1, m.dict[1].start)
        self.assertEqual(6, m.dict[1].end())

    def test_double_deallocation_partial_start(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        m.register_map(MMapAllocation(0, 10, 1))
        self.assertEqual(2, len(m.dict))
        m.register_unmap(0, 15)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(15, m.dict[15].start)
        self.assertEqual(30, m.dict[15].end())

    def test_double_deallocation_with_hole_partial_start(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        m.register_map(MMapAllocation(0, 5, 1))
        self.assertEqual(2, len(m.dict))
        m.register_unmap(0, 15)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(15, m.dict[15].start)
        self.assertEqual(30, m.dict[15].end())

    def test_double_deallocation_partial_end(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        m.register_map(MMapAllocation(0, 10, 1))
        self.assertEqual(2, len(m.dict))
        m.register_unmap(0, 25)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(25, m.dict[25].start)
        self.assertEqual(30, m.dict[25].end())

    def test_double_deallocation_with_hole_partial_end(self):
        m = MemoryMap()
        m.register_map(MMapAllocation(10, 20, 1))
        m.register_map(MMapAllocation(0, 5, 1))
        self.assertEqual(2, len(m.dict))
        m.register_unmap(0, 25)
        self.assertEqual(1, len(m.dict))
        self.assertEqual(25, m.dict[25].start)
        self.assertEqual(30, m.dict[25].end())


if __name__ == '__main__':
    main()
    # unittest.main()
