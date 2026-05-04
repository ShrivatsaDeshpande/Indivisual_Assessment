#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stddef.h>
#include <stdint.h>

#define SHM_NAME "/ssa_shared_mem"
#define PAGE_COUNT 256
#define STRIDE 4096
#define SECRET_VALUE 123
#define SECRET_INDEX (SECRET_VALUE * STRIDE)
#define SHM_SIZE (PAGE_COUNT * STRIDE)

#endif