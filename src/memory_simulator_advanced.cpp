// CS375 - Lab 9 - Advanced Segmented, Paged Memory Model Simulator
// Author: Santiago Soto
// Contributions: Completed and corrected the provided starter code. Implemented
//   LRU (last_access timestamps) and FIFO replacement, two-level paging, the TLB,
//   dynamic segments, batch and stress testing, adaptive suggestions, protection
//   enforcement, memory visualization and latency profiling (Challenges 1-10).
//
// Compile: g++ -o memory_simulator_advanced memory_simulator_advanced.cpp -std=c++11
// Run:     ./memory_simulator_advanced            (prompts for the configuration)
//          ./memory_simulator_advanced --help     (lists every option)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

enum Protection { READ_ONLY, READ_WRITE };

const int DEFAULT_PAGES_PER_TABLE = 4;     // Second-level page table size
const int MAX_SEGMENT_PAGES = 65536;       // Keeps a mistyped limit from exhausting host memory
const int PAGING_LEVELS = 2;
const int WALK_LATENCY_PER_LEVEL = 10;     // Added once per level on a TLB miss
const int PAGE_FAULT_PENALTY = 100;        // Added when a page must be loaded
const double FAULT_SUGGESTION_RATE = 0.20; // Challenge 7: suggest resizing above 20%
const int MIN_ACCESSES_FOR_SUGGESTION = 10;
const double HOT_REGION_FRACTION = 0.20;   // Stress test: hot region is 20% of a segment
const int HOT_PHASE_LENGTH = 100;          // Stress test: hot region moves this often

const char* protectionName(Protection p) { return p == READ_ONLY ? "RO" : "RW"; }
const char* accessName(Protection p) { return p == READ_ONLY ? "Read" : "Write"; }

// ---------------------------------------------------------------------------
// Exceptions. All derive from std::runtime_error so callers can catch them
// together; PageFaultException is recoverable and handled in translateAddress.
// ---------------------------------------------------------------------------
class MemoryException : public std::runtime_error {
public:
    explicit MemoryException(const std::string& msg) : std::runtime_error(msg) {}
};
class SegmentationFault : public MemoryException {
public:
    explicit SegmentationFault(const std::string& msg) : MemoryException(msg) {}
};
class PageFaultException : public MemoryException {
public:
    explicit PageFaultException(const std::string& msg) : MemoryException(msg) {}
};
class InvalidAddressFault : public MemoryException {
public:
    explicit InvalidAddressFault(const std::string& msg) : MemoryException(msg) {}
};
class ProtectionViolation : public MemoryException {
public:
    explicit ProtectionViolation(const std::string& msg) : MemoryException(msg) {}
};
class OffsetFault : public MemoryException {
public:
    explicit OffsetFault(const std::string& msg) : MemoryException(msg) {}
};

struct Page {
    int frame_number = -1;  // -1 if not in memory
    bool present = false;
    Protection protection = READ_WRITE;
    int last_access = 0;    // For LRU
};

struct Segment {
    int id = 0;
    int base_address = 0;
    int limit = 0;          // Number of pages
    Protection protection = READ_WRITE;
    int fault_count = 0;    // Demand page faults
    int accesses = 0;       // Translations aimed at this segment
    int violations = 0;     // Protection violations
    int bounds_errors = 0;  // Invalid directory, page or offset
};

// Records which page occupies a frame, so eviction can invalidate that page.
struct FrameOwner {
    int segNum = -1;
    int dirNum = -1;
    int pageNum = -1;
    Page* page = nullptr;
};

class PhysicalMemory {
public:
    int num_frames;
    std::vector<bool> free_frames;
    std::queue<int> fifo_queue;  // For FIFO: frames in load order
    std::list<int> lru_list;     // For LRU: resident frames, victim chosen by last_access
    std::map<int, FrameOwner> frame_to_page;
    bool use_lru;
    int evictions = 0;

    PhysicalMemory(int frames, bool lru) : num_frames(frames), use_lru(lru) {
        free_frames.resize(frames, true);
    }

    bool hasFreeFrame() const {
        return std::find(free_frames.begin(), free_frames.end(), true) != free_frames.end();
    }

    // Places a page in a specific frame (used for page mappings in config files).
    void claimFrame(int frame, const FrameOwner& owner) {
        if (frame < 0 || frame >= num_frames) {
            throw std::runtime_error("Frame " + std::to_string(frame) + " is outside physical memory (0-"
                                     + std::to_string(num_frames - 1) + ")");
        }
        if (!free_frames[frame]) {
            throw std::runtime_error("Frame " + std::to_string(frame) + " is already allocated");
        }
        free_frames[frame] = false;
        if (use_lru) lru_list.push_back(frame);
        else fifo_queue.push(frame);
        frame_to_page[frame] = owner;
    }

    // Returns a free frame or, when memory is full, the frame of the victim page.
    // The victim's owner is copied to *evicted so the caller can invalidate it.
    int allocateFrame(const FrameOwner& owner, FrameOwner* evicted) {
        for (int i = 0; i < num_frames; ++i) {
            if (free_frames[i]) {
                claimFrame(i, owner);
                return i;
            }
        }
        int frame;
        if (use_lru) {
            auto victim = std::min_element(lru_list.begin(), lru_list.end(), [this](int a, int b) {
                return frame_to_page.at(a).page->last_access < frame_to_page.at(b).page->last_access;
            });
            frame = *victim;
            lru_list.erase(victim);
            lru_list.push_back(frame);
        } else {
            frame = fifo_queue.front();
            fifo_queue.pop();
            fifo_queue.push(frame);
        }
        *evicted = frame_to_page.at(frame);
        evictions++;
        frame_to_page[frame] = owner;
        return frame;
    }

    void freeFrame(int frame) {
        if (frame >= 0 && frame < num_frames) {
            free_frames[frame] = true;
            frame_to_page.erase(frame);
            if (use_lru) lru_list.remove(frame);
            else {
                std::queue<int> temp;
                while (!fifo_queue.empty()) {
                    if (fifo_queue.front() != frame) temp.push(fifo_queue.front());
                    fifo_queue.pop();
                }
                fifo_queue = temp;
            }
        }
    }

    int usedFrames() const {
        return std::count(free_frames.begin(), free_frames.end(), false);
    }

    double utilization() const {
        return (double)usedFrames() / num_frames * 100;
    }
};

class PageTable {
public:
    std::vector<Page> pages;
    int page_size;
    int directory_index;

    PageTable(int numPages, int pageSize, int dirIdx, Protection prot)
        : page_size(pageSize), directory_index(dirIdx) {
        pages.resize(numPages);
        for (auto& p : pages) p.protection = prot;
    }

