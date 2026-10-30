// CS375 - Lab 9 - Regression tests for memory_simulator_advanced.cpp
// Author: Santiago Soto
// Contributions: Each test pins down one defect found in the starter code.
//
// Run with: make test

#define main simulator_main
#include "../src/memory_simulator_advanced.cpp"
#undef main

#include <sstream>

static int checks = 0, failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            failures++;                                                      \
            std::cout << "FAILED line " << __LINE__ << ": " #condition "\n"; \
        }                                                                    \
    } while (0)

template <typename Exception, typename Function>
static bool throwsType(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

static SegmentTable* makeTable(int frames, bool lru, int tlb = 4, int pageSize = 1000) {
    SegmentTable* st = new SegmentTable(tlb, frames, lru, pageSize, 1, 4, "");
    st->addSegment(0, 0, 16, READ_WRITE);
    return st;
}

static void read(SegmentTable* st, int seg, int page) {
    st->translateAddress(seg, page / 4, page % 4, 0, READ_ONLY);
}

static void testPageFaultsAreCounted() {
    SegmentTable* st = makeTable(4, true);
    read(st, 0, 0);
    read(st, 0, 0);
    CHECK(st->pageFaults() == 1);
    CHECK(st->segmentationFaults() == 0);
    delete st;
}

static void testEvictionInvalidatesVictim() {
    SegmentTable* st = makeTable(2, true);
    read(st, 0, 0);
    read(st, 0, 1);
    read(st, 0, 2);          // Evicts page 0
    CHECK(st->evictions() == 1);
    read(st, 0, 0);          // Must fault again, not reuse a stale mapping
    CHECK(st->pageFaults() == 4);
    delete st;
}

static void testLruAndFifoChooseDifferentVictims() {
    for (int lru = 0; lru <= 1; ++lru) {
        SegmentTable* st = makeTable(3, lru, 0);
        read(st, 0, 0);
        read(st, 0, 1);
        read(st, 0, 2);
        read(st, 0, 0);      // Page 0 is the oldest load but was just used
        read(st, 0, 3);      // LRU evicts page 1, FIFO evicts page 0
        int before = st->pageFaults();
        read(st, 0, 0);
        CHECK(st->pageFaults() - before == (lru ? 0 : 1));
        delete st;
    }
}

static void testTlbHitUpdatesLruTimestamp() {
    SegmentTable* st = makeTable(2, true, 4);
    read(st, 0, 0);
    read(st, 0, 1);
    read(st, 0, 0);          // TLB hit must still count as a use of page 0
    read(st, 0, 2);          // So this evicts page 1
    int before = st->pageFaults();
    read(st, 0, 0);
    CHECK(st->pageFaults() == before);
    delete st;
}

static void testTlbEnforcesPageProtection() {
    SegmentTable st(4, 4, true, 1000, 1, 4, "");
    st.addSegment(0, 0, 8, READ_WRITE);
    PageMapping mapping = {1, -1, READ_ONLY};
    st.mapPage(0, mapping);
    read(&st, 0, 1);         // Cached in the TLB
    CHECK(throwsType<ProtectionViolation>([&] { st.translateAddress(0, 0, 1, 0, READ_WRITE); }));
    CHECK(st.protectionViolations() == 1);
}

static void testFaultDoesNotChangeProtection() {
    SegmentTable st(4, 4, true, 1000, 1, 4, "");
    st.addSegment(0, 0, 8, READ_WRITE);
    read(&st, 0, 0);         // A read fault must not downgrade the page to read-only
    st.translateAddress(0, 0, 0, 0, READ_WRITE);
    PageMapping mapping = {1, -1, READ_ONLY};
    st.mapPage(0, mapping);  // A write fault must not upgrade a read-only page
    CHECK(throwsType<ProtectionViolation>([&] { st.translateAddress(0, 0, 1, 0, READ_WRITE); }));
    CHECK(st.pageFaults() == 1);
}

static void testInvalidAddresses() {
    SegmentTable* st = makeTable(4, true);
    CHECK(throwsType<SegmentationFault>([&] { st->translateAddress(-1, 0, 0, 0, READ_ONLY); }));
    CHECK(throwsType<SegmentationFault>([&] { st->translateAddress(99, 0, 0, 0, READ_ONLY); }));
    CHECK(throwsType<InvalidAddressFault>([&] { st->translateAddress(0, -1, 0, 0, READ_ONLY); }));
    CHECK(throwsType<InvalidAddressFault>([&] { st->translateAddress(0, 4, 0, 0, READ_ONLY); }));
    CHECK(throwsType<InvalidAddressFault>([&] { st->translateAddress(0, 0, 4, 0, READ_ONLY); }));
    CHECK(throwsType<InvalidAddressFault>([&] { st->translateAddress(0, 0, -1, 0, READ_ONLY); }));
    CHECK(throwsType<OffsetFault>([&] { st->translateAddress(0, 0, 0, 1000, READ_ONLY); }));
    CHECK(throwsType<OffsetFault>([&] { st->translateAddress(0, 0, 0, -1, READ_ONLY); }));
    CHECK(st->failedTranslations() == 8);
    CHECK(st->successfulTranslations() == 0);
    delete st;
}

static void testFailureMessageMatchesLabExample() {
    SegmentTable st(4, 4, true, 1000, 1, 4, "");
    st.addSegment(0, 0, 16, READ_WRITE);
    try {
        st.translateAddress(0, 5, 0, 0, READ_ONLY);
        CHECK(false);
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()) == "Invalid dirNum 5, max 3");
    }
}

