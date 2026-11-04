# CS375 Lab 9 – TLB, Multi-Level Paging and Dynamic Memory Management

A C++ simulator of a segmented, paged memory system with a TLB, two-level page tables, LRU and FIFO
page replacement, runtime segment creation and removal, metrics and stress testing, plus a LaTeX report.

## 📁 Folder layout

```
CS375-Lab9-TLB-Multi-Level-Paging/
├── submission/                            ← ✅ DRAG THESE 4 FILES INTO CANVAS
│   ├── memory_simulator_advanced.cpp      Copy of src/memory_simulator_advanced.cpp
│   ├── segments.txt                       Copy of src/segments.txt
│   ├── addresses.txt                      Copy of src/addresses.txt
│   └── report.pdf                         Copy of report/report.pdf
├── src/                                   Source code: edit files here, not in submission/
│   ├── memory_simulator_advanced.cpp      The simulator
│   ├── segments.txt                       Sample segment configuration
│   └── addresses.txt                      Sample batch of addresses
├── report/
│   ├── report.tex                         LaTeX source for the report
│   ├── report.pdf                         The built report
│   └── screenshots/                       The 12 PNG screenshots used by the report
│       ├── 01_compile.png ... 12_valid_ratio.png
│       └── originals/                     Uncropped copies of 05, 07, 08 and 09
├── tests/
│   └── test_simulator.cpp                 Regression tests
├── scripts/
│   └── seed_sweep.sh                      Repeats the experiments over seeds 1-10
├── Makefile                               Build shortcuts (listed below)
├── .gitignore
└── README.md
```

Running the project also creates these, which git ignores and Canvas doesn't need:

| Generated | Created by |
|-----------|------------|
| `bin/` | `make` and `make test` |
| `batch_results.txt` | `--batch` |
| `random_results.txt` | `--stress` or the `random` command |
| `memory_log.txt` | Any simulator run, except `--compare` |

### Makefile shortcuts

| Command | What it does |
|---------|--------------|
| `make` | Builds `bin/memory_simulator_advanced` |
| `make run` | Builds and starts the simulator |
| `make test` | Builds and runs the regression tests |
| `make valgrind` | Runs a memory leak check |
| `make report` | Rebuilds `report/report.pdf` |
| `make submission` | Rebuilds the report and refreshes `submission/` |
| `make clean` | Removes `bin/`, generated output and LaTeX build files |

## ✅ What to submit on Canvas

