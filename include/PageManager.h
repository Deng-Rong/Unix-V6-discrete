#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "MapNode.h"
#include "Allocator.h"

class PageManager									// ---------------------------------------------------
{
public:
	static unsigned int PHY_MEM_SIZE;							/* 物理内存大小，应该在系统启动化时根据物理内存大小设置 */
	static const unsigned int PAGE_SIZE = 0x1000;				/* 物理内存页大小 */
	static const unsigned int MEMORY_MAP_ARRAY_SIZE = 0x200;	/* 最多可分配512个对象 */
	static const unsigned int KERNEL_MEM_START_ADDR = 0x100000; /* 内核映像从1M物理内存开始 */
	static const unsigned int KERNEL_SIZE = 0x80000;			/* 内核映像大小限制(一般二进制映像远不会到512K大小) */

public:
	PageManager(BitMapAllocator *allocator);
	virtual ~PageManager();

	int Initialize();
	unsigned long AllocPage();
	unsigned long FreePage(unsigned long startAddress);
	unsigned long AvailablePages() const;

	unsigned long AllocMemory(unsigned long size);
	unsigned long FreeMemory(unsigned long size, unsigned long memoryStartAddress);

private:
	PageManager();

public:
	static BitMap bitmap;
	static int *Page;

private:
	BitMapAllocator *m_pAllocator;
	unsigned long m_FirstPage;
	unsigned long m_EndPage;

protected:
	void SetPool(unsigned long firstAddress, unsigned long endAddress);
};

class KernelPageManager : public PageManager    	// ---------------------------------------------------
{
public:
	static const unsigned int KERNEL_PAGE_POOL_START_ADDR = 0x200000 + 0x4000;
	static const unsigned int KERNEL_PAGE_POOL_END_ADDR = 0x380000;
	static const unsigned int KERNEL_PAGE_POOL_SIZE = KERNEL_PAGE_POOL_END_ADDR - KERNEL_PAGE_POOL_START_ADDR;

public:
	KernelPageManager(BitMapAllocator *allocator);
	int Initialize();
};

class UserPageManager : public PageManager  		// ---------------------------------------------------
{
public:
	static const unsigned int USER_PAGE_POOL_START_ADDR = 0x400000;

public:
	UserPageManager(BitMapAllocator *allocator);
	int Initialize();
	unsigned long AllocPage();
};

#endif