    // Returns the frame of a resident page. A page that is not resident raises
    // PageFaultException, the simulator's version of a hardware trap.
    int getFrameNumber(int pageNum, int time, Protection accessType) {
        if (pageNum < 0 || pageNum >= (int)pages.size()) {
            throw InvalidAddressFault("Invalid pageNum " + std::to_string(pageNum) + ", max "
                                      + std::to_string((int)pages.size() - 1));
        }
        Page& page = pages[pageNum];
        if (accessType == READ_WRITE && page.protection == READ_ONLY) {
            throw ProtectionViolation("Protection Violation: Cannot write to read-only page");
        }
        if (!page.present) {
            throw PageFaultException("Page Fault: Page " + std::to_string(pageNum) + " not present");
        }
        page.last_access = time;
        return page.frame_number;
    }

    void setFrame(int pageNum, int frame, int time) {
        pages[pageNum].frame_number = frame;
        pages[pageNum].present = true;
        pages[pageNum].last_access = time;
    }

    // Loads a page, evicting a victim by LRU or FIFO when memory is full.
    int replacePage(int segNum, int pageNum, int time, PhysicalMemory& physMem, FrameOwner* evicted) {
        FrameOwner owner;
        owner.segNum = segNum;
        owner.dirNum = directory_index;
        owner.pageNum = pageNum;
        owner.page = &pages[pageNum];
        int frame = physMem.allocateFrame(owner, evicted);
        if (evicted->page) {
            evicted->page->present = false;
            evicted->page->frame_number = -1;
        }
        setFrame(pageNum, frame, time);
        return frame;
    }
};

// First paging level. Entries stay null until a directory is first used, so a
// sparse segment needs far fewer page table entries than a single-level table.
class DirectoryTable {
public:
    std::vector<PageTable*> pageTables;
    int max_pages_per_table;
    int total_pages;
    int page_size;
    Protection protection;

    DirectoryTable(int totalPages, int pagesPerTable, int pageSize, Protection prot)
        : max_pages_per_table(pagesPerTable), total_pages(totalPages), page_size(pageSize), protection(prot) {
        pageTables.resize((totalPages + pagesPerTable - 1) / pagesPerTable, nullptr);
    }

    ~DirectoryTable() { freeTables(); }

    DirectoryTable(const DirectoryTable&) = delete;
    DirectoryTable& operator=(const DirectoryTable&) = delete;

    PageTable* allocatePageTable(int dirNum) {
        if (!pageTables[dirNum]) {
            int remaining = total_pages - dirNum * max_pages_per_table;
            pageTables[dirNum] = new PageTable(std::min(remaining, max_pages_per_table), page_size,
                                               dirNum, protection);
        }
        return pageTables[dirNum];
    }

    PageTable* getPageTable(int dirNum) {
        if (dirNum < 0 || dirNum >= (int)pageTables.size()) {
            throw InvalidAddressFault("Invalid dirNum " + std::to_string(dirNum) + ", max "
                                      + std::to_string((int)pageTables.size() - 1));
        }
        return allocatePageTable(dirNum);
    }

    void freeTables() {
        for (auto* pt : pageTables) delete pt;
        pageTables.clear();
    }
};

class TLB {
private:
    std::unordered_map<std::string, int> cache;  // Key: segNum:dirNum:pageNum
    std::list<std::string> lruOrder;             // Front is least recently used
    int maxSize;
    int hits = 0, total = 0;

    static std::string makeKey(int segNum, int dirNum, int pageNum) {
        return std::to_string(segNum) + ":" + std::to_string(dirNum) + ":" + std::to_string(pageNum);
    }

public:
    explicit TLB(int size) : maxSize(size) {}

    int get(int segNum, int dirNum, int pageNum) {
        std::string key = makeKey(segNum, dirNum, pageNum);
        total++;
        auto it = cache.find(key);
        if (it != cache.end()) {
            hits++;
            lruOrder.remove(key);
            lruOrder.push_back(key);
            return it->second;
        }
        return -1;
    }

    void put(int segNum, int dirNum, int pageNum, int frame) {
        if (maxSize <= 0) return;  // Size 0 turns the TLB off
        std::string key = makeKey(segNum, dirNum, pageNum);
        if (cache.find(key) != cache.end()) {
            lruOrder.remove(key);
        } else if ((int)cache.size() >= maxSize) {
            cache.erase(lruOrder.front());
            lruOrder.pop_front();
        }
        cache[key] = frame;
        lruOrder.push_back(key);
    }

    void invalidate(int segNum, int dirNum, int pageNum) {
        std::string key = makeKey(segNum, dirNum, pageNum);
        if (cache.erase(key)) lruOrder.remove(key);
    }

    void invalidateSegment(int segNum) {
        std::string prefix = std::to_string(segNum) + ":";
        for (auto it = lruOrder.begin(); it != lruOrder.end();) {
            if (it->compare(0, prefix.size(), prefix) == 0) {
                cache.erase(*it);
                it = lruOrder.erase(it);
            } else {
                ++it;
            }
        }
    }

    int hitCount() const { return hits; }
    int missCount() const { return total - hits; }
    int size() const { return (int)cache.size(); }
    int capacity() const { return maxSize; }

    double hitRate() const {
        return total > 0 ? (double)hits / total * 100 : 0;
    }

    void displayCache(std::ostream& out) const {
        out << "TLB Contents (LRU Order, oldest first, " << cache.size() << "/" << maxSize << " entries):\n";
        for (const auto& key : lruOrder) {
            out << "  " << key << " -> Frame " << cache.at(key) << "\n";
        }
        out << "TLB Hit Rate: " << hitRate() << "% (" << hits << " hits, " << missCount() << " misses)\n";
    }
};

// One line of the translation timeline.
struct TranslationEvent {
    int time;
    int segNum, dirNum, pageNum, offset;
    Protection access;
    int latency;
    std::string outcome;  // Physical address or the fault message
    std::string tag;      // TLB HIT, TLB MISS, PAGE FAULT or ERROR
};

struct PageMapping {
    int page;   // Page index within the segment, counted across directories
    int frame;  // -1 leaves the page out of memory
    Protection protection;
};

struct SegmentSpec {
    int id;
    int base;
    int limit;
    Protection protection;
    std::vector<PageMapping> mappings;
};

class SegmentTable {
private:
    std::map<int, Segment> segments;  // Keyed by ID, since IDs need not be consecutive
    std::map<int, DirectoryTable*> directoryTables;
    TLB tlb;
    PhysicalMemory physMem;
    int page_size;
    int pages_per_table;
    int time = 0;
    long long total_latency = 0, hit_latency = 0, miss_latency = 0, fault_latency = 0;
    int translation_count = 0, hit_count = 0, miss_count = 0, page_fault_count = 0;
    int segfaults = 0, invalid_addresses = 0, protection_violations = 0;
    std::vector<TranslationEvent> timeline;
    std::mt19937 latencyGen;
    std::ofstream log;
    bool color;