static void testPartialLastTable() {
    SegmentTable st(4, 4, true, 1000, 1, 4, "");
    st.addSegment(0, 0, 5, READ_WRITE);  // Directory 1 holds a single page
    st.translateAddress(0, 1, 0, 0, READ_ONLY);
    CHECK(throwsType<InvalidAddressFault>([&] { st.translateAddress(0, 1, 1, 0, READ_ONLY); }));
    CHECK(throwsType<InvalidAddressFault>([&] { st.translateAddress(0, 2, 0, 0, READ_ONLY); }));
}

static void testPageSizeIsUsed() {
    SegmentTable st(4, 4, true, 4000, 1, 4, "");
    st.addSegment(0, 100, 8, READ_WRITE);
    CHECK(st.translateAddress(0, 0, 0, 3999, READ_ONLY) == 100 + 3999);
    read(&st, 0, 1);
    CHECK(st.translateAddress(0, 0, 1, 7, READ_ONLY) == 100 + 4000 + 7);
}

static void testNonConsecutiveSegmentIds() {
    SegmentTable* st = makeTable(4, true);
    st->addSegment(7, 90000, 4, READ_ONLY);
    read(st, 7, 0);
    st->removeSegment(7);
    CHECK(!st->segmentExists(7));
    CHECK(st->segmentExists(0));
    delete st;
}

static void testRemoveFreesFramesAndTlb() {
    SegmentTable* st = makeTable(4, true);
    st->addSegment(3, 50000, 8, READ_WRITE);
    read(st, 3, 0);
    read(st, 3, 5);
    read(st, 0, 0);
    CHECK(st->usedFrames() == 3);
    CHECK(st->tlbEntries() == 3);
    st->removeSegment(3);
    CHECK(st->usedFrames() == 1);
    CHECK(st->tlbEntries() == 1);
    CHECK(throwsType<SegmentationFault>([&] { read(st, 3, 0); }));
    st->addSegment(3, 50000, 8, READ_WRITE);  // Same ID must start clean
    int before = st->pageFaults();
    read(st, 3, 0);
    CHECK(st->pageFaults() == before + 1);
    CHECK(throwsType<std::runtime_error>([&] { st->removeSegment(42); }));
    delete st;
}

static void testRemoveKeepsFifoUsable() {
    SegmentTable* st = makeTable(2, false);
    st->addSegment(1, 50000, 4, READ_WRITE);
    read(st, 1, 0);
    read(st, 0, 0);
    st->removeSegment(1);
    read(st, 0, 1);
    read(st, 0, 2);          // Evicts page 0, which must be a live frame
    CHECK(st->evictions() == 1);
    CHECK(st->usedFrames() == 2);
    delete st;
}

static void testTlbBehavior() {
    TLB tlb(2);
    tlb.put(0, 0, 0, 5);
    tlb.put(0, 0, 0, 6);     // Updating a key must not evict or duplicate
    tlb.put(0, 0, 1, 7);
    CHECK(tlb.size() == 2);
    CHECK(tlb.get(0, 0, 0) == 6);
    tlb.put(0, 0, 2, 8);     // Evicts 0:0:1, the least recently used
    CHECK(tlb.get(0, 0, 1) == -1);
    CHECK(tlb.get(0, 0, 0) == 6);
    CHECK(tlb.hitCount() == 2);
    CHECK(tlb.missCount() == 1);

    TLB off(0);
    off.put(0, 0, 0, 5);
    CHECK(off.size() == 0);
    CHECK(off.get(0, 0, 0) == -1);
}

static void testTwoLevelAllocatesLazily() {
    SegmentTable st(4, 4, true, 1000, 1, 4, "");
    st.addSegment(0, 0, 16, READ_WRITE);
    CHECK(st.twoLevelEntries() == 4);        // Directory only
    read(&st, 0, 0);
    CHECK(st.twoLevelEntries() == 8);        // Directory plus one table
    CHECK(st.singleLevelEntries() == 16);
}

