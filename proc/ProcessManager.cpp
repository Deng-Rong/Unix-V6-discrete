#include "ProcessManager.h"
#include "Machine.h"
#include "User.h"
#include "Kernel.h"
#include "Video.h"
#include "Utility.h"
#include "PEParser.h"
#include "Regs.h"
#include "MemoryDescriptor.h"

unsigned int ProcessManager::m_NextUniquePid = 0;

static PageTable *GetProcessPageTable(Process *process)
{
	unsigned long frame = process->pPageDirectory->m_Entrys[1].m_PageTableBaseAddress;
	return (PageTable *)((frame << 12) + Machine::KERNEL_SPACE_START_ADDRESS);
}

static MemoryDescriptor ReadMemoryDescriptor(unsigned long ppdaAddress)
{
	Process *current = Kernel::Instance().GetUser().u_procp;
	PageTableEntry &entry = Machine::Instance().GetKernelPageTable().m_Entrys[Utility::borrowedPTE + 2];
	unsigned long originalFrame = entry.m_PageBaseAddress;
	X86Assembly::CLI();
	entry.m_PageBaseAddress = ppdaAddress >> 12;
	FlushPageDirectory(current->GetPageDirectoryPhyAddr());
	User *mappedUser = (User *)(Machine::KERNEL_SPACE_START_ADDRESS +
		(Utility::borrowedPTE + 2) * PageManager::PAGE_SIZE);
	MemoryDescriptor descriptor = mappedUser->u_MemoryDescriptor;
	entry.m_PageBaseAddress = originalFrame;
	FlushPageDirectory(current->GetPageDirectoryPhyAddr());
	X86Assembly::STI();
	return descriptor;
}

static bool IsPrivatePage(const MemoryDescriptor &descriptor, unsigned int index)
{
	unsigned long address = (index + PageTable::ENTRY_CNT_PER_PAGETABLE) * PageManager::PAGE_SIZE;
	bool dataPage = descriptor.m_DataSize != 0 &&
		address < descriptor.m_DataStartAddress + descriptor.m_DataSize &&
		address + PageManager::PAGE_SIZE > descriptor.m_DataStartAddress;
	bool stackPage = descriptor.m_StackSize != 0 &&
		address >= MemoryDescriptor::USER_SPACE_SIZE - descriptor.m_StackSize;
	return dataPage || stackPage;
}

ProcessManager::ProcessManager()
{
	CurPri = 0;
	RunRun = 0;
	RunIn = 0;
	RunOut = 0;
	ExeCnt = 0;
	SwtchNum = 0;
}

ProcessManager::~ProcessManager()
{
}

void ProcessManager::Initialize()
{
	// nothing to do here
}

// ------------- NOTE 3: 进程创建页目录并引用内核页表 -------------
PageDirectory *ProcessManager::AllocPageDirectory()
{
	KernelPageManager &kernelPageManager = Kernel::Instance().GetKernelPageManager();
	unsigned long page = kernelPageManager.AllocPage();
	if (page == 0)
		Utility::Panic("No page for process page directory");
	return (PageDirectory *)((page << 12) + Machine::KERNEL_SPACE_START_ADDRESS);
}

void ProcessManager::InitProcPageDirectory(Process *proc, PageTable * privateUsrPageTable)
{
	PageDirectory *pPageDirectory = proc->pPageDirectory;
	if (pPageDirectory)
	{
		// 引用公用的0#用户页表
		pPageDirectory->m_Entrys[0].m_UserSupervisor = 1;
		pPageDirectory->m_Entrys[0].m_Present = 1;
		pPageDirectory->m_Entrys[0].m_ReadWriter = 1;
		pPageDirectory->m_Entrys[0].m_PageTableBaseAddress = Machine::USER_PAGE_TABLE_BASE_ADDRESS >> 12;
		// 引用核心页表
		unsigned int kPageTableIdx = Machine::KERNEL_SPACE_START_ADDRESS / PageTable::SIZE_PER_PAGETABLE_MAP;
		pPageDirectory->m_Entrys[kPageTableIdx].m_UserSupervisor = 0;
		pPageDirectory->m_Entrys[kPageTableIdx].m_Present = 1;
		pPageDirectory->m_Entrys[kPageTableIdx].m_ReadWriter = 1;
		pPageDirectory->m_Entrys[kPageTableIdx].m_PageTableBaseAddress = Machine::KERNEL_PAGE_TABLE_BASE_ADDRESS >> 12;
		// 引用私有的1#用户页表
		unsigned long phyFrame = ((unsigned long)privateUsrPageTable - Machine::KERNEL_SPACE_START_ADDRESS) >> 12;
		pPageDirectory->m_Entrys[1].m_PageTableBaseAddress = phyFrame;
		pPageDirectory->m_Entrys[1].m_UserSupervisor = 1;
		pPageDirectory->m_Entrys[1].m_Present = 1;
		pPageDirectory->m_Entrys[1].m_ReadWriter = 1;
	}
}
// ------------- END NOTE 3 -------------