    void record(int tick, int segNum, int dirNum, int pageNum, int offset, Protection accessType,
                int latency, const std::string& outcome, const std::string& tag) {
        TranslationEvent event = {tick, segNum, dirNum, pageNum, offset, accessType, latency, outcome, tag};
        timeline.push_back(event);
        if (log.is_open()) writeEvent(log, event);
    }

    static void writeEvent(std::ostream& out, const TranslationEvent& e) {
        std::ostringstream address;
        address << "(" << e.segNum << "," << e.dirNum << "," << e.pageNum << "," << e.offset << ","
                << (e.access == READ_ONLY ? "R" : "W") << ")";
        int width = std::min(24, std::max(1, e.latency / 5));  // One # per 5 latency units
        bool failed = e.tag == "ERROR";
        out << "Time " << std::setw(4) << e.time << ": Address " << std::left << std::setw(18) << address.str()
            << "-> " << std::setw(8) << (failed ? "--" : e.outcome)
            << " [" << std::setw(10) << e.tag << "] " << std::right << std::setw(3) << e.latency
            << " |" << std::string(width, '#') << "|";
        if (failed) out << " " << e.outcome;
        out << "\n";
    }

public:
    SegmentTable(int tlbSize, int numFrames, bool use_lru, int pageSize, unsigned seed,
                 int pagesPerTable = DEFAULT_PAGES_PER_TABLE, const std::string& logFile = "memory_log.txt")
        : tlb(tlbSize), physMem(numFrames, use_lru), page_size(pageSize), pages_per_table(pagesPerTable),
          latencyGen(seed), color(isatty(STDOUT_FILENO)) {
        if (!logFile.empty()) log.open(logFile);
        validateConfiguration(numFrames, pageSize, tlbSize);
    }

    ~SegmentTable() {
        for (auto& pair : directoryTables) delete pair.second;
    }

    SegmentTable(const SegmentTable&) = delete;
    SegmentTable& operator=(const SegmentTable&) = delete;

    void validateConfiguration(int numFrames, int pageSize, int tlbSize) const {
        if (numFrames < 1) throw std::runtime_error("Physical memory needs at least 1 frame");
        if (pageSize < 1) throw std::runtime_error("Page size must be at least 1");
        if (tlbSize < 0) throw std::runtime_error("TLB size cannot be negative");
        if (pages_per_table < 1) throw std::runtime_error("Page table size must be at least 1");
    }

    // Starts over with a new memory configuration. Every segment is removed
    // because frame numbers and page sizes would no longer be meaningful.
    void configureMemory(int numFrames, int pageSize, int tlbSize, bool use_lru) {
        validateConfiguration(numFrames, pageSize, tlbSize);
        for (auto& pair : directoryTables) delete pair.second;
        directoryTables.clear();
        segments.clear();
        timeline.clear();
        tlb = TLB(tlbSize);
        physMem = PhysicalMemory(numFrames, use_lru);
        page_size = pageSize;
        time = 0;
        total_latency = hit_latency = miss_latency = fault_latency = 0;
        translation_count = hit_count = miss_count = page_fault_count = 0;
        segfaults = invalid_addresses = protection_violations = 0;
    }

    void addSegment(int id, int base, int limit, Protection prot) {
        if (segments.find(id) != segments.end()) {
            throw std::runtime_error("Segment " + std::to_string(id) + " already exists");
        }
        if (id < 0 || base < 0 || limit < 1 || limit > MAX_SEGMENT_PAGES) {
            throw std::runtime_error("Invalid segment: id and base must be >= 0, limit must be 1-"
                                     + std::to_string(MAX_SEGMENT_PAGES));
        }
        Segment segment;
        segment.id = id;
        segment.base_address = base;
        segment.limit = limit;
        segment.protection = prot;
        segments[id] = segment;
        directoryTables[id] = new DirectoryTable(limit, pages_per_table, page_size, prot);
        if (log.is_open()) {
            log << "Segment " << id << " added: base=" << base << " limit=" << limit
                << " protection=" << protectionName(prot) << "\n";
        }
    }

    void removeSegment(int id) {
        auto entry = directoryTables.find(id);
        if (entry == directoryTables.end()) {
            throw std::runtime_error("Cannot remove invalid segment " + std::to_string(id));
        }
        for (auto* pt : entry->second->pageTables) {
            if (!pt) continue;
            for (auto& page : pt->pages) {
                if (page.present) physMem.freeFrame(page.frame_number);
            }
        }
        tlb.invalidateSegment(id);  // Otherwise a recreated segment could hit stale entries
        delete entry->second;
        directoryTables.erase(entry);
        segments.erase(id);
        if (log.is_open()) log << "Segment " << id << " removed\n";
    }

    // Applies a page's protection and optional frame from a config file entry.
    void mapPage(int segNum, const PageMapping& mapping) {
        Segment& segment = segments.at(segNum);
        if (mapping.page < 0 || mapping.page >= segment.limit) {
            throw std::runtime_error("Page mapping " + std::to_string(mapping.page) + " is outside segment "
                                     + std::to_string(segNum));
        }
        int dirNum = mapping.page / pages_per_table;
        int pageNum = mapping.page % pages_per_table;
        PageTable* table = directoryTables.at(segNum)->getPageTable(dirNum);
        Page& page = table->pages[pageNum];
        if (page.present) {
            throw std::runtime_error("Page " + std::to_string(mapping.page) + " of segment "
                                     + std::to_string(segNum) + " is mapped twice");
        }
        // A page may be stricter than its segment, never more permissive.
        page.protection = segment.protection == READ_ONLY ? READ_ONLY : mapping.protection;
        if (mapping.frame >= 0) {
            FrameOwner owner;
            owner.segNum = segNum;
            owner.dirNum = dirNum;
            owner.pageNum = pageNum;
            owner.page = &page;
            physMem.claimFrame(mapping.frame, owner);
            table->setFrame(pageNum, mapping.frame, time++);
        }
    }

    // Used for randomly initialized segments: some pages start in memory and
    // some pages of a read-write segment are read-only.
    void randomizePages(int segNum, std::mt19937& gen) {
        Segment& segment = segments.at(segNum);
        for (int i = 0; i < segment.limit; ++i) {
            PageMapping mapping;
            mapping.page = i;
            mapping.frame = -1;
            mapping.protection = (gen() % 4 == 0) ? READ_ONLY : READ_WRITE;
            mapPage(segNum, mapping);
            if (gen() % 2 && physMem.hasFreeFrame()) {
                int free = 0;
                while (!physMem.free_frames[free]) ++free;
                mapping.frame = free;
                mapPage(segNum, mapping);
            }
        }
    }