static void testConfigFileMappings() {
    SegmentTable st(4, 12, true, 1000, 1, 4, "");
    applySegmentSpecs(st, loadSegmentFile("src/segments.txt"));
    CHECK(st.usedFrames() == 4);
    CHECK(st.translateAddress(0, 1, 1, 5, READ_ONLY) == 2005);  // Page 5 is in frame 2
    CHECK(st.pageFaults() == 0);
    CHECK(throwsType<ProtectionViolation>([&] { st.translateAddress(0, 0, 2, 0, READ_WRITE); }));
    PageMapping duplicate = {5, 6, READ_WRITE};
    CHECK(throwsType<std::runtime_error>([&] { st.mapPage(0, duplicate); }));
}

static void testConfigureMemoryResets() {
    SegmentTable* st = makeTable(4, true);
    read(st, 0, 0);
    st->configureMemory(8, 2000, 2, false);
    CHECK(!st->segmentExists(0));
    CHECK(st->translations() == 0);
    CHECK(st->totalFrames() == 8);
    CHECK(st->pageSize() == 2000);
    CHECK(throwsType<std::runtime_error>([&] { st->configureMemory(0, 1000, 2, true); }));
    delete st;
}

static void testValidRatioIsHonored() {
    SegmentTable all(16, 40, true, 1000, 1, 4, "");
    all.addSegment(0, 0, 16, READ_WRITE);
    all.addSegment(1, 20000, 8, READ_ONLY);
    generateRandomAddresses(all, 1000, 1.0, "", 0.8, 7);
    CHECK(all.failedTranslations() == 0);

    SegmentTable none(16, 40, true, 1000, 1, 4, "");
    none.addSegment(0, 0, 16, READ_WRITE);
    none.addSegment(1, 20000, 8, READ_ONLY);
    generateRandomAddresses(none, 1000, 0.0, "", 0.8, 7);
    CHECK(none.failedTranslations() == 1000);
}

static void testWorkloadRepeatsAcrossPolicies() {
    int failed[2];
    for (int lru = 0; lru <= 1; ++lru) {
        SegmentTable st(4, 6, lru, 1000, 1, 4, "");
        st.addSegment(0, 0, 16, READ_WRITE);
        st.addSegment(1, 20000, 8, READ_ONLY);
        generateRandomAddresses(st, 1000, 0.7, "", 0.8, 42);
        failed[lru] = st.failedTranslations();
    }
    CHECK(failed[0] == failed[1]);
}

// After any workload, every frame in use must belong to exactly one present
// page and the frame count must match; this catches two pages sharing a frame.
static void testNoFrameIsShared() {
    for (int lru = 0; lru <= 1; ++lru) {
        SegmentTable st(4, 5, lru, 1000, 1, 4, "");
        st.addSegment(0, 0, 16, READ_WRITE);
        st.addSegment(1, 20000, 8, READ_ONLY);
        generateRandomAddresses(st, 2000, 0.8, "", 0.5, 3);
        std::ostringstream map;
        st.printMemoryMap(map);
        std::istringstream lines(map.str());
        std::string line;
        std::map<std::string, int> frames;
        while (std::getline(lines, line)) {
            if (line.compare(0, 11, "  Directory") != 0) continue;
            size_t position = 0;
            while ((position = line.find(" F", position)) != std::string::npos) {
                size_t end = line.find(' ', position + 1);
                frames[line.substr(position + 1, end - position - 1)]++;
                position += 2;
            }
        }
        int present = 0;
        for (const auto& entry : frames) {
            CHECK(entry.second == 1);
            present++;
        }
        CHECK(present == st.usedFrames());
    }
}

int main() {
    testPageFaultsAreCounted();
    testEvictionInvalidatesVictim();
    testLruAndFifoChooseDifferentVictims();
    testTlbHitUpdatesLruTimestamp();
    testTlbEnforcesPageProtection();
    testFaultDoesNotChangeProtection();
    testInvalidAddresses();
    testFailureMessageMatchesLabExample();
    testPartialLastTable();
    testPageSizeIsUsed();
    testNonConsecutiveSegmentIds();
    testRemoveFreesFramesAndTlb();
    testRemoveKeepsFifoUsable();
    testTlbBehavior();
    testTwoLevelAllocatesLazily();
    testConfigFileMappings();
    testConfigureMemoryResets();
    testValidRatioIsHonored();
    testWorkloadRepeatsAcrossPolicies();
    testNoFrameIsShared();
    std::cout << checks - failures << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
