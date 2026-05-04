# Ghost in the Shared Space

This project is a Proof of Concept (PoC) for a Flush+Reload software side-channel attack using POSIX shared memory in C.

## Project Structure

ssa-assessment/
├── Makefile
├── README.md
├── attacker/
│   └── attacker.c
├── build/
├── dist/
├── docs/
│   ├── report/
│   └── screenshots/
├── shared/
│   └── shared_mem.h
└── victim/
    └── victim.c

## Files for Examiner

- `victim/victim.c` -> Victim process
- `attacker/attacker.c` -> Attacker process
- `shared/shared_mem.h` -> Shared constants and memory layout

## Marking Criteria Map

### Criterion 1: Functional attacker with Flush+Reload, `rdtscp`, and STRIDE 4096
- `attacker/attacker.c`
- `flush_line()` performs cache flushing.
- `measure_access_time()` uses `__rdtscp()` timing.
- `flush_all_pages()` scans 256 pages using `i * STRIDE`.
- The main loop repeatedly flushes, waits, reloads, measures, and ranks candidates.

### Criterion 2: Shared memory implementation with `shm_open` and `ftruncate`
- `victim/victim.c`
  - `shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666)`
  - `ftruncate(shm_fd, SHM_SIZE)`
  - `mmap(... MAP_SHARED ...)`
- `attacker/attacker.c`
  - `shm_open(SHM_NAME, O_RDWR, 0666)`
  - `mmap(... MAP_SHARED ...)`

### Criterion 3: Victim active in `while(1)` and touching location tied to the secret
- `victim/victim.c`
- `SECRET_VALUE = 123`
- `SECRET_INDEX = SECRET_VALUE * STRIDE`
- Victim continuously touches `shared_mem[SECRET_INDEX]` inside a loop.

## Ubuntu dependencies

```bash
sudo apt update
sudo apt install -y build-essential gcc make libc6-dev manpages-dev gdb valgrind zip unzip
```

## Build

```bash
make clean
make
```

Or manually:

```bash
gcc -O0 -Wall -Wextra -std=c11 -Ishared -o build/victim victim/victim.c -lrt
gcc -O0 -Wall -Wextra -std=c11 -Ishared -o build/attacker attacker/attacker.c -lrt
```

## Run

Open two terminals.

### Terminal 1
```bash
cd ~/ssa-assessment
./build/victim
```

### Terminal 2
```bash
cd ~/ssa-assessment
./build/attacker
```

## Expected Behaviour

- Victim creates shared memory and stays active.
- Attacker connects to the same memory region.
- Attacker performs Flush+Reload using a 4096-byte stride.
- Attacker uses timing to infer the most likely page index.
- Under favourable conditions, the guessed index should converge toward 123.

## Notes

- Results may vary based on hardware and timing precision.
- A working example is acceptable even if the results are not perfect.




sudo apt update
sudo apt install -y build-essential gcc make libc6-dev manpages-dev gdb valgrind zip unzip

cd ~
mkdir -p ssa-assessment/{attacker,victim,shared,build,dist,docs/report,docs/screenshots}
cd ssa-assessment


make clean
make

./build/victim
./build/attacker