    int translateAddress(int segNum, int dirNum, int pageNum, int offset, Protection accessType) {
        int tick = time++;
        int latency = 1 + latencyGen() % 10;  // Random 1-10 units for every translation
        int baseLatency = latency;
        translation_count++;
        Segment* segment = nullptr;
        try {
            auto found = segments.find(segNum);
            if (found == segments.end()) {
                throw SegmentationFault("Segmentation Fault: Invalid segment " + std::to_string(segNum));
            }
            segment = &found->second;
            segment->accesses++;
            if (accessType == READ_WRITE && segment->protection == READ_ONLY) {
                throw ProtectionViolation("Protection Violation: Cannot write to read-only segment "
                                          + std::to_string(segNum));
            }
            PageTable* pageTable = directoryTables.at(segNum)->getPageTable(dirNum);
            if (pageNum < 0 || pageNum >= (int)pageTable->pages.size()) {
                throw InvalidAddressFault("Invalid pageNum " + std::to_string(pageNum) + ", max "
                                          + std::to_string((int)pageTable->pages.size() - 1));
            }
            if (offset < 0 || offset >= page_size) {
                throw OffsetFault("Offset Fault: Offset " + std::to_string(offset) + " exceeds page size "
                                  + std::to_string(page_size));
            }
            Page& page = pageTable->pages[pageNum];
            // Permissions are rechecked on every access, including TLB hits.
            if (accessType == READ_WRITE && page.protection == READ_ONLY) {
                throw ProtectionViolation("Protection Violation: Cannot write to read-only page "
                                          + std::to_string(segNum) + ":" + std::to_string(dirNum) + ":"
                                          + std::to_string(pageNum));
            }

            int frame = tlb.get(segNum, dirNum, pageNum);
            std::string tag = "TLB HIT";
            if (frame != -1) {
                page.last_access = tick;
                hit_count++;
                hit_latency += latency;
            } else {
                latency += PAGING_LEVELS * WALK_LATENCY_PER_LEVEL;
                tag = "TLB MISS";
                try {
                    frame = pageTable->getFrameNumber(pageNum, tick, accessType);
                } catch (const PageFaultException&) {
                    FrameOwner evicted;
                    frame = pageTable->replacePage(segNum, pageNum, tick, physMem, &evicted);
                    if (evicted.page) tlb.invalidate(evicted.segNum, evicted.dirNum, evicted.pageNum);
                    segment->fault_count++;
                    page_fault_count++;
                    latency += PAGE_FAULT_PENALTY;
                    tag = "PAGE FAULT";
                }
                tlb.put(segNum, dirNum, pageNum, frame);
                if (tag == "PAGE FAULT") fault_latency += latency;
                else {
                    miss_count++;
                    miss_latency += latency;
                }
            }
            total_latency += latency;
            int address = segment->base_address + frame * page_size + offset;
            record(tick, segNum, dirNum, pageNum, offset, accessType, latency, std::to_string(address), tag);
            return address;
        } catch (const MemoryException& e) {
            total_latency += baseLatency;
            if (dynamic_cast<const SegmentationFault*>(&e)) {
                segfaults++;
            } else if (dynamic_cast<const ProtectionViolation*>(&e)) {
                segment->violations++;
                protection_violations++;
            } else {
                segment->bounds_errors++;
                invalid_addresses++;
            }
            record(tick, segNum, dirNum, pageNum, offset, accessType, baseLatency, e.what(), "ERROR");
            throw;
        }
    }

    // Accessors used by the stress test to build addresses for the live segments.
    std::vector<Segment> liveSegments() const {
        std::vector<Segment> list;
        for (const auto& pair : segments) list.push_back(pair.second);
        return list;
    }
    int pageSize() const { return page_size; }
    int pagesPerTable() const { return pages_per_table; }
    Protection pageProtection(int segNum, int dirNum, int pageNum) {
        return directoryTables.at(segNum)->getPageTable(dirNum)->pages[pageNum].protection;
    }
    int directoryCount(int segNum) const {
        return (int)directoryTables.at(segNum)->pageTables.size();
    }

    // Metric accessors, also used by the comparison tables and the tests.
    int pageFaults() const { return page_fault_count; }
    int translations() const { return translation_count; }
    int failedTranslations() const { return segfaults + invalid_addresses + protection_violations; }
    int successfulTranslations() const { return translation_count - failedTranslations(); }
    int evictions() const { return physMem.evictions; }
    double tlbHitRate() const { return tlb.hitRate(); }
    double memoryUtilization() const { return physMem.utilization(); }
    double averageLatency() const {
        return translation_count > 0 ? (double)total_latency / translation_count : 0;
    }
    double pageFaultRate() const {
        int ok = successfulTranslations();
        return ok > 0 ? (double)page_fault_count / ok * 100 : 0;
    }
    int usedFrames() const { return physMem.usedFrames(); }
    int totalFrames() const { return physMem.num_frames; }
    int tlbEntries() const { return tlb.size(); }
    int protectionViolations() const { return protection_violations; }
    int segmentationFaults() const { return segfaults; }
    int invalidAddresses() const { return invalid_addresses; }
    bool segmentExists(int id) const { return segments.find(id) != segments.end(); }

    // Two-level cost is the directories plus the tables allocated so far; a
    // single-level table would need one entry for every page of every segment.
    long long twoLevelEntries() const {
        long long entries = 0;
        for (const auto& pair : directoryTables) {
            entries += pair.second->pageTables.size();
            for (auto* pt : pair.second->pageTables) {
                if (pt) entries += pt->pages.size();
            }
        }
        return entries;
    }
    long long singleLevelEntries() const {
        long long entries = 0;
        for (const auto& pair : segments) entries += pair.second.limit;
        return entries;
    }

