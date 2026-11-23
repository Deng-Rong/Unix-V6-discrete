#include "MemoryDescriptor.h"
#include "Kernel.h"
#include "PageManager.h"
#include "Machine.h"
#include "PageDirectory.h"
#include "Video.h"
#include "Utility.h"

MemoryDescriptor::MemoryDescriptor()
{
}

MemoryDescriptor::~MemoryDescriptor()
{
}

void MemoryDescriptor::Initialize()  // 为进程分配1#用户页表
{
	KernelPageManager &kernelPageManager = Kernel::Instance().GetKernelPageManager();
	unsigned long page = kernelPageManager.AllocPage();
	if (page == 0)
		Utility::Panic("No page for user page table");
	this->m_UserPageTableArray = (PageTable *)((page << 12) + Machine::KERNEL_SPACE_START_ADDRESS);
	for (unsigned int index = 0; index < PageTable::ENTRY_CNT_PER_PAGETABLE; index++)
	{
		PageTableEntry &entry = this->m_UserPageTableArray->m_Entrys[index];
		entry.m_Present = 0;
		entry.m_ReadWriter = 0;
		entry.m_UserSupervisor = 1;
		entry.m_ForSystemUser = 0;
		entry.m_PageBaseAddress = 0;
	}
}

void MemoryDescriptor::Release()    // 进程释放1#用户页表
{
	KernelPageManager &kernelPageManager = Kernel::Instance().GetKernelPageManager();
	if (this->m_UserPageTableArray)
	{
		int page = ((unsigned long)this->m_UserPageTableArray - Machine::KERNEL_SPACE_START_ADDRESS ) >>12;    // 物理页框号
		kernelPageManager.FreePage(page);
		this->m_UserPageTableArray = NULL;
	}
}

PageTable *MemoryDescriptor::GetUserPageTableArray()
{
	return this->m_UserPageTableArray;
}
unsigned long MemoryDescriptor::GetTextStartAddress()
{
	return this->m_TextStartAddress;
}
unsigned long MemoryDescriptor::GetTextSize()
{
	return this->m_TextSize;
}
unsigned long MemoryDescriptor::GetDataStartAddress()
{
	return this->m_DataStartAddress;
}
unsigned long MemoryDescriptor::GetDataSize()
{
	return this->m_DataSize;
}
unsigned long MemoryDescriptor::GetStackSize()
{
	return this->m_StackSize;
}

void MemoryDescriptor::ClearUserPageTable()
{
	User &u = Kernel::Instance().GetUser();
	PageTable *pUserPageTable = u.u_MemoryDescriptor.m_UserPageTableArray;

	unsigned int j;

	// ------------- NOTE 3: 只负责一张用户页表的清除 -------------
	for (j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++)
	{
		pUserPageTable->m_Entrys[j].m_Present = 0;
		pUserPageTable->m_Entrys[j].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[j].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[j].m_PageBaseAddress = 0;
	}
	// ------------- END NOTE 3 -------------
}

// ------------- NOTE 3: MapToPageTable函数将1#用户页表页框号写到私有的页目录 -------------
void MemoryDescriptor::MapToPageTable()
{
	User &u = Kernel::Instance().GetUser();

	if (NULL == u.u_MemoryDescriptor.m_UserPageTableArray)
	{
		return;     // 选中0#进程上台运行
	}

	FlushPageDirectory(u.u_procp->GetPageDirectoryPhyAddr());
}
// ------------- END NOTE 3 -------------