Drag all 4 files from `submission/` into Canvas. To open the folder in Windows:

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging/submission && explorer.exe .
```

| File | Contents |
|------|----------|
| `memory_simulator_advanced.cpp` | The simulator |
| `segments.txt` | Sample segment configuration |
| `addresses.txt` | Sample batch of addresses |
| `report.pdf` | Report with the screenshots, all 20 answers and the discussion |

`submission/` holds copies, so make any changes in `src/` or `report/`, then run `make submission`.

### Submission checklist

Every item in the handout's "Submission Requirements", and where it is met:

| Requirement | Where |
|-------------|-------|
| `memory_simulator_advanced.cpp` and any additional files | `submission/`: the source, plus `segments.txt` and `addresses.txt`, which it reads |
| A header in each file with your name and contributions | The top of all three source and input files; the report's title page |
| Report (PDF/Word) | `submission/report.pdf` |
| Screenshots: 5 manual translations, valid and invalid | Report §3.2, Figure 2 (7 translations) |
| Screenshots: batch output (`batch_results.txt`) | Report §3.3, Figure 3 |
| Screenshots: random address test (`random_results.txt`) | Report §3.4, Figure 4 |
| Screenshots: memory map and stats after segment add/remove | Report §3.5, Figures 5-8 |
| Answers to all 20 questions | Report §5 (questions 1-10) and §6 (questions 11-20, each 300-360 words) |
| Discussion of challenges, enhancements and test results | Report §4 (LRU vs FIFO, TLB size, page size, valid ratio) and §7 |
| External references cited | Report §8 |

The generated files `batch_results.txt`, `random_results.txt` and `memory_log.txt` are not submitted. The
handout asks for screenshots of them, which are in the report.

## 🔧 Setup (Ubuntu / WSL)

```bash
sudo apt update && sudo apt install build-essential valgrind
sudo apt install texlive-full          # only needed to build the report
```

## ▶️ Compile and run

```bash
make                                   # builds bin/memory_simulator_advanced
./bin/memory_simulator_advanced        # asks for frames, page size, TLB size and policy
./bin/memory_simulator_advanced --help # lists every option
```

To compile by hand, as a grader might:

```bash
cd src
g++ -o memory_simulator_advanced memory_simulator_advanced.cpp -std=c++11
./memory_simulator_advanced
```

### Commands inside the simulator

| Command | Meaning |
|---------|---------|
| `add <id> <base> <limit> <RO\|RW>` | Create a segment, for example `add 3 10000 5 RW` |
| `remove <id>` | Delete a segment and free its frames |
| `translate <seg> <dir> <page> <offset> <0\|1>` | Translate an address (0 = read, 1 = write) |
| `random <num> [validRatio]` | Stress test, written to `random_results.txt` |
| `stats` | Metrics, per-segment faults and suggestions, TLB contents |
| `map` | Memory map, with allocated and free frames |
| `timeline [n]` | Timeline of the last `n` translations |
| `config <frames> <pagesize> <tlb> <lru\|fifo>` | Reset with a new configuration |
| `help` / `quit` | Show commands / exit |

### Command-line options

| Option | Meaning | Default |
|--------|---------|---------|
| `--frames N` | Physical frames | 12 |
| `--pagesize N` | Page size | 1000 |
| `--tlb N` | TLB entries (0 turns it off) | 4 |
| `--replace lru\|fifo` | Replacement policy | lru |
| `--seed N` | Random seed, for repeatable runs | clock |
| `--config FILE` | Load segments from a file | random segments |
| `--segments N` | Number of random segments | 3 |
| `--batch FILE` | Translate addresses from a file | |
| `--stress N` | Translate N random addresses | |
| `--valid R` | Fraction of valid addresses | 0.7 |
| `--locality L` | Fraction of references to a hot region | 0.8 |
| `--compare KIND` | Side-by-side table: `policy`, `tlb`, `pagesize` or `ratio` | |
| `--num N` | Addresses for `--compare` | 1000 |

### Input file formats

`segments.txt`: `segID, base, limit, protection, page mappings...`, where a mapping is `page:frame`,
`page:frame:RO`, or `page:-:RO` for a page that starts out of memory. Pages are numbered across
directories, and each table holds 4 pages, so page 5 is directory 1, page 1.

`addresses.txt`: `segNum dirNum pageNum offset access`, one per line, where access is 0 or R for a
read and 1 or W for a write. In both files, `#` starts a comment.

## 📸 Screenshots

All 12 screenshots are finished and saved in `report/screenshots/`. The commands below reproduce them
if one needs to be retaken:

1. In VS Code, click the **^** at the top right of the terminal panel to maximize it.
2. Clear the terminal, then paste one block below.
3. Press **Win + Shift + S** and drag a box over the range listed under the block.
4. Save it to `report/screenshots/` with the exact file name shown, replacing the old file. To open that
   folder in Windows, run:

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging/report/screenshots && explorer.exe .
```

5. Run `make submission` to put the new screenshot in the report.

Each block creates the files it displays, and the seeds make the output match the report's tables exactly.

### 1. `01_compile.png` – compile and tests

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
make clean && make && make test
```

Capture from the `make clean` line to `73/73 checks passed`.

### 2. `02_manual_translations.png` – manual translations, valid and invalid

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 <<'EOF' | tail -n 15
translate 0 0 1 250 0
translate 0 0 1 300 1
translate 0 1 3 10 0
translate 1 0 0 0 1
translate 5 0 0 0 0
translate 0 7 0 0 0
translate 0 0 0 1500 0
quit
EOF
```

Capture from the first `memsim> translate 0 0 1 250 0` to `memsim> quit`.

### 3. `03_batch_results.png` – batch output

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --frames 6 --tlb 4 --seed 42 --batch src/addresses.txt > /dev/null
cat batch_results.txt
```

Capture every line, ending at `Page Faults: 8`.

### 4. `04_random_results.png` – random address test output

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --frames 6 --tlb 4 --seed 42 --stress 1000 --valid 0.7 > /dev/null
head -n 12 random_results.txt; echo "..."; tail -n 8 random_results.txt
```

Capture every line, ending at `Segmentation Fault: 65`.

### 5. `05_add_map.png` – memory map after adding a segment

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 <<'EOF' | tail -n 22
translate 0 0 0 100 0
add 3 90000 5 RW
translate 3 0 0 100 1
translate 3 1 0 200 0
map
quit
EOF
```