    void displayStats(std::ostream& out = std::cout) {
        out << std::fixed << std::setprecision(2);
        out << "========== Simulation Metrics ==========\n"
            << "Configuration: " << (physMem.use_lru ? "LRU" : "FIFO") << " replacement, "
            << physMem.num_frames << " frames, page size " << page_size << ", TLB size " << tlb.capacity() << "\n"
            << "Translations: " << translation_count << " attempted, " << successfulTranslations()
            << " succeeded, " << failedTranslations() << " failed\n"
            << "Failures: " << segfaults << " segmentation faults, " << invalid_addresses
            << " invalid addresses, " << protection_violations << " protection violations\n"
            << "Page Faults: " << page_fault_count << " (" << pageFaultRate()
            << "% of successful translations), " << physMem.evictions << " evictions\n"
            << "TLB Hit Rate: " << tlb.hitRate() << "% (" << tlb.hitCount() << " hits, "
            << tlb.missCount() << " misses)\n"
            << "Physical Memory Utilization: " << physMem.utilization() << "% ("
            << physMem.usedFrames() << "/" << physMem.num_frames << " frames)\n"
            << "Page Table Entries: " << twoLevelEntries() << " allocated (two-level) vs "
            << singleLevelEntries() << " (single-level)\n";

        out << "Average Translation Latency: " << averageLatency() << " units\n";
        if (hit_count > 0) out << "  TLB hit:    " << (double)hit_latency / hit_count << " units\n";
        if (miss_count > 0) out << "  TLB miss:   " << (double)miss_latency / miss_count << " units\n";
        if (page_fault_count > 0) out << "  Page fault: " << (double)fault_latency / page_fault_count << " units\n";
        long long saved = (long long)hit_count * PAGING_LEVELS * WALK_LATENCY_PER_LEVEL;
        out << "TLB Impact: hits avoided " << saved << " units of page table walks";
        if (total_latency + saved > 0) {
            out << " (" << (double)saved / (total_latency + saved) * 100 << "% of the no-TLB cost)";
        }
        out << "\n";

        out << "Per-Segment Statistics:\n";
        for (const auto& pair : segments) {
            const Segment& s = pair.second;
            double rate = s.accesses > 0 ? (double)s.fault_count / s.accesses * 100 : 0;
            out << "  Segment " << s.id << ": " << s.fault_count << " page faults / " << s.accesses
                << " accesses (" << rate << "%), " << s.violations << " violations, "
                << s.bounds_errors << " invalid addresses\n";
            if (s.accesses >= MIN_ACCESSES_FOR_SUGGESTION && rate > FAULT_SUGGESTION_RATE * 100) {
                out << "    Suggestion: Increase limit for Segment " << s.id << " from " << s.limit << " to "
                    << (int)std::ceil(s.limit * 1.5) << " to reduce faults (rate above "
                    << FAULT_SUGGESTION_RATE * 100 << "%)\n";
            }
        }
        tlb.displayCache(out);
        out << "========================================\n";
        out.unsetf(std::ios::fixed);
        out << std::setprecision(6);
    }

    void printMemoryMap(std::ostream& out = std::cout) {
        bool useColor = color && &out == &std::cout;  // Log files stay plain text
        const char* usedOn = useColor ? "\033[1;32m" : "";
        const char* freeOn = useColor ? "\033[2m" : "";
        const char* off = useColor ? "\033[0m" : "";
        out << "Memory Map (" << (physMem.use_lru ? "LRU" : "FIFO") << ", page size " << page_size << "):\n";
        for (const auto& pair : segments) {
            const Segment& s = pair.second;
            out << "Segment " << s.id << ": Base=" << s.base_address << ", Limit=" << s.limit
                << ", Protection=" << protectionName(s.protection) << ", Faults=" << s.fault_count << "\n";
            DirectoryTable* directory = directoryTables.at(s.id);
            for (size_t j = 0; j < directory->pageTables.size(); ++j) {
                PageTable* pt = directory->pageTables[j];
                out << "  Directory " << j << ":";
                if (!pt) {
                    out << " (table not allocated)\n";
                    continue;
                }
                for (size_t k = 0; k < pt->pages.size(); ++k) {
                    const Page& p = pt->pages[k];
                    out << " [P" << k << " ";
                    if (p.present) out << "F" << p.frame_number << " t=" << p.last_access;
                    else out << "--";
                    out << " " << protectionName(p.protection) << "]";
                }
                out << "\n";
            }
        }
        out << "Physical Frames: " << physMem.usedFrames() << " allocated, "
            << physMem.num_frames - physMem.usedFrames() << " free  (Fn[seg:dir:page] allocated, Fn[free] free)\n";
        for (int i = 0; i < physMem.num_frames; ++i) {
            std::ostringstream cell;
            cell << "F" << i;
            if (physMem.free_frames[i]) {
                cell << "[free]";
                out << freeOn;
            } else {
                const FrameOwner& owner = physMem.frame_to_page.at(i);
                cell << "[" << owner.segNum << ":" << owner.dirNum << ":" << owner.pageNum << "]";
                out << usedOn;
            }
            out << std::left << std::setw(13) << cell.str() << std::right << off;
            if (i % 6 == 5 || i == physMem.num_frames - 1) out << "\n";
        }
    }

    // Text Gantt chart: each # is 5 latency units, so faults stand out.
    void printTimeline(std::ostream& out = std::cout, int lastN = 0) const {
        size_t first = 0;
        if (lastN > 0 && timeline.size() > (size_t)lastN) first = timeline.size() - lastN;
        out << "Translation Timeline (" << timeline.size() - first << " of " << timeline.size() << " events):\n";
        for (size_t i = first; i < timeline.size(); ++i) writeEvent(out, timeline[i]);
    }

    void writeFinalLog() {
        if (log.is_open()) {
            log << "\n";
            displayStats(log);
            printMemoryMap(log);
        }
    }

};

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------
std::string stripComment(std::string line) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    return line;
}

Protection parseProtection(const std::string& text) {
    if (text == "RO" || text == "ro") return READ_ONLY;
    if (text == "RW" || text == "rw") return READ_WRITE;
    throw std::runtime_error("Protection must be RO or RW, got '" + text + "'");
}

// Access is 0 or R for a read, 1 or W for a write.
Protection parseAccess(const std::string& text) {
    if (text == "0" || text == "R" || text == "r") return READ_ONLY;
    if (text == "1" || text == "W" || text == "w") return READ_WRITE;
    throw std::runtime_error("Access must be 0/R (read) or 1/W (write), got '" + text + "'");
}

// Format: segID, base, limit, protection, page mappings...
// A mapping is page:frame or page:frame:RO, with - as the frame for a page
// that starts out of memory. Commas are optional.
std::vector<SegmentSpec> loadSegmentFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open config file " + filename);
    std::vector<SegmentSpec> specs;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        line = stripComment(line);
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream in(line);
        std::string protection;
        SegmentSpec spec;
        if (!(in >> spec.id)) continue;  // Blank or comment-only line
        if (!(in >> spec.base >> spec.limit >> protection)) {
            throw std::runtime_error(filename + " line " + std::to_string(lineNumber)
                                     + ": expected segID base limit protection");
        }
        spec.protection = parseProtection(protection);
        std::string token;
        while (in >> token) {
            std::replace(token.begin(), token.end(), ':', ' ');
            std::istringstream fields(token);
            std::string frame, pageProtection;
            PageMapping mapping;
            mapping.protection = spec.protection;
            if (!(fields >> mapping.page >> frame)) {
                throw std::runtime_error(filename + " line " + std::to_string(lineNumber)
                                         + ": mappings must look like page:frame");
            }
            mapping.frame = frame == "-" ? -1 : std::stoi(frame);
            if (fields >> pageProtection) mapping.protection = parseProtection(pageProtection);
            spec.mappings.push_back(mapping);
        }
        specs.push_back(spec);
    }
    if (specs.empty()) throw std::runtime_error("No segments found in " + filename);
    return specs;
}

