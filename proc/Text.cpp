#include "Text.h"
#include "Kernel.h"

Text::Text()
{
	// nothing to do here
}

Text::~Text()
{
	// nothing to do here
}

void Text::XccDec()
{
	User &u = Kernel::Instance().GetUser();
	XccDec(u.u_MemoryDescriptor.m_UserPageTableArray, u.u_MemoryDescriptor);
}

void Text::XccDec(PageTable *pUserPageTable, const MemoryDescriptor &descriptor)
{
	UserPageManager &userPageMgr = Kernel::Instance().GetUserPageManager();

	--this->x_ccount;  // 引用计数器递减

	int index = (descriptor.m_TextStartAddress >> 12) - 1024;
	int count = (descriptor.m_TextSize + PageManager::PAGE_SIZE - 1) >> 12;

	while (count)
	{
		if (this->x_ccount == 0)
			userPageMgr.FreePage(pUserPageTable->m_Entrys[index].m_PageBaseAddress);  // 释放物理页框

		pUserPageTable->m_Entrys[index].m_Present = 0;				// 清空PTE
		pUserPageTable->m_Entrys[index].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[index].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[index].m_PageBaseAddress = 0;

		index++; 		count--;
	}
}

void Text::XFree()
{
	/* 释放物理内存中的副本 */
	this->XccDec();

	/* 释放盘交换区上的副本 */
	if (--this->x_count == 0)
	{
		unsigned int pages = (this->x_size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
		Kernel::Instance().GetSwapperManager().FreeSwap(pages * PageManager::PAGE_SIZE, this->x_daddr);
		Kernel::Instance().GetFileManager().m_InodeTable->IPut(this->x_iptr);
		this->x_iptr = NULL;
	}
}
