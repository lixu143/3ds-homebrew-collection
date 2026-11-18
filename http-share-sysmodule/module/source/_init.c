/**
 * @file _init.c
 * @brief Custom heap setup for a sysmodule.
 *
 * A system module is launched as a raw process: libctru's default
 * __system_allocateHeaps() assumes an application memory layout and does not
 * apply here. Without this file, malloc()/soc buffers have no backing memory
 * and the module crashes immediately. Based on the allocator used by 3Input
 * (which was itself derived from rehid / HorizonMod).
 */
#include <3ds.h>

/* Memory region type, see 3dbrew MemoryRegion. >5 means New 3DS. */
#define APPMEMTYPE (*(u32*)0x1FF80030)

void __system_allocateHeaps(void)
{
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    extern u32 __ctru_heap;
    extern u32 __ctru_heap_size;
    extern u32 __ctru_linear_heap;
    extern u32 __ctru_linear_heap_size;

    extern int __stacksize__;

    u32 tmp = 0;
    Result res = 0;

    /* New 3DS has far more system memory available to a Base process. */
    bool new3ds = APPMEMTYPE > 5;
    u32 heapBytes   = new3ds ? 0x320000 : 0x64000;
    u32 linearBytes = new3ds ? 0x2A0000 : 0;

    __ctru_heap_size = (heapBytes + 0xFFF) & ~0xFFF;
    __ctru_linear_heap_size = linearBytes;
    __stacksize__ = new3ds ? 0x10000 : 0x8000;

    /* Main (non-linear) heap at 0x08000000. */
    __ctru_heap = 0x08000000;
    res = svcControlMemory(&tmp, __ctru_heap, 0, __ctru_heap_size,
                           MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);
    (void)res; /* no console to print to; a failed alloc is fatal anyway */

    /* Linear heap is required by socInit's DMA buffer. */
    if (__ctru_linear_heap_size) {
        res = svcControlMemory(&__ctru_linear_heap, 0, 0, __ctru_linear_heap_size,
                               MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);
        (void)res;
    }

    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);

    /* wire newlib's heap to the memory we just mapped */
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end   = fake_heap_start + __ctru_heap_size;
}