void ProcessManager::SetupProcessZero()
{
	// 初始化Process#0的Process和User结构
	Process *pProcZero = &(this->process[0]);
	pProcZero->p_stat = Process::SRUN;
	pProcZero->p_flag = Process::SLOAD | Process::SSYS;
	pProcZero->p_nice = 0;
	pProcZero->p_time = 0;
	pProcZero->p_pid = NextUniquePid();
	pProcZero->p_size = 0x1000;
	pProcZero->p_addr = PROCESS_ZERO_PPDA_ADDRESS;
	pProcZero->p_textp = NULL;

	User &u = Kernel::Instance().GetUser();
	u.u_procp = pProcZero;
	u.u_MemoryDescriptor.m_TextStartAddress = 0;
	u.u_MemoryDescriptor.m_TextSize = 0;
	u.u_MemoryDescriptor.m_DataStartAddress = 0;
	u.u_MemoryDescriptor.m_DataSize = 0;
	u.u_MemoryDescriptor.m_StackSize = 0;
	u.u_MemoryDescriptor.m_UserPageTableArray = NULL;
	pProcZero->pPageDirectory = (PageDirectory *)(Machine::PAGE_DIRECTORY_BASE_ADDRESS + Machine::KERNEL_SPACE_START_ADDRESS);
}

unsigned int ProcessManager::NextUniquePid()
{
	return ProcessManager::m_NextUniquePid++;
}

// ------------- NOTE COW: 修改父进程页表中的RW标识 -------------
void ProcessManager::ModifyPageTable(UserPageManager &userPageManager, PageTable* pgTable) {
	MemoryDescriptor &descriptor = Kernel::Instance().GetUser().u_MemoryDescriptor;
	unsigned long dataEnd = descriptor.m_DataStartAddress + descriptor.m_DataSize;
	unsigned long stackStart = MemoryDescriptor::USER_SPACE_SIZE - descriptor.m_StackSize;
	for (unsigned int j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++) {
		unsigned long address = (j + PageTable::ENTRY_CNT_PER_PAGETABLE) * PageManager::PAGE_SIZE;
		bool dataPage = descriptor.m_DataSize != 0 && address < dataEnd &&
			address + PageManager::PAGE_SIZE > descriptor.m_DataStartAddress;
		bool stackPage = descriptor.m_StackSize != 0 && address < MemoryDescriptor::USER_SPACE_SIZE &&
			address + PageManager::PAGE_SIZE > stackStart;
		if (!pgTable->m_Entrys[j].m_Present || (!dataPage && !stackPage))
			continue;
		unsigned long page = pgTable->m_Entrys[j].m_PageBaseAddress;
		if (page >= PageManager::bitmap.pageCount || userPageManager.Page[page] <= 0)
			Utility::Panic("Invalid shared user page");
		pgTable->m_Entrys[j].m_ReadWriter = 0;
		userPageManager.Page[page]++;
	}
}
// ------------- END NOTE COW -------------

int ProcessManager::NewProc()
{
	Process *child = 0;
	for (int i = 0; i < ProcessManager::NPROC; i++)
	{
		if (process[i].p_stat == Process::SNULL)
		{
			child = &process[i];
			break;
		}
	}
	if (!child)
	{
		Utility::Panic("No Proc Entry!");
	}

	User &u = Kernel::Instance().GetUser();
	Process *current = (Process *)u.u_procp;
	current->Clone(*child);

	PageTable *pgTable = u.u_MemoryDescriptor.m_UserPageTableArray;  // 父进程用户态页表指针

	child->pPageDirectory = ProcessManager::AllocPageDirectory();   // 分配页目录
	u.u_MemoryDescriptor.Initialize();  // 分配1#用户页表
	PageTable *desPgTable = u.u_MemoryDescriptor.m_UserPageTableArray;
	ProcessManager::InitProcPageDirectory(child, desPgTable);   // 初始化二级页表

	UserPageManager &userPageManager = Kernel::Instance().GetUserPageManager();
	unsigned long desAddress = userPageManager.AllocPage()<<12;

	SaveU(u.u_rsav);
	u.u_procp = child;

	if (desAddress == 0)
	{
		Utility::Panic("No page for process image");
	}

	child->p_addr = desAddress;
	Utility::CopySeg(current->p_addr, desAddress);

	if (pgTable) {
		ProcessManager::ModifyPageTable(userPageManager, pgTable);
		Utility::MemCopy((unsigned long)pgTable, (unsigned long)desPgTable, sizeof(PageTable));
	}
	
	u.u_procp = current;
	u.u_MemoryDescriptor.m_UserPageTableArray = pgTable;
	return 0;
}

/* 在进程切换的过程中，根本没有用到TSS */
int ProcessManager::Swtch()
{
	User &u = Kernel::Instance().GetUser();
	SaveU(u.u_rsav);

	Process *procZero = &process[0];
	X86Assembly::CLI();
	SwtchUStruct(procZero);
	RetU();
	X86Assembly::STI();

	Process *selected = Select();

	X86Assembly::CLI();
	SwtchUStruct(selected);
	RetU();
	X86Assembly::STI();

	User &newu = Kernel::Instance().GetUser();
	newu.u_MemoryDescriptor.MapToPageTable();

	if (newu.u_procp->p_flag & Process::SSWAP)
	{
		newu.u_procp->p_flag &= ~Process::SSWAP;
		aRetU(newu.u_ssav);
	}

	return 1;
}

