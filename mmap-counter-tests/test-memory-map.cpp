#define MMAP_COUNTER_TESTS
#include "memory-map.h"
#include <gtest/gtest.h>

class MemoryMapTest: public ::testing::Test {
};

TEST_F(MemoryMapTest, RangesAreStored) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    EXPECT_NE(map.find(10), map.end());
    EXPECT_EQ(map.at(10).start, 10);
    EXPECT_EQ(map.at(10).size, 20);
    EXPECT_EQ(map.at(10).end(), 30);
}

TEST_F(MemoryMapTest, SplittingInTheMiddle) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.splitAt(20);
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map.at(10).start, 10);
    EXPECT_EQ(map.at(10).end(), 20);
    EXPECT_EQ(map.at(20).start, 20);
    EXPECT_EQ(map.at(20).end(), 30);
}

TEST_F(MemoryMapTest, SplittingOutside) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.splitAt(5);
    EXPECT_EQ(map.size(), 1);
    map.splitAt(35);
    EXPECT_EQ(map.size(), 1);
    map.splitAt(30);
    EXPECT_EQ(map.size(), 1);
    map.splitAt(10);
    EXPECT_EQ(map.size(), 1);
}

TEST_F(MemoryMapTest, SimpleDeallocation) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.registerUnmap(10, 20);
    EXPECT_EQ(map.size(), 0);
}

TEST_F(MemoryMapTest, PartialDeallocationMiddle) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.registerUnmap(15, 5);
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map.at(10).start, 10);
    EXPECT_EQ(map.at(10).end(), 15);
    EXPECT_EQ(map.at(20).start, 20);
    EXPECT_EQ(map.at(20).end(), 30);
}

TEST_F(MemoryMapTest, PartialDeallocationStart) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.registerUnmap(10, 5);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(15).start, 15);
    EXPECT_EQ(map.at(15).end(), 30);
}

TEST_F(MemoryMapTest, PartialDeallocationEnd) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.registerUnmap(25, 5);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(10).start, 10);
    EXPECT_EQ(map.at(10).end(), 25);
}

TEST_F(MemoryMapTest, OuterDeallocation) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    EXPECT_EQ(map.size(), 1);
    map.registerUnmap(5, 30);
    EXPECT_EQ(map.size(), 0);
}

TEST_F(MemoryMapTest, DoubleDeallocation) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    map.registerMap(MMapAllocation(0, 10));
    EXPECT_EQ(map.size(), 2);
    map.registerUnmap(0, 30);
    EXPECT_EQ(map.size(), 0);
}

TEST_F(MemoryMapTest, OuterDoubleDeallocation) {
    MemoryMap map;
    map.registerMap(MMapAllocation(20, 20));
    map.registerMap(MMapAllocation(10, 10));
    map.registerMap(MMapAllocation(1, 5));
    EXPECT_EQ(map.size(), 3);
    map.registerUnmap(7, 50);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(1).start, 1);
    EXPECT_EQ(map.at(1).end(), 6);
}

TEST_F(MemoryMapTest, DoubleDeallocationPartialStart) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    map.registerMap(MMapAllocation(0, 10));
    EXPECT_EQ(map.size(), 2);
    map.registerUnmap(0, 15);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(15).start, 15);
    EXPECT_EQ(map.at(15).end(), 30);
}

TEST_F(MemoryMapTest, DoubleDeallocationWithHolePartialStart) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    map.registerMap(MMapAllocation(0, 5));
    EXPECT_EQ(map.size(), 2);
    map.registerUnmap(0, 15);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(15).start, 15);
    EXPECT_EQ(map.at(15).end(), 30);
}

TEST_F(MemoryMapTest, DoubleDeallocationPartialEnd) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    map.registerMap(MMapAllocation(0, 10));
    EXPECT_EQ(map.size(), 2);
    map.registerUnmap(0, 25);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(25).start, 25);
    EXPECT_EQ(map.at(25).end(), 30);
}

TEST_F(MemoryMapTest, DoubleDeallocationWithHolePartialEnd) {
    MemoryMap map;
    map.registerMap(MMapAllocation(10, 20));
    map.registerMap(MMapAllocation(0, 5));
    EXPECT_EQ(map.size(), 2);
    map.registerUnmap(0, 25);
    EXPECT_EQ(map.size(), 1);
    EXPECT_EQ(map.at(25).start, 25);
    EXPECT_EQ(map.at(25).end(), 30);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