// Random limits and protections, with bases that do not overlap.
std::vector<SegmentSpec> randomSegmentSpecs(int count, int pageSize, std::mt19937& gen) {
    std::vector<SegmentSpec> specs;
    int base = 0;
    for (int i = 0; i < count; ++i) {
        SegmentSpec spec;
        spec.id = i;
        spec.base = base;
        spec.limit = 4 + gen() % 13;
        spec.protection = (gen() % 3 == 0) ? READ_ONLY : READ_WRITE;
        specs.push_back(spec);
        base += spec.limit * pageSize;
    }
    return specs;
}

void applySegmentSpecs(SegmentTable& st, const std::vector<SegmentSpec>& specs) {
    for (const auto& spec : specs) {
        st.addSegment(spec.id, spec.base, spec.limit, spec.protection);
        for (const auto& mapping : spec.mappings) st.mapPage(spec.id, mapping);
    }
}

// ---------------------------------------------------------------------------
// Batch processing (Challenge 5)
// ---------------------------------------------------------------------------
void processBatchFile(SegmentTable& st, const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open batch file " + filename);
    std::ofstream log("batch_results.txt");
    int faults = 0, translations = 0;
    std::string line;
    while (std::getline(file, line)) {
        line = stripComment(line);
        std::istringstream in(line);
        int segNum, dirNum, pageNum, offset;
        std::string access;
        if (!(in >> segNum)) continue;  // Blank or comment-only line
        translations++;
        if (!(in >> dirNum >> pageNum >> offset >> access)) {
            faults++;
            log << "Malformed line: " << line << "\n";
            continue;
        }
        log << "Address (" << segNum << "," << dirNum << "," << pageNum << "," << offset << "," << access << "): ";
        try {
            int addr = st.translateAddress(segNum, dirNum, pageNum, offset, parseAccess(access));
            log << "Physical=" << addr << "\n";
        } catch (const std::runtime_error& e) {
            faults++;
            log << e.what() << "\n";
        }
    }
    log << "Fault Rate: " << (translations ? (double)faults / translations * 100 : 0)
        << "% (" << faults << " of " << translations << " addresses failed)\n";
    log << "Page Faults: " << st.pageFaults() << "\n";
}

// ---------------------------------------------------------------------------
// Stress testing (Challenge 6)
// ---------------------------------------------------------------------------
struct StressAddress {
    int segNum, dirNum, pageNum, offset;
    Protection access;
};

// Produces one address. All random values are drawn up front, so a given seed
// yields the same byte-level reference string under every memory configuration.
// A valid address passes every check; an invalid one breaks exactly one rule.
StressAddress nextAddress(SegmentTable& st, const std::vector<Segment>& segments, std::mt19937& gen,
                          double validRatio, double locality, int index, int& hotSegment, double& hotStart) {
    auto unit = [&gen]() { return gen() / 4294967296.0; };
    if (index % HOT_PHASE_LENGTH == 0) {
        hotSegment = gen() % segments.size();
        hotStart = unit();
    }
    double validDraw = unit(), localityDraw = unit(), position = unit(), writeDraw = unit();
    int kind = gen() % 5, excess = gen() % 3, randomSegment = gen() % segments.size();

    bool hot = localityDraw < locality;
    const Segment& segment = segments[hot ? hotSegment : randomSegment];
    double fraction = hot ? std::fmod(hotStart + position * HOT_REGION_FRACTION, 1.0) : position;
    long long segmentBytes = (long long)segment.limit * st.pageSize();
    long long byte = (long long)(fraction * segmentBytes);
    int pageIndex = (int)(byte / st.pageSize());

    StressAddress a;
    a.segNum = segment.id;
    a.dirNum = pageIndex / st.pagesPerTable();
    a.pageNum = pageIndex % st.pagesPerTable();
    a.offset = (int)(byte % st.pageSize());
    bool canWrite = segment.protection == READ_WRITE
                    && st.pageProtection(a.segNum, a.dirNum, a.pageNum) == READ_WRITE;
    a.access = (canWrite && writeDraw < 0.3) ? READ_WRITE : READ_ONLY;
    if (validDraw < validRatio) return a;

    switch (kind) {
        case 0: a.segNum = segments.back().id + 1 + excess; break;
        case 1: a.dirNum = st.directoryCount(a.segNum) + excess; break;
        case 2: a.pageNum = st.pagesPerTable() + excess; break;
        case 3: a.offset = st.pageSize() + excess; break;
        default:
            for (const auto& s : segments) {
                if (s.protection == READ_ONLY) {
                    a.segNum = s.id;
                    a.dirNum = 0;
                    a.pageNum = 0;
                    a.access = READ_WRITE;
                    return a;
                }
            }
            a.offset = st.pageSize() + excess;  // No read-only segment to violate
    }
    return a;
}

void generateRandomAddresses(SegmentTable& st, int num, double validRatio, const std::string& logFile,
                             double locality, unsigned seed) {
    std::vector<Segment> segments = st.liveSegments();
    if (segments.empty()) throw std::runtime_error("No segments to test; add one first");
    if (num < 1) throw std::runtime_error("Number of addresses must be at least 1");
    if (validRatio < 0 || validRatio > 1 || locality < 0 || locality > 1) {
        throw std::runtime_error("Valid ratio and locality must be between 0 and 1");
    }
    std::ofstream log;
    if (!logFile.empty()) log.open(logFile);
    std::mt19937 gen(seed);
    int hotSegment = 0, failures = 0, pageFaultsBefore = st.pageFaults();
    double hotStart = 0;
    std::map<std::string, int> reasons;
    for (int i = 0; i < num; ++i) {
        StressAddress a = nextAddress(st, segments, gen, validRatio, locality, i, hotSegment, hotStart);
        if (log.is_open()) {
            log << "Address " << i << ": (" << a.segNum << "," << a.dirNum << "," << a.pageNum << ","
                << a.offset << "," << accessName(a.access) << ") ";
        }
        try {
            int addr = st.translateAddress(a.segNum, a.dirNum, a.pageNum, a.offset, a.access);
            if (log.is_open()) log << "Physical=" << addr << "\n";
        } catch (const std::runtime_error& e) {
            failures++;
            std::string reason = e.what();
            std::string category = reason.substr(0, reason.find_first_of(":,0123456789"));
            category.erase(category.find_last_not_of(' ') + 1);
            reasons[category]++;
            if (log.is_open()) log << reason << "\n";
        }
    }
    if (log.is_open()) {
        int faults = st.pageFaults() - pageFaultsBefore;
        log << "Invalid Address Rate: " << (double)failures / num * 100 << "% (" << failures << " of " << num << ")\n";
        log << "Page Fault Rate: " << (double)faults / num * 100 << "% (" << faults << " of " << num << ")\n";
        for (const auto& reason : reasons) log << "  " << reason.first << ": " << reason.second << "\n";
    }
}