void ProcessManager::Sched()
{
	User &u = Kernel::Instance().GetUser();
	UserPageManager &pages = Kernel::Instance().GetUserPageManager();
	BufferManager &buffers = Kernel::Instance().GetBufferManager();
	SwapperManager &swapper = Kernel::Instance().GetSwapperManager();
	const unsigned int blocksPerPage = PageManager::PAGE_SIZE / SwapperManager::BLOCK_SIZE;

	for (;;)
	{
		X86Assembly::CLI();
		Process *selected = NULL;
		for (int index = 0; index < NPROC; index++)
		{
			Process *candidate = &process[index];
			if (candidate->p_stat == Process::SRUN &&
				(candidate->p_flag & (Process::SLOAD | Process::SLOCK)) == 0 &&
				(selected == NULL || candidate->p_time > selected->p_time))
				selected = candidate;
		}
		if (selected == NULL)
		{
			RunOut++;
			u.u_procp->Sleep((unsigned long)&RunOut, PSWP);
			continue;
		}
		X86Assembly::STI();

		PageTable *pageTable = GetProcessPageTable(selected);
		unsigned long requiredPages = 1;
		for (unsigned int index = 0; index < PageTable::ENTRY_CNT_PER_PAGETABLE; index++)
			if (pageTable->m_Entrys[index].m_ForSystemUser == 1)
				requiredPages++;
		if (selected->p_textp != NULL && selected->p_textp->x_ccount == 0)
			requiredPages += (selected->p_textp->x_size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;

		if (!EnsureUserPages(requiredPages))
		{
			RunIn++;
			u.u_procp->Sleep((unsigned long)&RunIn, PSWP);
			continue;
		}

		selected->p_flag |= Process::SLOCK;
		unsigned long ppda = pages.AllocPage() << 12;
		if (ppda == 0)
			Utility::Panic("No page for swapped process");
		int swapBlock = selected->p_addr;
		if (!buffers.Swap(swapBlock, ppda, PageManager::PAGE_SIZE, Buf::B_READ))
			Utility::Panic("Swap read error");
		MemoryDescriptor descriptor = ReadMemoryDescriptor(ppda);
		unsigned int diskBlock = swapBlock + blocksPerPage;
		for (unsigned int index = 0; index < PageTable::ENTRY_CNT_PER_PAGETABLE; index++)
		{
			PageTableEntry &entry = pageTable->m_Entrys[index];
			if (entry.m_ForSystemUser != 1)
				continue;
			unsigned long frame = pages.AllocPage();
			if (frame == 0 || !buffers.Swap(diskBlock, frame << 12, PageManager::PAGE_SIZE, Buf::B_READ))
				Utility::Panic("Swap page read error");
			entry.m_PageBaseAddress = frame;
			entry.m_Present = 1;
			entry.m_ReadWriter = 1;
			entry.m_ForSystemUser = 0;
			diskBlock += blocksPerPage;
		}
		if (selected->p_textp != NULL)
		{
			Text *text = selected->p_textp;
			unsigned int textPages = (text->x_size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
			unsigned int firstIndex = (descriptor.m_TextStartAddress >> 12) - PageTable::ENTRY_CNT_PER_PAGETABLE;
			for (unsigned int index = 0; index < textPages; index++)
			{
				if (text->x_ccount == 0)
				{
					text->x_caddr[index] = pages.AllocPage();
					if (text->x_caddr[index] == 0 || !buffers.Swap(text->x_daddr + index * blocksPerPage,
						text->x_caddr[index] << 12, PageManager::PAGE_SIZE, Buf::B_READ))
						Utility::Panic("Text swap read error");
				}
				PageTableEntry &entry = pageTable->m_Entrys[firstIndex + index];
				entry.m_PageBaseAddress = text->x_caddr[index];
				entry.m_Present = 1;
				entry.m_ReadWriter = 0;
				entry.m_UserSupervisor = 1;
			}
			text->x_ccount++;
		}
		swapper.FreeSwap((diskBlock - swapBlock) * SwapperManager::BLOCK_SIZE, swapBlock);
		selected->p_addr = ppda;
		selected->p_flag = (selected->p_flag | Process::SLOAD) & ~Process::SLOCK;
		selected->p_time = 0;
		Diagnose::Write("Swap in PID %d: %d pages\n", selected->p_pid, requiredPages);
	}
}

bool ProcessManager::EnsureUserPages(unsigned long requiredPages)
{
	UserPageManager &pages = Kernel::Instance().GetUserPageManager();
	if (pages.AvailablePages() >= requiredPages)
		return true;

	Process *current = Kernel::Instance().GetUser().u_procp;
	for (int priority = 0; priority < 3; priority++)
	{
		for (int index = 0; index < NPROC; index++)
		{
			Process *candidate = &process[index];
			if (candidate == current ||
				(candidate->p_flag & (Process::SSYS | Process::SLOCK | Process::SLOAD)) != Process::SLOAD)
				continue;
			bool waiting = candidate->p_stat == Process::SWAIT || candidate->p_stat == Process::SSTOP;
			bool sleeping = candidate->p_stat == Process::SSLEEP;
			if ((priority == 0 && !waiting) || (priority == 1 && !sleeping) ||
				(priority == 2 && candidate->p_stat != Process::SRUN))
				continue;
			if (XSwap(candidate) && pages.AvailablePages() >= requiredPages)
				return true;
		}
	}
	return pages.AvailablePages() >= requiredPages;
}

void ProcessManager::Wait()
{
	int i;
	bool hasChild = false;
	User &u = Kernel::Instance().GetUser();
	SwapperManager &swapperMgr = Kernel::Instance().GetSwapperManager();
	BufferManager &bufMgr = Kernel::Instance().GetBufferManager();

	Diagnose::Write("Process %d finding dead son. They are ", u.u_procp->p_pid);
	while (true)
	{
		for (i = 0; i < NPROC; i++)
		{
			if (u.u_procp->p_pid == process[i].p_ppid)
			{
				Diagnose::Write("Process %d (Status:%d)  ", process[i].p_pid, process[i].p_stat);
				hasChild = true;
				/* 睡眠等待直至子进程结束 */
				if (Process::SZOMB == process[i].p_stat)
				{
					/* wait()系统调用返回子进程的pid */
					u.u_ar0[User::EAX] = process[i].p_pid;

					process[i].p_stat = Process::SNULL;
					process[i].p_pid = 0;
					process[i].p_ppid = -1;
					process[i].p_sig = 0;
					process[i].p_flag = 0;

					/* 读入swapper中子进程u结构副本 */
					Buf *pBuf = bufMgr.Bread(DeviceManager::ROOTDEV, process[i].p_addr);
					swapperMgr.FreeSwap(BufferManager::BUFFER_SIZE, process[i].p_addr);
					User *pUser = (User *)pBuf->b_addr;

					/* 把子进程的时间加到父进程上 */
					u.u_cstime += pUser->u_cstime + pUser->u_stime;
					u.u_cutime += pUser->u_cutime + pUser->u_utime;

					int *pInt = (int *)u.u_arg[0];
					/* 获取子进程exit(int status)的返回值 */
					*pInt = pUser->u_arg[0];

					/* 如果此处没有Brelse()系统会发生什么-_- */
					bufMgr.Brelse(pBuf);
					Diagnose::Write("end wait\n");
					return;
				}
			}
		}
		if (true == hasChild)
		{
			/* 睡眠等待直至子进程结束 */
			Diagnose::Write("wait until child process Exit! ");
			u.u_procp->Sleep((unsigned long)u.u_procp, ProcessManager::PWAIT);
			Diagnose::Write("end sleep\n");
			continue; /* 回到外层while(true)循环 */
		}
		else
		{
			/* 不存在需要等待结束的子进程，设置出错码，wait()返回 */
			u.u_error = User::ECHILD;
			break; /* Get out of while loop */
		}
	}
}

void ProcessManager::Fork()
{
	User &u = Kernel::Instance().GetUser();
	Process *child = NULL;

	/* 寻找空闲的process项，作为子进程的进程控制块 */
	for (int i = 0; i < ProcessManager::NPROC; i++)
	{
		if (this->process[i].p_stat == Process::SNULL)
		{
			child = &this->process[i];
			break;
		}
	}
	if (child == NULL)
	{
		/* 没有空闲process表项，返回 */
		u.u_error = User::EAGAIN;
		return;
	}

	if (this->NewProc()) /* 子进程返回1，父进程返回0 */
	{
		/* 子进程fork()系统调用返回0 */
		u.u_ar0[User::EAX] = 0;
		u.u_cstime = 0;
		u.u_stime = 0;
		u.u_cutime = 0;
		u.u_utime = 0;
	}
	else
	{
		/* 父进程进程fork()系统调用返回子进程PID */
		u.u_ar0[User::EAX] = child->p_pid;
	}

	return;
}

extern "C" void runtime();
extern "C" void ExecShell();

void ProcessManager::Exec()
{
	Inode *pInode;
	Text *pText;
	User &u = Kernel::Instance().GetUser();
	FileManager &fileMgr = Kernel::Instance().GetFileManager();
	UserPageManager &userPgMgr = Kernel::Instance().GetUserPageManager();
	KernelPageManager &kernelPgMgr = Kernel::Instance().GetKernelPageManager();
	BufferManager &bufMgr = Kernel::Instance().GetBufferManager();

	Diagnose::Write("Process %d execing\n", u.u_procp->p_pid);
	pInode = fileMgr.NameI(FileManager::NextChar, FileManager::OPEN);
	if (NULL == pInode) // 搜索目录失败
	{
		return;
	}

	/* 如果同时进行图像改换的进程数超出限制，则先进入睡眠 */
	while (this->ExeCnt >= NEXEC)
	{
		u.u_procp->Sleep((unsigned long)&ExeCnt, ProcessManager::EXPRI);
	}
	this->ExeCnt++;

	/* 进程必需拥有可执行文件的执行权限，且被执行的只能是一般文件。 */
	if (fileMgr.Access(pInode, Inode::IEXEC) || (pInode->i_mode & Inode::IFMT) != 0)
	{
		fileMgr.m_InodeTable->IPut(pInode);
		if (this->ExeCnt >= NEXEC)
		{
			WakeUpAll((unsigned long)&ExeCnt);
		}
		this->ExeCnt--;
		return;
	}

	PEParser parser;

	if (parser.HeaderLoad(pInode) == false)
	{
		fileMgr.m_InodeTable->IPut(pInode);
		return;
	}

	if (parser.TextSize + parser.DataSize + parser.StackSize + PageManager::PAGE_SIZE > MemoryDescriptor::USER_SPACE_SIZE - parser.TextAddress)
	{
		fileMgr.m_InodeTable->IPut(pInode);
		u.u_error = User::ENOMEM;
		return;
	}

	X86Assembly::CLI();    //////////原子操作，接纳新程序的命令行参数//////////////////////////////////////////////////////////////////////////////////////////////
	int pages = (parser.StackSize + PageManager::PAGE_SIZE - 1) >> 12;
	int allocLength = pages << 12;   // 用户栈按页对齐

	// PageTableEntry *tempEntrys = (PageTableEntry *)Machine::Instance().GetUserPageTableArray();  // 借用0#用户页表，从1#开始的连续PTE，建立用户栈的虚实地址映射关系
	PageTableEntry *tempEntrys = (PageTableEntry *)0xC0202000;
	for (int i = 1; i <= pages; i++)
	{
		tempEntrys[i].m_UserSupervisor = 0x1;
		tempEntrys[i].m_Present = 0x1;
		tempEntrys[i].m_ReadWriter = true;
		tempEntrys[i].m_PageBaseAddress = userPgMgr.AllocPage();
	}
	// trueStack -= pages;
	u.u_MemoryDescriptor.MapToPageTable();

	int argc = u.u_arg[1];  // 接纳命令行参数
	char **argv = (char **)u.u_arg[2];

	/* esp定位到栈底 */
	unsigned int esp = MemoryDescriptor::USER_SPACE_SIZE;
	unsigned long desAddress = 4096 + allocLength;

	int length;

	/* 复制argv[]指针数组指向的命令行参数字符串 */
	for (int i = 0; i < argc; i++)
	{
		length = 0;
		/* 计算参数字符串长度，length不含'\0' */
		while (NULL != argv[i][length])
		{
			length++;
		}
		desAddress = desAddress - (length + 1);
		/* 拷贝时将'\0'一起拷贝过去 */
		Utility::MemCopy((unsigned long)argv[i], desAddress, length + 1);
		/* 将参数字符串在新进程图像用户栈中的起始位置存入argv[i]，用户栈位于进程逻辑地址空间0x800000的底部 */
		esp = esp - (length + 1);
		argv[i] = (char *)esp;
	}

	/* 后续存放的是int型数值，这里以16字节边界对齐 */
	desAddress = desAddress & 0xFFFFFFF0;
	esp = esp & 0xFFFFFFF0;

	/* 复制argc和argv[] */
	int endValue = 0;
	desAddress -= sizeof(endValue);
	esp -= sizeof(endValue);
	/* 向用户栈中写入endValue作为argv[]的结束 */
	Utility::MemCopy((unsigned long)&endValue, desAddress, sizeof(endValue));

	desAddress -= argc * sizeof(int);
	esp -= argc * sizeof(int);
	/* 写入argv[]的内容 */
	Utility::MemCopy((unsigned long)argv, desAddress, argc * sizeof(int));

	/* 令endValue指向当前栈中argv[]的起始地址，即argv[]入栈完毕后当前栈顶地址 */
	endValue = esp;
	desAddress -= sizeof(int);
	esp -= sizeof(int);
	Utility::MemCopy((unsigned long)&endValue, desAddress, sizeof(int));

	/* 最后入栈argc */
	desAddress -= sizeof(int);
	esp -= sizeof(int);
	Utility::MemCopy((unsigned long)&argc, desAddress, sizeof(int)); /* Done! */

	// u.u_MemoryDescriptor.MapToPageTable();
	X86Assembly::STI();     ///////////////////////////////////////////////////////////////////////////////////////////////////////////

	Process *current = u.u_procp;
	PageTable *pUserPageTable = u.u_MemoryDescriptor.m_UserPageTableArray;
	MemoryDescriptor &md = u.u_MemoryDescriptor;

	/* 释放原进程图像的共享正文段，数据段，堆栈段 */
	if (u.u_procp->p_textp != NULL)
	{
		// 释放代码段
		u.u_procp->p_textp->XFree();
		u.u_procp->p_textp = NULL;

		// 释放数据段
		int index = (md.m_DataStartAddress >> 12) - 1024;
		int count = (md.m_DataSize + PageManager::PAGE_SIZE - 1) >> 12;
		unsigned long frame;

		while (count)
		{
		frame = pUserPageTable->m_Entrys[index].m_PageBaseAddress;  // 释放物理页框
		userPgMgr.FreePage(frame);

		pUserPageTable->m_Entrys[index].m_Present = 0;				// 清空PTE
		pUserPageTable->m_Entrys[index].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[index].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[index].m_PageBaseAddress = 0;

		index++; 		count--;
		}

		// 释放栈段
		index = ((md.USER_SPACE_START_ADDRESS + md.USER_SPACE_SIZE - md.m_StackSize) >> 12) - 1024;
		count = (md.m_StackSize + PageManager::PAGE_SIZE - 1) >> 12;
		while (count)
		{
			frame = pUserPageTable->m_Entrys[index].m_PageBaseAddress;  // 释放物理页框
			userPgMgr.FreePage(frame);

			pUserPageTable->m_Entrys[index].m_Present = 0;				// 清空PTE
			pUserPageTable->m_Entrys[index].m_ReadWriter = 0;
			pUserPageTable->m_Entrys[index].m_UserSupervisor = 1;
			pUserPageTable->m_Entrys[index].m_PageBaseAddress = 0;
			index++; 		count--;
		}
	}

	/* 从PE头中获取新应用程序，正文段、数据段、堆栈段的起始地址、长度 */
	u.u_MemoryDescriptor.m_TextStartAddress = parser.TextAddress;
	u.u_MemoryDescriptor.m_TextSize = parser.TextSize;
	u.u_MemoryDescriptor.m_DataStartAddress = parser.DataAddress;
	u.u_MemoryDescriptor.m_DataSize = parser.DataSize;
	u.u_MemoryDescriptor.m_StackSize = parser.StackSize;

	pText = NULL;
	/* 分配一个空闲Text结构，或者和其它进程共享同一正文段 */
	for (int i = 0; i < ProcessManager::NTEXT; i++)
	{
		if (NULL == this->text[i].x_iptr) /* 记下找到的第一个空闲text结构 */
		{
			if (NULL == pText)
			{
				pText = &(this->text[i]);
			}
		}
		else if (pInode == this->text[i].x_iptr) /* 如果，这不是一个空闲text结构，看一下text结构指向的可执行文件是exec系统调用要执行的应用程序吗？ */
		{
			if (this->text[i].x_ccount == 0)
			{
				unsigned int textPages = (this->text[i].x_size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
				for (unsigned int index = 0; index < textPages; index++)
				{
					unsigned long frame = userPgMgr.AllocPage();
					if (frame == 0 || !bufMgr.Swap(this->text[i].x_daddr +
						index * (PageManager::PAGE_SIZE / SwapperManager::BLOCK_SIZE),
						frame << 12, PageManager::PAGE_SIZE, Buf::B_READ))
						Utility::Panic("Shared text swap read error");
					this->text[i].x_caddr[index] = frame;
				}
			}
			this->text[i].x_count++;
			this->text[i].x_ccount++;
			u.u_procp->p_textp = &(this->text[i]);
			pText = NULL; /* 与其它进程共享同一正文段，则pText重新清零，否则指向一空闲Text结构 */
			break;
		}
	}

	int sharedText = 0;

	PageTableEntry *entrys = (PageTableEntry *)md.m_UserPageTableArray;   // 1# 用户页表，hardcode Unix V6++
	pages = (u.u_MemoryDescriptor.m_TextSize + 4096 - 1) / 4096;    // 正文段页数
	int text_page_start = parser.TextAddress >> 12;  // 代码段第1页的逻辑页号
	text_page_start -= PageTable::ENTRY_CNT_PER_PAGETABLE;  // 在1# 用户页表中的下标，hardcode Unix V6++


	/* 无共享Text结构，为正文段分配text结构、物理页框、建立地址映射关系 */
	if (NULL != pText)
	{
		pInode->i_count++;

		pText->x_ccount = 1;
		pText->x_count = 1;
		pText->x_iptr = pInode;
		pText->x_size = u.u_MemoryDescriptor.m_TextSize;

		for (int i = 0; i < pages; i++)
		{
			entrys[text_page_start + i].m_UserSupervisor = 0x1;
			entrys[text_page_start + i].m_Present = 0x1;
			entrys[text_page_start + i].m_ReadWriter = false;
			entrys[text_page_start + i].m_PageBaseAddress = pText->x_caddr[i] = userPgMgr.AllocPage();
		}

		pText->x_daddr = Kernel::Instance().GetSwapperManager().AllocSwap(pages * PageManager::PAGE_SIZE);

		u.u_procp->p_textp = pText;  // 建立Process结构和Text结构的勾连关系
	}
	else
	{
		pText = u.u_procp->p_textp;
		for (int i = 0; i < pages; i++)  // 写PTE
		{
			entrys[text_page_start + i].m_UserSupervisor = 0x1;
			entrys[text_page_start + i].m_Present = 0x1;
			entrys[text_page_start + i].m_ReadWriter = false;
			entrys[text_page_start + i].m_PageBaseAddress = pText->x_caddr[i];
		}
		sharedText = 1;
	}

	// 进程图像需要的物理空间尺寸：          PPDA + 数据 + 堆栈。没有考虑内存不足的情况。
	unsigned int newSize = ProcessManager::USIZE +
		((u.u_MemoryDescriptor.m_DataSize + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE +
		(u.u_MemoryDescriptor.m_StackSize + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE) * PageManager::PAGE_SIZE;
	u.u_procp->p_size = newSize;

	// 数据段
	pages = (u.u_MemoryDescriptor.m_DataSize + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;  // 数据段页数
	int data_page_start = parser.DataAddress >> 12;
	data_page_start -= PageTable::ENTRY_CNT_PER_PAGETABLE;

	for (int i = 0; i < pages; i++)
	{
		entrys[data_page_start + i].m_UserSupervisor = 0x1;
		entrys[data_page_start + i].m_Present = 0x1;
		entrys[data_page_start + i].m_ReadWriter = true;
		entrys[data_page_start + i].m_PageBaseAddress = userPgMgr.AllocPage();
	}

	// 用户栈
	pages = (u.u_MemoryDescriptor.m_StackSize + PageManager::PAGE_SIZE - 1) >> 12;
	unsigned long start = md.USER_SPACE_SIZE - allocLength;
	int stack_page = (start >> 12) - PageTable::ENTRY_CNT_PER_PAGETABLE;

	for (int i = 0,j = 1; i < pages; i++,j++)  // 写PTE
	{
		entrys[stack_page + i].m_UserSupervisor = 0x1;
		entrys[stack_page + i].m_Present = 0x1;
		entrys[stack_page + i].m_ReadWriter = true;
		entrys[stack_page + i].m_PageBaseAddress = tempEntrys[j].m_PageBaseAddress;

		tempEntrys[j].m_Present = 0;
		tempEntrys[j].m_PageBaseAddress = j;
	}

	md.MapToPageTable();

	/* 从exe文件中依次读入.text段、.data段、.rdata段、.bss段 */
	parser.Relocate(pInode, sharedText);

	/* .text段在swap分区上留复本 */
	if (sharedText == 0)
	{
		u.u_procp->p_flag |= Process::SLOCK;
		int textPages = (pText->x_size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
		for (int index = 0; index < textPages; index++)
			if (!bufMgr.Swap(pText->x_daddr + index * (PageManager::PAGE_SIZE / SwapperManager::BLOCK_SIZE),
				pText->x_caddr[index] << 12, PageManager::PAGE_SIZE, Buf::B_WRITE))
				Utility::Panic("Text swap write error");
		u.u_procp->p_flag &= ~Process::SLOCK;
	}

	/* 释放Inode，减少ExeCnt计数值 */
	fileMgr.m_InodeTable->IPut(pInode);
	if (this->ExeCnt >= NEXEC)
	{
		WakeUpAll((unsigned long)&ExeCnt);
	}
	this->ExeCnt--;

	/* 用默认的方式处理信号  */
	for (int i = 0; i < u.NSIG; i++)
	{
		u.u_signal[i] = 0;
	}

	/* 清0所有通用寄存器  */
	for (int i = User::EAX - 4; i < User::EAX - 4 * 7; i = i - 4)
	{
		u.u_ar0[i] = 0; /* 下标写成  User::EAX + i 可读性要强一些，但是运算速度慢了。就小抠，追求速度吧 */
	}

	/* 将exe程序的入口地址放入核心栈现场保护区中的EAX作为系统调用返回值，这个是runtime要用  */
	u.u_ar0[User::EAX] = parser.EntryPointAddress;

	/* 构造出Exec()系统调用的退出环境，使之退出到ring3时，开始执行user code */
	struct pt_context *pContext = (struct pt_context *)u.u_arg[4];
	pContext->eip = 0x00000000; /* 退出到ring3特权级下从线性地址0x00000000处runtime()开始执行 */
	// pContext->eip = parser.EntryPointAddress;
	pContext->xcs = Machine::USER_CODE_SEGMENT_SELECTOR;
	pContext->eflags = 0x200; /* 此项是否篡改无关紧要 */
	pContext->esp = esp;
	pContext->xss = Machine::USER_DATA_SEGMENT_SELECTOR;
}


Process *ProcessManager::Select()
{
	/* 前一次选中上台进程 */
	static int lastSelect = 0;

	while (true)
	{
		int priority = 256;
		int best = -1; /* 本轮搜索找到的最合适上台进程 */

		this->RunRun = 0;

		/* 搜索优先级最高的可运行进程 */
		for (int count = 0; count < NPROC; count++)
		{
			/* 从上一次被选中进程的下一个开始回环扫描，而不是每次从0#进程开始，保证各进程机会均等 */
			int i = (lastSelect + 1 + count) % NPROC;
			if (Process::SRUN == process[i].p_stat && (process[i].p_flag & Process::SLOAD) != 0)
			{
				if (process[i].p_pri < priority)
				{
					best = i;
					priority = process[i].p_pri;
				}
			}
		}
		if (-1 == best)
		{
			__asm__ __volatile__("hlt");
			continue;
		}

		SwtchNum++;
		if (SwtchNum & 0x80000000)
		{
			SwtchNum = 0; /* 计数溢出变为负数后，重置为零 */
		}
		/* 如果选出优先级最高的可运行进程 */
		this->CurPri = priority;
		lastSelect = best;
		// Diagnose::Write("Process %d is running!",best);
		return &process[best];
	}
}

void ProcessManager::Kill()
{
	User &u = Kernel::Instance().GetUser();
	int pid = u.u_arg[0];
	int signal = u.u_arg[1];
	bool flag = false;

	for (int i = 0; i < ProcessManager::NPROC; i++)
	{
		/* 不允许发送信号给进程自身 */
		if (u.u_procp == &process[i])
		{
			continue;
		}
		/* 不是信号的接收方目标进程，继续搜寻 */
		if (pid != 0 && process[i].p_pid != pid)
		{
			continue;
		}
		/* pid为0，则将信号发送至与发送进程同一终端的所有进程，0#进程不包括在内 */
		if (pid == 0 && (process[i].p_ttyp != u.u_procp->p_ttyp || i == 0))
		{
			continue;
		}
		/* 除非是超级用户，否则要求发送、接收进程u.uid相同，即不可给其它用户进程发送信号 */
		if (u.u_uid != 0 && u.u_uid != process[i].p_uid)
		{
			continue;
		}
		flag = true;
		/* 信号发送给满足条件的目标进程 */
		process[i].PSignal(signal);
	}
	if (false == flag)
	{
		u.u_error = User::ESRCH;
	}
}

void ProcessManager::WakeUpAll(unsigned long chan)
{
	/* 唤醒系统中所有因chan而进入睡眠的进程 */
	for (int i = 0; i < ProcessManager::NPROC; i++)
	{
		if (this->process[i].IsSleepOn(chan))
		{
			this->process[i].SetRun();
		}
	}
}

bool ProcessManager::XSwap(Process *pProcess)
{
	PageTable *pageTable = GetProcessPageTable(pProcess);
	MemoryDescriptor descriptor = ReadMemoryDescriptor(pProcess->p_addr);
	UserPageManager &pages = Kernel::Instance().GetUserPageManager();
	BufferManager &buffers = Kernel::Instance().GetBufferManager();
	const unsigned int blocksPerPage = PageManager::PAGE_SIZE / SwapperManager::BLOCK_SIZE;
	unsigned int privatePages = 0;
	for (unsigned int index = 0; index < PageTable::ENTRY_CNT_PER_PAGETABLE; index++)
		if (IsPrivatePage(descriptor, index) && pageTable->m_Entrys[index].m_Present)
			privatePages++;

	int block = Kernel::Instance().GetSwapperManager().AllocSwap((privatePages + 1) * PageManager::PAGE_SIZE);
	if (block == 0)
		return false;
	pProcess->p_flag = (pProcess->p_flag | Process::SLOCK) & ~Process::SLOAD;
	if (!buffers.Swap(block, pProcess->p_addr, PageManager::PAGE_SIZE, Buf::B_WRITE))
		Utility::Panic("Swap I/O Error");
	unsigned int diskBlock = block + blocksPerPage;
	for (unsigned int index = 0; index < PageTable::ENTRY_CNT_PER_PAGETABLE; index++)
	{
		PageTableEntry &entry = pageTable->m_Entrys[index];
		if (!IsPrivatePage(descriptor, index) || !entry.m_Present)
			continue;
		if (!buffers.Swap(diskBlock, entry.m_PageBaseAddress << 12, PageManager::PAGE_SIZE, Buf::B_WRITE))
			Utility::Panic("Swap page write error");
		pages.FreePage(entry.m_PageBaseAddress);
		entry.m_PageBaseAddress = 0;
		entry.m_Present = 0;
		entry.m_ForSystemUser = 1;
		diskBlock += blocksPerPage;
	}
	if (pProcess->p_textp != NULL)
		pProcess->p_textp->XccDec(pageTable, descriptor);
	pages.FreePage(pProcess->p_addr >> 12);
	pProcess->p_addr = block;
	pProcess->p_flag &= ~(Process::SLOAD | Process::SLOCK);
	/* 最近一次被换入或换出以来，在内出或交换区驻留的时间长度清零 */
	pProcess->p_time = 0;
	Diagnose::Write("Swap out PID %d: %d pages\n", pProcess->p_pid, privatePages + 1);

	if (this->RunOut)
	{
		this->RunOut = 0;
		Kernel::Instance().GetProcessManager().WakeUpAll((unsigned long)&RunOut);
	}
	return true;
}

void ProcessManager::Signal(TTy *pTTy, int signal)
{
	for (int i = 0; i < ProcessManager::NPROC; i++)
	{
		if (this->process[i].p_ttyp == pTTy)
		{
			this->process[i].PSignal(signal);
		}
	}
}
