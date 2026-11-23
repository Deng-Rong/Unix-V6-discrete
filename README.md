# Unix V6++: Non-contiguous Page Allocation

An educational x86 kernel that allocates process memory as independent **4 KiB physical page frames**. Adjacent virtual pages can map to non-adjacent physical frames, allowing scattered free memory to be reused.

## Memory Management

- **Page allocation:** `AllocPage()` selects a free frame from a bitmap and returns its frame number. Process loading, heap/stack growth, and page-table allocation use individual frames without requiring physical contiguity.
- **Shared metadata:** Kernel and user page pools share one bitmap (`0` = free, `1` = allocated) and one reference-count array, `Page[]`. Both arrays are sized from the physical memory detected at boot and allocated from the kernel heap.
- **Virtual memory:** Processes have private page directories and page tables. Reference counts support shared pages and copy-on-write; memory pressure can trigger process swapping through `EnsureUserPages()`.
- **Kernel heap:** The 512 KiB heap occupies physical addresses `[0x180000, 0x200000)`, mapped at `[0xC0180000, 0xC0200000)`. `KernelAllocator::AllocMemory()` still allocates contiguous heap bytes for kernel objects and metadata. The legacy `PageManager::AllocMemory()` interface remains in the source; current process memory paths use `AllocPage()`.

## Physical Memory Layout

Ranges below are half-open: the end address is excluded.

| Physical range | Use |
| --- | --- |
| `0x000000–0x204000` | Reserved: low memory, kernel image, heap, and fixed paging structures |
| `0x204000–0x380000` | Kernel page pool: page directories and page tables |
| `0x380000–0x400000` | Reserved: terminal history buffers, kernel stack, and fixed process-0 pages |
| `0x400000–PHY_MEM_SIZE` | User page pool: process memory and per-process data areas |

## Main Sources

- `mm/Allocator.cpp`, `mm/PageManager.cpp`: bitmap allocation, pool boundaries, and reference counts.
- `proc/MemoryDescriptor.cpp`, `proc/Process.cpp`, `proc/ProcessManager.cpp`: page mappings, process memory, and swapping.
- `interrupt/Exception.cpp`: copy-on-write page-fault handling.

## Validation

Boot-time checks in `kernel/Kernel.cpp` verify metadata placement, reserved frames, allocation/release, and mapping non-adjacent physical frames to adjacent virtual pages. Successful checks print `Physical page test OK`.

For a memory-pressure test, boot an image containing the user programs with **8 MiB RAM**, then run in the guest shell:

```text
cd /bin
malloc swap
```

The test starts six children, each writing and verifying 170 pages. Expected output: `SWAPTEST 6/6 children passed`. Check swap-out/swap-in logs to confirm swapping occurred.