// ---------------------------------------------------------------------------
// Side-by-side experiments: each row replays the same seeded workload on a
// fresh simulator, starting with memory empty.
// ---------------------------------------------------------------------------
struct RunConfig {
    std::string label;
    int frames, pageSize, tlbSize;
    bool lru;
    double validRatio, locality;
};

void printComparison(const std::string& title, const std::vector<RunConfig>& runs,
                     const std::vector<SegmentSpec>& specs, int referencePageSize, int num, unsigned seed) {
    std::cout << title << "\n" << num << " addresses, seed " << seed << ", " << specs.size() << " segments\n\n";
    std::cout << std::left << std::setw(20) << "Configuration" << std::right
              << std::setw(7) << "Frames" << std::setw(9) << "PgSize" << std::setw(7) << "Faults"
              << std::setw(9) << "Fault%" << std::setw(8) << "Evict" << std::setw(8) << "TLB%"
              << std::setw(9) << "AvgLat" << std::setw(8) << "Util%" << std::setw(8) << "Failed"
              << std::setw(8) << "PTEs" << "\n";
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& run : runs) {
        SegmentTable st(run.tlbSize, run.frames, run.lru, run.pageSize, seed, DEFAULT_PAGES_PER_TABLE, "");
        for (const auto& spec : specs) {
            // Segments keep their size in bytes when the page size changes.
            long long bytes = (long long)spec.limit * referencePageSize;
            st.addSegment(spec.id, spec.base, (int)((bytes + run.pageSize - 1) / run.pageSize), spec.protection);
        }
        generateRandomAddresses(st, num, run.validRatio, "", run.locality, seed);
        std::cout << std::left << std::setw(20) << run.label << std::right
                  << std::setw(7) << run.frames << std::setw(9) << run.pageSize
                  << std::setw(7) << st.pageFaults() << std::setw(9) << st.pageFaultRate()
                  << std::setw(8) << st.evictions() << std::setw(8) << st.tlbHitRate()
                  << std::setw(9) << st.averageLatency() << std::setw(8) << st.memoryUtilization()
                  << std::setw(8) << st.failedTranslations() << std::setw(8) << st.twoLevelEntries() << "\n";
    }
    std::cout.unsetf(std::ios::fixed);
    std::cout << std::setprecision(6);
}

void runComparison(const std::string& kind, const std::vector<SegmentSpec>& specs, int frames, int pageSize,
                   int tlbSize, bool lru, double validRatio, double locality, int num, unsigned seed) {
    std::vector<RunConfig> runs;
    std::ostringstream title;
    if (kind == "policy") {
        title << "LRU vs FIFO (valid ratio " << validRatio << ", locality " << locality << ")";
        runs.push_back({"LRU", frames, pageSize, tlbSize, true, validRatio, locality});
        runs.push_back({"FIFO", frames, pageSize, tlbSize, false, validRatio, locality});
    } else if (kind == "tlb") {
        title << "TLB size comparison (valid ratio " << validRatio << ", locality " << locality << ")";
        for (int size : {0, 4, 16}) {
            runs.push_back({size == 0 ? "TLB off" : "TLB " + std::to_string(size), frames, pageSize, size,
                            lru, validRatio, locality});
        }
    } else if (kind == "pagesize") {
        // Same segment bytes and physical memory bytes, so only the page size differs.
        title << "Page size comparison with equal memory (valid ratio " << validRatio
              << ", locality " << locality << ")";
        long long memory = (long long)frames * pageSize;
        for (int size : {1000, 4000}) {
            runs.push_back({"Page size " + std::to_string(size), (int)(memory / size), size, tlbSize,
                            lru, validRatio, locality});
        }
    } else if (kind == "ratio") {
        title << "Valid/invalid ratio comparison (locality " << locality << ")";
        for (double ratio : {1.0, 0.9, 0.7, 0.5, 0.1}) {
            std::ostringstream label;
            label << "Valid " << ratio;
            runs.push_back({label.str(), frames, pageSize, tlbSize, lru, ratio, locality});
        }
    } else {
        throw std::runtime_error("--compare must be policy, tlb, pagesize or ratio");
    }
    printComparison(title.str(), runs, specs, pageSize, num, seed);
}

// ---------------------------------------------------------------------------
// Main program
// ---------------------------------------------------------------------------
void printUsage() {
    std::cout <<
        "Usage: memory_simulator_advanced [options]\n"
        "With no options the program asks for its configuration.\n\n"
        "Memory:\n"
        "  --frames N        physical frames (default 12)\n"
        "  --pagesize N      page size (default 1000)\n"
        "  --tlb N           TLB entries, 0 turns it off (default 4)\n"
        "  --replace P       lru or fifo (default lru)\n"
        "  --seed N          random seed, for repeatable runs (default: clock)\n\n"
        "Segments (random unless --config is given):\n"
        "  --config FILE     load segments: segID, base, limit, protection, page mappings...\n"
        "  --segments N      number of random segments (default 3)\n\n"
        "Run modes (default: interactive commands):\n"
        "  --batch FILE      translate addresses from FILE, write batch_results.txt\n"
        "  --stress N        translate N random addresses, write random_results.txt\n"
        "  --compare KIND    policy, tlb, pagesize or ratio: side-by-side table on one workload\n\n"
        "Stress options:\n"
        "  --num N           addresses for --compare (default 1000)\n"
        "  --valid R         fraction of valid addresses, 0 to 1 (default 0.7)\n"
        "  --locality L      fraction of references to a hot region, 0 to 1 (default 0.8)\n";
}

void printCommands() {
    std::cout <<
        "Commands:\n"
        "  add <id> <base> <limit> <RO|RW>             create a segment\n"
        "  remove <id>                                 delete a segment and free its frames\n"
        "  translate <seg> <dir> <page> <offset> <0|1> 0 = read, 1 = write\n"
        "  random <num> [validRatio]                   stress test, default ratio 0.7\n"
        "  stats                                       metrics and TLB contents\n"
        "  map                                         memory map\n"
        "  timeline [n]                                last n translations (all by default)\n"
        "  config <frames> <pagesize> <tlb> <lru|fifo> reset with a new configuration\n"
        "  help                                        show this list\n"
        "  quit                                        exit\n";
}

