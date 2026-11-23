#include "PageManager.h"
#include "Allocator.h"
#include "Kernel.h"
#include "Utility.h"

unsigned int PageManager::PHY_MEM_SIZE;
BitMap PageManager::bitmap;
int *PageManager::Page = 0;

PageManager::PageManager(BitMapAllocator *allocator)
{
	m_pAllocator = allocator;
	m_FirstPage = 0;
	m_EndPage = 0;
}

int PageManager::Initialize()
{
	if (Page != 0)
		return 0;

	bitmap.pageCount = PHY_MEM_SIZE / PAGE_SIZE;
	bitmap.rows = (bitmap.pageCount + 63) / 64;
	if (bitmap.pageCount <= UserPageManager::USER_PAGE_POOL_START_ADDR / PAGE_SIZE)
		Utility::Panic("Physical memory too small");

	KernelAllocator &heap = Kernel::Instance().GetKernelAllocator();
	bitmap.map = (unsigned long long *)heap.AllocMemory(bitmap.rows * sizeof(unsigned long long));
	Page = (int *)heap.AllocMemory(bitmap.pageCount * sizeof(int));
	if (bitmap.map == 0 || Page == 0)
		Utility::Panic("Page metadata allocation failed");

	for (unsigned long row = 0; row < bitmap.rows; row++)
		bitmap.map[row] = ~0ULL;
	for (unsigned long page = 0; page < bitmap.pageCount; page++)
	{
		bool kernelPool = page >= KernelPageManager::KERNEL_PAGE_POOL_START_ADDR / PAGE_SIZE &&
			page < (KernelPageManager::KERNEL_PAGE_POOL_START_ADDR + KernelPageManager::KERNEL_PAGE_POOL_SIZE) / PAGE_SIZE;
		bool userPool = page >= UserPageManager::USER_PAGE_POOL_START_ADDR / PAGE_SIZE;
		Page[page] = (kernelPool || userPool) ? 0 : 1;
		if (Page[page] == 0)
			m_pAllocator->set_bit(bitmap, page, 0);
	}
	return 0;
}

void PageManager::SetPool(unsigned long firstAddress, unsigned long endAddress)
{
	m_FirstPage = firstAddress / PAGE_SIZE;
	m_EndPage = endAddress / PAGE_SIZE;
	if (m_EndPage > bitmap.pageCount)
		m_EndPage = bitmap.pageCount;
}

unsigned long PageManager::AllocMemory(unsigned long size)
{
	if (size == 0)
		return 0;
	unsigned long address = m_pAllocator->Alloc(bitmap, size, m_FirstPage, m_EndPage);
	if (address == 0)
		return 0;
	unsigned long firstPage = address / PAGE_SIZE;
	unsigned long pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	for (unsigned long page = firstPage; page < firstPage + pages; page++)
		Page[page] = 1;
	return address;
}

unsigned long PageManager::AllocPage()
{
	unsigned long page = m_pAllocator->Alloc(bitmap, m_FirstPage, m_EndPage);
	if (page != 0)
		Page[page] = 1;
	return page;
}

unsigned long PageManager::AvailablePages() const
{
	unsigned long count = 0;
	for (unsigned long page = m_FirstPage; page < m_EndPage; page++)
		if (m_pAllocator->is_free(bitmap, page))
			count++;
	return count;
}

unsigned long PageManager::FreeMemory(unsigned long size, unsigned long startAddress)
{
	unsigned long firstPage = startAddress / PAGE_SIZE;
	unsigned long pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (startAddress % PAGE_SIZE != 0 || firstPage < m_FirstPage ||
		firstPage + pages > m_EndPage)
		Utility::Panic("Invalid physical memory range");
	for (unsigned long page = firstPage; page < firstPage + pages; page++)
		FreePage(page);
	return 1;
}

unsigned long PageManager::FreePage(unsigned long page)
{
	if (page < m_FirstPage || page >= m_EndPage || Page[page] <= 0)
		Utility::Panic("Invalid physical page release");
	Page[page]--;
	if (Page[page] == 0)
		m_pAllocator->Free(bitmap, page);
	return 1;
}

PageManager::~PageManager()
{
}

KernelPageManager::KernelPageManager(BitMapAllocator *allocator)
	: PageManager(allocator)
{
}

int KernelPageManager::Initialize()
{
	PageManager::Initialize();
	SetPool(KERNEL_PAGE_POOL_START_ADDR, KERNEL_PAGE_POOL_START_ADDR + KERNEL_PAGE_POOL_SIZE);
	return 0;
}

UserPageManager::UserPageManager(BitMapAllocator *allocator)
	: PageManager(allocator)
{
}

int UserPageManager::Initialize()
{
	PageManager::Initialize();
	SetPool(USER_PAGE_POOL_START_ADDR, PHY_MEM_SIZE);
	return 0;
}

unsigned long UserPageManager::AllocPage()
{
	unsigned long page = PageManager::AllocPage();
	if (page == 0 && Kernel::Instance().GetProcessManager().EnsureUserPages(1))
		page = PageManager::AllocPage();
	return page;
}
