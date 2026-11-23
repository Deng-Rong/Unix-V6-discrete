#include "Process.h"
#include "ProcessManager.h"
#include "Kernel.h"
#include "Utility.h"
#include "Machine.h"
#include "Video.h"

Process::Process()
{
	/* 标识所有p_stat为SNULL，标识该进程项可以使用 */
	this->p_stat = SNULL;
	/* 避免0#进程在Wait()时，许多空闲process项以0#进程为父进程 */
	this->p_ppid = -1;
}

Process::~Process()
{
}

// ------------- NOTE 3: 获取页目录的物理地址（现在记录的是逻辑地址） -------------
unsigned long Process::GetPageDirectoryPhyAddr()
{
	return (unsigned long)(this->pPageDirectory) - Machine::KERNEL_SPACE_START_ADDRESS;
}
// ------------- END NOTE 3 -------------

void Process::SetRun()
{
	ProcessManager &procMgr = Kernel::Instance().GetProcessManager();

	/* 清除睡眠原因，转为就绪状态 */
	this->p_wchan = 0;
	this->p_stat = Process::SRUN;
	if (this->p_pri < procMgr.CurPri)
	{
		procMgr.RunRun++;
	}
	if (0 != procMgr.RunOut && (this->p_flag & Process::SLOAD) == 0)
	{
		procMgr.RunOut = 0;
		procMgr.WakeUpAll((unsigned long)&procMgr.RunOut);
	}
}

void Process::SetPri()
{
	int priority;
	ProcessManager &procMgr = Kernel::Instance().GetProcessManager();

	priority = this->p_cpu / 16;
	priority += ProcessManager::PUSER + this->p_nice;

	if (priority > 255)
	{
		priority = 255;
	}
	if (priority > procMgr.CurPri)
	{
		procMgr.RunRun++;
	}
	this->p_pri = priority;
}

bool Process::IsSleepOn(unsigned long chan)
{
	/* 检查当前进程睡眠原因是否为chan */
	if (this->p_wchan == chan && (this->p_stat == Process::SWAIT || this->p_stat == Process::SSLEEP))
	{
		return true;
	}
	return false;
}

void Process::Sleep(unsigned long chan, int pri)
{
	User &u = Kernel::Instance().GetUser();
	ProcessManager &procMgr = Kernel::Instance().GetProcessManager();

	if (pri > 0)
	{
		/*
		 * 进程在进入低优先权睡眠之前，以及被唤醒之后，如果接收到不可忽略
		 * 的信号，则停止执行Sleep()，通过aRetU()直接跳转回Trap1()函数
		 */
		if (this->IsSig())
		{
			/* return确保aRetU()跳回到SystemCall::Trap1()之后立刻执行ret返回指令 */
			aRetU(u.u_qsav);
			return;
		}
		/*
		 * 此处关中断进入临界区，保证进程在设置睡眠原因chan和
		 * 改进程状态为SSLEEP之间不会发生切换。
		 */
		X86Assembly::CLI();
		this->p_wchan = chan;
		/* 根据睡眠优先级pri确定进程进入高、低优先权睡眠 */
		this->p_stat = Process::SWAIT;
		this->p_pri = pri;
		X86Assembly::STI();

		if (procMgr.RunIn != 0)
		{
			procMgr.RunIn = 0;
			procMgr.WakeUpAll((unsigned long)&procMgr.RunIn);
		}
		/* 当前进程放弃CPU，切换其它进程上台 */
		// Diagnose::Write("Process %d Start Sleep!\n", this->p_pid);
		Kernel::Instance().GetProcessManager().Swtch();
		// Diagnose::Write("Process %d End Sleep!\n", this->p_pid);
		/* 被唤醒之后再次检查信号 */
		if (this->IsSig())
		{
			/* return确保aRetU()跳回到SystemCall::Trap1()之后立刻执行ret返回指令 */
			aRetU(u.u_qsav);
			return;
		}
	}
	else
	{
		X86Assembly::CLI();
		this->p_wchan = chan;
		/* 根据睡眠优先级pri确定进程进入高、低优先权睡眠 */
		this->p_stat = Process::SSLEEP;
		this->p_pri = pri;
		X86Assembly::STI();

		/* 当前进程放弃CPU，切换其它进程上台 */
		// Diagnose::Write("Process %d Start Sleep!\n", this->p_pid);
		Kernel::Instance().GetProcessManager().Swtch();
		// Diagnose::Write("Process %d End Sleep!\n", this->p_pid);
	}
}