// Prompts with a default, so pressing Enter accepts the value in brackets.
std::string promptLine(const std::string& question, const std::string& fallback) {
    std::cout << question << " [" << fallback << "]: " << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    return answer.empty() ? fallback : answer;
}

void runCommand(SegmentTable& st, const std::string& line, double locality, unsigned& workloadSeed) {
    std::istringstream in(line);
    std::string command;
    in >> command;
    auto usage = [](const std::string& text) { throw std::runtime_error("Usage: " + text); };
    if (command == "add") {
        int id, base, limit;
        std::string prot;
        if (!(in >> id >> base >> limit >> prot)) usage("add <id> <base> <limit> <RO|RW>");
        st.addSegment(id, base, limit, parseProtection(prot));
        std::cout << "Segment " << id << " added\n";
        st.printMemoryMap();
    } else if (command == "remove") {
        int id;
        if (!(in >> id)) usage("remove <id>");
        st.removeSegment(id);
        std::cout << "Segment " << id << " removed\n";
        st.printMemoryMap();
    } else if (command == "translate") {
        int segNum, dirNum, pageNum, offset;
        std::string access;
        if (!(in >> segNum >> dirNum >> pageNum >> offset >> access)) {
            usage("translate <seg> <dir> <page> <offset> <0|1>");
        }
        int addr = st.translateAddress(segNum, dirNum, pageNum, offset, parseAccess(access));
        std::cout << "Physical Address: " << addr << "\n";
    } else if (command == "random") {
        int num;
        double ratio = 0.7;
        if (!(in >> num)) usage("random <num> [validRatio]");
        in >> ratio;
        generateRandomAddresses(st, num, ratio, "random_results.txt", locality, workloadSeed++);
        std::cout << "Results logged to random_results.txt\n";
    } else if (command == "stats") {
        st.displayStats();
    } else if (command == "map") {
        st.printMemoryMap();
    } else if (command == "timeline") {
        int count = 0;
        in >> count;
        st.printTimeline(std::cout, count);
    } else if (command == "config") {
        int frames, pageSize, tlbSize;
        std::string policy;
        if (!(in >> frames >> pageSize >> tlbSize >> policy)) usage("config <frames> <pagesize> <tlb> <lru|fifo>");
        if (policy != "lru" && policy != "fifo") usage("config <frames> <pagesize> <tlb> <lru|fifo>");
        st.configureMemory(frames, pageSize, tlbSize, policy == "lru");
        std::cout << "Memory reconfigured; all segments removed\n";
    } else if (command == "help") {
        printCommands();
    } else {
        throw std::runtime_error("Unknown command '" + command + "' (type help)");
    }
}

int main(int argc, char* argv[]) {
    int numFrames = 12, tlbSize = 4, pageSize = 1000, randomSegments = 3, stressCount = 0, compareNum = 1000;
    bool use_lru = true;
    double validRatio = 0.7, locality = 0.8;
    unsigned seed = (unsigned)std::time(nullptr);
    std::string batchFile, configFile, compareKind;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printUsage();
                return 0;
            }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            std::string value = argv[++i];
            if (arg == "--frames") numFrames = std::stoi(value);
            else if (arg == "--tlb") tlbSize = std::stoi(value);
            else if (arg == "--pagesize") pageSize = std::stoi(value);
            else if (arg == "--replace") {
                if (value != "lru" && value != "fifo") throw std::runtime_error("--replace must be lru or fifo");
                use_lru = value == "lru";
            }
            else if (arg == "--batch") batchFile = value;
            else if (arg == "--config") configFile = value;
            else if (arg == "--seed") seed = (unsigned)std::stoul(value);
            else if (arg == "--segments") randomSegments = std::stoi(value);
            else if (arg == "--stress") stressCount = std::stoi(value);
            else if (arg == "--valid") validRatio = std::stod(value);
            else if (arg == "--locality") locality = std::stod(value);
            else if (arg == "--compare") compareKind = value;
            else if (arg == "--num") compareNum = std::stoi(value);
            else throw std::runtime_error("Unknown option " + arg + " (see --help)");
        }

        if (argc == 1 && isatty(STDIN_FILENO)) {
            std::cout << "Press Enter to accept the value in brackets.\n";
            numFrames = std::stoi(promptLine("Physical memory size (frames)", std::to_string(numFrames)));
            pageSize = std::stoi(promptLine("Page size", std::to_string(pageSize)));
            tlbSize = std::stoi(promptLine("TLB size", std::to_string(tlbSize)));
            use_lru = promptLine("Replacement policy (lru/fifo)", "lru") == "lru";
            std::string file = promptLine("Segment config file (none = random segments)", "none");
            if (file != "none") configFile = file;
        }

        std::mt19937 gen(seed);
        std::vector<SegmentSpec> specs = configFile.empty() ? randomSegmentSpecs(randomSegments, pageSize, gen)
                                                            : loadSegmentFile(configFile);
        if (!compareKind.empty()) {
            runComparison(compareKind, specs, numFrames, pageSize, tlbSize, use_lru, validRatio, locality,
                          compareNum, seed);
            return 0;
        }

        SegmentTable segmentTable(tlbSize, numFrames, use_lru, pageSize, seed);
        applySegmentSpecs(segmentTable, specs);
        if (configFile.empty()) {
            for (const auto& spec : specs) segmentTable.randomizePages(spec.id, gen);
        }

        if (!batchFile.empty()) {
            processBatchFile(segmentTable, batchFile);
            std::cout << "Batch results logged to batch_results.txt\n";
            segmentTable.printTimeline();
            segmentTable.displayStats();
            segmentTable.writeFinalLog();
            return 0;
        }
        if (stressCount > 0) {
            generateRandomAddresses(segmentTable, stressCount, validRatio, "random_results.txt", locality, seed);
            std::cout << "Results logged to random_results.txt\n";
            segmentTable.printTimeline(std::cout, 10);
            segmentTable.displayStats();
            segmentTable.writeFinalLog();
            return 0;
        }

        segmentTable.printMemoryMap();
        printCommands();
        bool piped = !isatty(STDIN_FILENO);
        unsigned workloadSeed = seed;
        std::string line;
        while (true) {
            std::cout << "memsim> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (piped) std::cout << line << "\n";  // Keeps piped sessions readable
            line = stripComment(line);
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            if (line.find("quit") == line.find_first_not_of(" \t")) break;
            try {
                runCommand(segmentTable, line, locality, workloadSeed);
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        }
        segmentTable.writeFinalLog();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
