#include "Kernel.h"
#include "Machine.h"
#include "New.h"
#include "Video.h"
#include "Assembly.h"
#include "Utility.h"

Kernel Kernel::instance;

UserPageManager g_UserPageManager(&(BitMapAllocator::GetInstance()));
KernelPageManager g_KernelPageManager(&(BitMapAllocator::GetInstance()));
KernelAllocator g_KernelAllocator(&(Allocator::GetInstance()));      // 单体对象，内存分配器
SwapperManager g_SwapperManager(&(Allocator::GetInstance()));
ProcessManager g_ProcessManager;
BufferManager g_BufferManager;
DeviceManager g_DeviceManager;
FileSystem g_FileSystem;
FileManager g_FileManager;

Kernel::Kernel()
{
}

Kernel::~Kernel()
{
}

Kernel &Kernel::Instance()
{
	return Kernel::instance;
}

void Kernel::InitMemory()
{
	this->m_KernelAllocator = &g_KernelAllocator;
	Diagnose::Write("Initilize KernelAllocator...");
	this->GetKernelAllocator().Initialize();
	Diagnose::Write("Ok.\n");
	set_kernel_allocator(this->m_KernelAllocator);

	this->m_KernelPageManager = &g_KernelPageManager;
	this->m_UserPageManager = &g_UserPageManager;

	Diagnose::Write("Initilize Memory...");
	this->GetKernelPageManager().Initialize();
	this->GetUserPageManager().Initialize();
	Diagnose::Write("Ok.\n");

	BitMap &bitmap = PageManager::bitmap;
	unsigned long heapStart = KernelAllocator::KERNEL_HEAP_START_ADDR;
	unsigned long heapEnd = heapStart + KernelAllocator::KERNEL_HEAP_SIZE;
	unsigned long bitmapEnd = (unsigned long)bitmap.map + bitmap.rows * sizeof(unsigned long long);
	unsigned long pageArrayEnd = (unsigned long)PageManager::Page + bitmap.pageCount * sizeof(int);
	BitMapAllocator &allocator = BitMapAllocator::GetInstance();
	unsigned long kernelFirst = KernelPageManager::KERNEL_PAGE_POOL_START_ADDR / PageManager::PAGE_SIZE;
	unsigned long kernelEnd = KernelPageManager::KERNEL_PAGE_POOL_END_ADDR / PageManager::PAGE_SIZE;
	unsigned long userFirst = UserPageManager::USER_PAGE_POOL_START_ADDR / PageManager::PAGE_SIZE;
	if ((unsigned long)bitmap.map < heapStart || bitmapEnd > heapEnd ||
		(unsigned long)PageManager::Page < heapStart || pageArrayEnd > heapEnd ||
		bitmap.pageCount != PageManager::PHY_MEM_SIZE / PageManager::PAGE_SIZE)
		Utility::Panic("Physical page metadata test failed");
	for (unsigned long page = 0; page < bitmap.pageCount; page++)
	{
		bool expectedFree = (page >= kernelFirst && page < kernelEnd) || page >= userFirst;
		if (PageManager::Page[page] != (expectedFree ? 0 : 1) ||
			allocator.is_free(bitmap, page) != expectedFree)
			Utility::Panic("Physical page initialization test failed");
	}

	UserPageManager &userPages = this->GetUserPageManager();
	unsigned long first = userPages.AllocPage();
	unsigned long middle = userPages.AllocPage();
	unsigned long last = userPages.AllocPage();
	userPages.FreePage(middle);
	unsigned long reused = userPages.AllocPage();
	unsigned long next = userPages.AllocPage();
	if (first == 0 || last == 0 || reused != middle || next == reused + 1 ||
		PageManager::Page[reused] != 1 || PageManager::Page[next] != 1 ||
		allocator.is_free(bitmap, reused) || allocator.is_free(bitmap, next))
		Utility::Panic("Discontinuous page allocation test failed");

	PageTableEntry &leftEntry = Machine::Instance().GetKernelPageTable().m_Entrys[Utility::borrowedPTE];
	PageTableEntry &rightEntry = Machine::Instance().GetKernelPageTable().m_Entrys[Utility::borrowedPTE + 1];
	PageTableEntry savedLeft = leftEntry;
	PageTableEntry savedRight = rightEntry;
	leftEntry.m_PageBaseAddress = reused;
	rightEntry.m_PageBaseAddress = next;
	FlushPageDirectory(Machine::PAGE_DIRECTORY_BASE_ADDRESS);
	volatile unsigned long *left = (volatile unsigned long *)(Machine::KERNEL_SPACE_START_ADDRESS +
		Utility::borrowedPTE * PageManager::PAGE_SIZE);
	volatile unsigned long *right = (volatile unsigned long *)(Machine::KERNEL_SPACE_START_ADDRESS +
		(Utility::borrowedPTE + 1) * PageManager::PAGE_SIZE);
	*left = 0x13579BDF;
	*right = 0x2468ACE0;
	if (*left != 0x13579BDF || *right != 0x2468ACE0)
		Utility::Panic("Discontinuous page mapping test failed");
	*left = 0;
	*right = 0;
	leftEntry = savedLeft;
	rightEntry = savedRight;
	FlushPageDirectory(Machine::PAGE_DIRECTORY_BASE_ADDRESS);
	userPages.FreePage(first);
	userPages.FreePage(last);
	userPages.FreePage(reused);
	userPages.FreePage(next);
	if (PageManager::Page[reused] != 0 || !allocator.is_free(bitmap, reused) ||
		PageManager::Page[next] != 0 || !allocator.is_free(bitmap, next))
		Utility::Panic("Physical page release test failed");
	Diagnose::Write("Physical page test OK: %x, %x -> adjacent virtual pages\n", reused, next);

	this->m_SwapperManager = &g_SwapperManager;
	Diagnose::Write("Initialize Swapper...");
	this->GetSwapperManager().Initialize();
	Diagnose::Write("Ok.\n");
}