void Process::Exit()
{
	int i;
	User &u = Kernel::Instance().GetUser();
	ProcessManager &procMgr = Kernel::Instance().GetProcessManager();
	OpenFileTable &fileTable = *Kernel::Instance().GetFileManager().m_OpenFileTable;
	InodeTable &inodeTable = *Kernel::Instance().GetFileManager().m_InodeTable;

	Diagnose::Write("Process %d is exiting\n", u.u_procp->p_pid);
	/* Reset Tracing flag */
	u.u_procp->p_flag &= (~Process::STRC);


	for (i = 0; i < User::NSIG; i++)
	{
		u.u_signal[i] = 1;
	}


	for (i = 0; i < OpenFiles::NOFILES; i++)
	{
		File *pFile = NULL;
		if ((pFile = u.u_ofiles.GetF(i)) != NULL)
		{
			fileTable.CloseF(pFile);
			u.u_ofiles.SetF(i, NULL);
		}
	}

	u.u_error = User::NOERROR;


	inodeTable.IPut(u.u_cdir);


	if (u.u_procp->p_textp != NULL)
	{
		u.u_procp->p_textp->XFree();
		u.u_procp->p_textp = NULL;
	}


	SwapperManager &swapperMgr = Kernel::Instance().GetSwapperManager();
	BufferManager &bufMgr = Kernel::Instance().GetBufferManager();

	int blkno = swapperMgr.AllocSwap(BufferManager::BUFFER_SIZE);
	if (NULL == blkno)
	{
		Utility::Panic("Out of Swapper Space");
	}
	Buf *pBuf = bufMgr.GetBlk(DeviceManager::ROOTDEV, blkno);
	Utility::DWordCopy((int *)&u, (int *)pBuf->b_addr, BufferManager::BUFFER_SIZE / sizeof(int));
	bufMgr.Bwrite(pBuf);



	Process *current = u.u_procp;
	UserPageManager &userPageMgr = Kernel::Instance().GetUserPageManager();

	PageTable *pUserPageTable = u.u_MemoryDescriptor.m_UserPageTableArray;
	MemoryDescriptor &md = u.u_MemoryDescriptor;


	int index = (md.m_DataStartAddress >> 12) - PageTable::ENTRY_CNT_PER_PAGETABLE;
	int count = (md.m_DataSize + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
	unsigned long frame;

	while (count)
	{
		frame = pUserPageTable->m_Entrys[index].m_PageBaseAddress;
		userPageMgr.FreePage(frame);

		pUserPageTable->m_Entrys[index].m_Present = 0;
		pUserPageTable->m_Entrys[index].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[index].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[index].m_PageBaseAddress = 0;

		index++; 		count--;
	}


	index = ((md.USER_SPACE_START_ADDRESS + md.USER_SPACE_SIZE - md.m_StackSize) >> 12) - PageTable::ENTRY_CNT_PER_PAGETABLE;
	count = (md.m_StackSize + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
	while (count)
	{
		frame = pUserPageTable->m_Entrys[index].m_PageBaseAddress;
		userPageMgr.FreePage(frame);

		pUserPageTable->m_Entrys[index].m_Present = 0;
		pUserPageTable->m_Entrys[index].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[index].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[index].m_PageBaseAddress = 0;

		index++; 		count--;
	}

	u.u_MemoryDescriptor.Release();					// 释放1#用户页表
	userPageMgr.FreePage( current->p_addr >> 12);   // 释放PPDA区
	KernelPageManager &kernelPageManager = Kernel::Instance().GetKernelPageManager();
	frame = ((unsigned int)current->pPageDirectory - Machine::KERNEL_SPACE_START_ADDRESS )>>12;
	kernelPageManager.FreePage(frame);  			// 释放页目录

	current->p_addr = blkno;
	current->p_stat = Process::SZOMB;

	for (i = 0; i < ProcessManager::NPROC; i++)
	{
		if (procMgr.process[i].p_pid == current->p_ppid)
		{
			procMgr.WakeUpAll((unsigned long)&procMgr.process[i]);
			break;
		}
	}

	if (ProcessManager::NPROC == i)
	{
		current->p_ppid = 1;
		procMgr.WakeUpAll((unsigned long)&procMgr.process[1]);
	}


	for (i = 0; i < ProcessManager::NPROC; i++)
	{
		if (current->p_pid == procMgr.process[i].p_ppid)
		{
			Diagnose::Write("My:%d 's child %d passed to 1#process", current->p_pid, procMgr.process[i].p_pid);
			procMgr.process[i].p_ppid = 1;
			if (procMgr.process[i].p_stat == Process::SSTOP)
			{
				procMgr.process[i].SetRun();
			}
		}
	}

	procMgr.Swtch();
}

void Process::Clone(Process &proc)
{
	User &u = Kernel::Instance().GetUser();

	/* 拷贝父进程Process结构中的大部分数据 */
	proc.p_size = this->p_size;
	proc.p_stat = Process::SRUN;
	proc.p_flag = Process::SLOAD;
	proc.p_uid = this->p_uid;
	proc.p_ttyp = this->p_ttyp;
	proc.p_nice = this->p_nice;
	proc.p_textp = this->p_textp;

	/* 建立父子关系 */
	proc.p_pid = ProcessManager::NextUniquePid();
	proc.p_ppid = this->p_pid;

	/* 初始化进程调度相关成员 */
	proc.p_pri = 0; /* 确保child的优先数较小，与其它进程相比更有机会占用CPU */
	proc.p_time = 0;

	/* 打开文件控制块File结构引用计数+1 */
	for (int i = 0; i < OpenFiles::NOFILES; i++)
	{
		File *pFile;
		if ((pFile = u.u_ofiles.GetF(i)) != NULL)
		{
			pFile->f_count++;
		}
	}
	/*
	 * GetF()访问u.u_ofiles中的空闲项会产生出错码，
	 * 如不清除将导致进程创建(fork)系统调用失败。
	 */
	u.u_error = User::NOERROR;

	/* 增加对共享正文段的引用计数 */
	if (proc.p_textp != 0)
	{
		proc.p_textp->x_count++;
		proc.p_textp->x_ccount++;
	}

	/* 增加对当前工作目录的引用计数 */
	u.u_cdir->i_count++;
}

// 用于堆栈溢出时，自动扩展堆栈
void Process::SStack()
{
	// 获取内存管理对象
	User &u = Kernel::Instance().GetUser();
	UserPageManager &userPgMgr = Kernel::Instance().GetUserPageManager();
	MemoryDescriptor &md = u.u_MemoryDescriptor;
	PageTableEntry *entrys = (PageTableEntry *)md.m_UserPageTableArray;

	unsigned int index = 1024 - ((md.m_StackSize + PageManager::PAGE_SIZE - 1) >> 12) - 1;

	if (md.m_TextSize + md.m_DataSize + md.m_StackSize + 2 * PageManager::PAGE_SIZE > md.USER_SPACE_SIZE - md.m_TextStartAddress)
	{
		u.u_error = User::ENOMEM;
		return;
	}

	unsigned int phyFrame = userPgMgr.AllocPage();
	if (phyFrame == 0)
	{
		u.u_error = User::ENOMEM;
		return;
	}

	/* 内存分配成功，写PTE */
	entrys[index].m_Present = 0x1;
	entrys[index].m_ReadWriter = true;
	entrys[index].m_UserSupervisor = 0x1;
	entrys[index].m_PageBaseAddress = phyFrame;

	md.m_StackSize += PageManager::PAGE_SIZE;
	p_size += PageManager::PAGE_SIZE;
	u.u_error = User::NOERROR;
	md.MapToPageTable();
}

void Process::SBreak()
{
	User &u = Kernel::Instance().GetUser();
	MemoryDescriptor &md = u.u_MemoryDescriptor;
	UserPageManager &userPgMgr = Kernel::Instance().GetUserPageManager();

	unsigned int m_brk = md.m_DataStartAddress + md.m_DataSize;  // 原来的进程图像，堆空间之后的第一个字节地址
	unsigned int new_brk = u.u_arg[0];    // 新数据段尾地址
	if (new_brk == 0)  // 获取当前堆的尾部地址
	{
		u.u_ar0[User::EAX] = m_brk;
		return;
	}


	PageTableEntry *entrys = (PageTableEntry *)md.m_UserPageTableArray;
	if (new_brk < md.m_DataStartAddress ||
		new_brk > md.USER_SPACE_SIZE - md.m_StackSize - PageManager::PAGE_SIZE)
	{
		u.u_error = User::ENOMEM;
		return;
	}
	unsigned int oldEndPage = (m_brk + PageManager::PAGE_SIZE - 1) >> 12;
	unsigned int newEndPage = (new_brk + PageManager::PAGE_SIZE - 1) >> 12;
	if (newEndPage > oldEndPage &&
		!Kernel::Instance().GetProcessManager().EnsureUserPages(newEndPage - oldEndPage))
	{
		u.u_error = User::ENOMEM;
		return;
	}
	for (unsigned int page = newEndPage; page < oldEndPage; page++)
	{
		PageTableEntry &entry = entrys[page - PageTable::ENTRY_CNT_PER_PAGETABLE];
		userPgMgr.FreePage(entry.m_PageBaseAddress);
		entry.m_Present = 0;
		entry.m_ReadWriter = 0;
		entry.m_PageBaseAddress = 0;
	}
	for (unsigned int page = oldEndPage; page < newEndPage; page++)
	{
		PageTableEntry &entry = entrys[page - PageTable::ENTRY_CNT_PER_PAGETABLE];
		entry.m_PageBaseAddress = userPgMgr.AllocPage();
		entry.m_Present = 1;
		entry.m_ReadWriter = 1;
		entry.m_UserSupervisor = 1;
	}

	md.MapToPageTable();
	md.m_DataSize = new_brk - md.m_DataStartAddress;
	if (newEndPage >= oldEndPage)
		p_size += (newEndPage - oldEndPage) * PageManager::PAGE_SIZE;
	else
		p_size -= (oldEndPage - newEndPage) * PageManager::PAGE_SIZE;
	u.u_ar0[User::EAX] = new_brk;
	return;
}

void Process::PSignal(int signal)
{
	if (signal >= User::NSIG)
	{
		return;
	}

	/* 如果已经接收到SIGKILL信号，则忽略后续信号 */
	if (this->p_sig != User::SIGKILL)
	{
		this->p_sig = signal;
	}
	/* 若进程的优先数大于PUSER(100)，则将其设置为PUSER */
	if (this->p_pri > ProcessManager::PUSER)
	{
		this->p_pri = ProcessManager::PUSER;
	}
	/* 若进程的处于低优先权睡眠，则将其唤醒 */
	if (this->p_stat == Process::SWAIT)
	{
		this->SetRun();
	}
}

int Process::IsSig()
{
	User &u = Kernel::Instance().GetUser();

	/* 未接受到信号 */
	if (this->p_sig == 0)
	{
		return 0;
	}
	/* u.u_signal[n]为偶数才表示对信号进程处理 */
	else if ((u.u_signal[this->p_sig] & 1) == 0)
	{
		return this->p_sig;
	}
	return 0;
}

/*
extern "C" void runtime();
extern "C" void SignalHandler();
*/

void Process::PSig(struct pt_context *pContext)
{
	User &u = Kernel::Instance().GetUser();
	int signal = this->p_sig;
	/* 清除已进入处理流程的信号 */
	this->p_sig = 0;

	if (u.u_signal[signal] != 0)
	{
		/* 清除进程在收到信号之前执行系统调用期间可能产生的ErrCode */
		u.u_error = User::NOERROR;

		unsigned int old_eip = pContext->eip;

		/* 核心态返回值为预定的用户函数SignalHandler()的首地址 */
		/*pContext->eip = ((unsigned long)SignalHandler - (unsigned long)runtime);
		pContext->esp -= 8;
		int* pInt = (int *)pContext->esp;
		*pInt = u.u_signal[signal];
		*(pInt + 1) = old_eip;*/
		pContext->eip = u.u_signal[signal];
		pContext->esp -= 4;
		int *pInt = (int *)pContext->esp;
		*pInt = old_eip;

		/*
		 * 当前信号处理函数在响应完本次信号之后，需要重置为默认
		 * 的信号处理函数，设为0表示对信号的处理方式为终止本进程。
		 */
		u.u_signal[signal] = 0;
		return;
	}

	/* u.u_signal[n]为0，则对信号的处理方式是终止本进程 */
	u.u_procp->Exit();
}

void Process::Nice()
{
	User &u = Kernel::Instance().GetUser();
	int niceValue = u.u_arg[0];

	if (niceValue > 20)
	{
		niceValue = 20;
	}
	if (niceValue < 0 && !u.SUser())
	{
		/* 非系统超级用户不能为进程设置小于0的进程优先数计算偏置值 */
		niceValue = 0;
	}
	this->p_nice = niceValue;
}

void Process::Ssig()
{
	User &u = Kernel::Instance().GetUser();

	int signalIndex = u.u_arg[0];
	unsigned long func = u.u_arg[1];

	/* 这几个信号不许设置 */
	if (signalIndex <= 0 || signalIndex >= User::NSIG || signalIndex == User::SIGKILL)
	{
		u.u_error = User::EINVAL;
		return;
	}
	/* 设置函数地址到信号处理函数数组 */
	u.u_ar0[User::EAX] = u.u_signal[signalIndex];
	u.u_signal[signalIndex] = func;
	/* 清当前信号 */
	if (u.u_procp->p_sig == signalIndex)
	{
		u.u_procp->p_sig = 0;
	}
}