Capture from `memsim> map` to `memsim> quit`. Segment 3 appears at the bottom of the list.

### 6. `06_add_stats.png` – stats after adding a segment

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 <<'EOF' | tail -n 25
translate 0 0 0 100 0
add 3 90000 5 RW
translate 3 0 0 100 1
translate 3 1 0 200 0
stats
quit
EOF
```

Capture from `memsim> stats` to `memsim> quit`.

### 7. `07_remove_map.png` – memory map after removing a segment

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 <<'EOF' | tail -n 22
translate 0 0 0 100 0
add 3 90000 5 RW
translate 3 0 0 100 1
translate 3 1 0 200 0
remove 3
translate 3 0 0 100 0
quit
EOF
```

Capture from `memsim> remove 3` to `memsim> quit`, including the `Segmentation Fault` line.

### 8. `08_remove_stats.png` – stats after removing a segment

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 <<'EOF' | tail -n 22
translate 0 0 0 100 0
add 3 90000 5 RW
translate 3 0 0 100 1
translate 3 1 0 200 0
remove 3
stats
quit
EOF
```

Capture from `memsim> stats` to `memsim> quit`.

### 9. `09_lru_vs_fifo.png` – LRU vs FIFO

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --valid 1.0 --tlb 8 --frames 12 --compare policy
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --valid 1.0 --tlb 8 --frames 6 --compare policy
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --valid 1.0 --tlb 8 --frames 12 --locality 0 --compare policy
```

Capture all three tables.

### 10. `10_tlb_size.png` – TLB size 4 vs 16

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --valid 1.0 --frames 20 --compare tlb
```

Capture the whole table.

### 11. `11_page_size.png` – page size 1000 vs 4000

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --valid 1.0 --frames 20 --tlb 8 --compare pagesize
```

Capture the whole table.

### 12. `12_valid_ratio.png` – valid/invalid ratio

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
./bin/memory_simulator_advanced --config src/segments.txt --seed 42 --frames 12 --tlb 8 --compare ratio
```

Capture the whole table.

## 📄 Build the report

```bash
cd ~/CS375-Lab9-TLB-Multi-Level-Paging
make report
```

This creates `report/report.pdf`. Each `\screenshot{...}` line in `report.tex` loads the matching file
from `report/screenshots/`, and a missing file shows up as a "Screenshot missing" box, so search the PDF
for that phrase before submitting.

Screenshots 05, 07, 08 and 09 are cropped to remove garbled pasted commands left by a terminal redraw
glitch. Only those top rows were removed, and the uncropped versions are in `report/screenshots/originals/`.

## 🧪 Verify

```bash
make test                # 73 checks, with AddressSanitizer and UBSan
make valgrind            # memory leak check on a session with add, stress and remove
scripts/seed_sweep.sh    # repeats the experiments over seeds 1-10
```

## Notes on changes to the starter code

The starter code did not compile, and some defects would have invalidated the experiments. Section 2.3
of the report has the full table. The most important changes:

- **Page faults are counted.** The starter never counted demand faults, so LRU and FIFO could not be compared.
- **Eviction is correct.** An evicted page stayed `present`, so two pages could share a frame. It is now
  invalidated, along with its TLB entry.
- **TLB hits enforce protection** and update `last_access`.
- **A page fault no longer changes a page's protection.** A write fault used to make a read-only page writable.
- **`--pagesize` works.** It was ignored, and 1000 was hard-coded.
- **Segments are stored by ID**, so `add 3 ...` works with two existing segments.
- **`removeSegment` clears the TLB and frees all its memory.**
- **LRU uses `last_access` timestamps**, as Challenge 1 requires.
- **Invalid stress addresses are really invalid.** The starter multiplied by 0, which produced valid addresses.

Design decisions to be aware of:

- A segment's **limit is its total page count**, and each table holds 4 pages. In the starter, `dirNum` had no
  bound, so the required "Invalid dirNum 5, max 3" error was impossible. With this design, segment 0 of
  `segments.txt` (limit 16) has directories 0-3.
- **Bounds are checked before the TLB lookup**, as in the starter, so invalid addresses don't count as misses.
- The **physical address formula** is the starter's `base + frame * pageSize + offset`.
- **Latency** is the required random 1-10 units, plus 10 per level on a TLB miss and 100 for a page fault.
  Without these additions the TLB could not affect latency.