void Kernel::InitProcess()
{
	this->m_ProcessManager = &g_ProcessManager;

	Diagnose::Write("Initilize Process...");
	this->GetProcessManager().Initialize();
	Diagnose::Write("Ok.\n");
}

void Kernel::InitBuffer()
{
	this->m_BufferManager = &g_BufferManager;
	this->m_DeviceManager = &g_DeviceManager;

	Diagnose::Write("Initialize Buffer...");
	this->GetBufferManager().Initialize();
	Diagnose::Write("OK.\n");

	Diagnose::Write("Initialize Device Manager...");
	this->GetDeviceManager().Initialize();
	Diagnose::Write("OK.\n");
}

void Kernel::InitFileSystem()
{
	this->m_FileSystem = &g_FileSystem;
	this->m_FileManager = &g_FileManager;

	Diagnose::Write("Initialize File System...");
	this->GetFileSystem().Initialize();
	Diagnose::Write("OK.\n");

	Diagnose::Write("Initialize File Manager...");
	this->GetFileManager().Initialize();
	Diagnose::Write("OK.\n");
}

void Kernel::Initialize()
{
	InitMemory();
	InitProcess();
	InitBuffer();
	InitFileSystem();
}

KernelPageManager &Kernel::GetKernelPageManager()
{
	return *(this->m_KernelPageManager);
}

UserPageManager &Kernel::GetUserPageManager()
{
	return *(this->m_UserPageManager);
}

ProcessManager &Kernel::GetProcessManager()
{
	return *(this->m_ProcessManager);
}

KernelAllocator &Kernel::GetKernelAllocator()
{
	return *(this->m_KernelAllocator);
}

SwapperManager &Kernel::GetSwapperManager()
{
	return *(this->m_SwapperManager);
}

BufferManager &Kernel::GetBufferManager()
{
	return *(this->m_BufferManager);
}

DeviceManager &Kernel::GetDeviceManager()
{
	return *(this->m_DeviceManager);
}

FileSystem &Kernel::GetFileSystem()
{
	return *(this->m_FileSystem);
}

FileManager &Kernel::GetFileManager()
{
	return *(this->m_FileManager);
}

User &Kernel::GetUser()
{
	return *(User *)USER_ADDRESS;
}
