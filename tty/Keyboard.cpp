#include "Keyboard.h"
#include "IOPort.h"
#include "Kernel.h"
#include "ProcessManager.h"
#include "Video.h"
#include "CharDevice.h"
#include "CRT.h"

char Keyboard::Keymap[] = {
	0,0x1b,'1','2','3','4','5','6',		/* 0x00-0x07 0, <esc>,1,2,3,4,5,6, */
	'7','8','9','0','-','=',0x8,0x9,	/* 0x08-0x0f 7,8,9,0,-,=,<backspace><tab>*/
	'q','w','e','r','t','y','u','i',	/* 0x10-0x17 qwertyui*/
	'o','p','[',']','\n',0,'a','s', 	/* 0x18-0x1f op[] <enter><ctrl>as */
	'd','f','g','h','j','k','l',';',	/* 0x20-0x27 dfghjkl; */
	'\'','`',0,'\\','z','x','c','v',	/* 0x28-0x2f '`<lshift>\zdcv */
	'b','n','m',',','.','/',0,'*', 		/* 0x30-0x37 bnm,./<rshitf><printscr> */
	0,' ',0,0,0,0,0,0, 					/* 0x38-0x3f <alt><space><caps><f1><f2><f3><f4><f5> */
	0,0,0,0,0,0,0,'7', 					/* 0x40-0x47 <f0><><><><><numlock><scrlock>7*/
	'8','9','-','4','5','6','+','1',	/* 0x48-x04f 89-456+1  */
	'2','3','0','.',0,0,0,0				/* 0x50-0x57 230.<><><><>  */
};

char Keyboard::Shift_Keymap[] = {
	0,0x1b,'!','@','#','$','%','^',		/* 0x00-0x07 0, <esc>,!,@,#,$,%,^, */
	'&','*','(',')','_','+',0x8,0x9,	/* 0x08-0x0f ~,<backspace><tab>*/
	'q','w','e','r','t','y','u','i',	/* 0x10-0x17 qwertyui*/
	'o','p','{','}','\n',0,'a','s', 	/* 0x18-0x1f op[] <enter><ctrl>as */
	'd','f','g','h','j','k','l',':',	/* 0x20-0x27 dfghjkl; */
	'\"','~',0,'|','z','x','c','v',		/* 0x28-0x2f '`<lshift>\zdcv */
	'b','n','m','<','>','?',0,'*', 		/* 0x30-0x37 bnm,./<rshitf><printscr> */
	0,' ',0,0,0,0,0,0, 					/* 0x38-0x3f <alt><space><caps><f1><f2><f3><f4><f5> */
	0,0,0,0,0,0,0,0, 					/* 0x40-0x47 <f0><><><><><numlock><scrlock>7*/
	0,0,'-',0,0,0,'+',0,				/* 0x48-x04f 89-456+1  */
	0,0,0,0x7f/*del*/,0,0,0,0			/* 0x50-0x57 230.<><><><>  */
};

int Keyboard::Mode = 0;

/* 初始化时当前焦点窗口为用户终端窗口 */
Keyboard::ScrollFocus Keyboard::m_ScrollFocus = Keyboard::FOCUS_CRT;

void Keyboard::KeyboardHandler( struct pt_regs* reg, struct pt_context* context )
{
	/***********************************************/
	//for test
	//Diagnose::Write("key pressed !   ");
	/***********************************************/

	unsigned char status = IOPort::InByte(Keyboard::STATUS_PORT);
	int limit = 10;
	static int pre_state = 0;

	while ( (status & Keyboard::DATA_BUFFER_BUSY) && !(status & Keyboard::AUX_DATA) && limit-- )
	{
		/* 从键盘读取一个扫描码，需要处理扫描码的扩展扫描码 */
		unsigned char scancode = IOPort::InByte(Keyboard::DATA_PORT);

		/* 根据键盘中断的扫描码以及当前的键盘状态，结合最后采用新的地方没有效果 */
		if ( 0 == pre_state )
		{
			if ( 0xE0 == scancode || 0xE1 == scancode )
			{
				/* 新状态设为0xe0表示检测到扩展码没有读完 */
				/* 其中0xe1只有一种状态，就是暂停pause键进入，扫描码依次为0xe1,0x1d,0x45，松开放开依次为0xe1,0x9d,0xc5  */
				pre_state = scancode;
			}
			else	/* 普通扫描码 */
			{
				pre_state = 0;
				Keyboard::HandleScanCode(scancode, 0);
			}
		}
		else if ( 0xE0 == pre_state )
		{
			/* 扩展码的第二个扫描码 */
			pre_state = 0;
			Keyboard::HandleScanCode(scancode, 0xe0);
		}
		else if ( 0xE1 == pre_state && ( 0x1d == scancode || 0x9d == scancode ) )
		{
			pre_state = 0x100;	/* 临时状态，用于表示pause已经按了两个扫描码 */
		}
		else if ( pre_state == 0x100 && 0x45 == scancode )	/* 只需要知道pause是什么时候按下 */
		{
			pre_state = 0;
			Keyboard::HandleScanCode(scancode, 0xe1);
		}
		else
		{
			pre_state = 0;
		}
		status = IOPort::InByte(Keyboard::STATUS_PORT);
	}
}

void Keyboard::HandleScanCode(unsigned char scanCode, int expand)
{
	int isOK = 0;
	char ch = 0;

	if ( 0xE1 == expand )
	{
		ch = Keyboard::ScanCodeTranslate(scanCode, expand);
	}

	switch ( scanCode )
	{
	case SCAN_ALT:
		if ( 0xE0 == expand )	/* 使用扩展码来表示右边的 alt按键 */
			Mode |= M_RALT;
		else
			Mode |= M_LALT;
		break;

	case SCAN_CTRL:
		if ( 0xE0 == expand )	/* 使用扩展码来表示右边的 ctrl按键 */
			Mode |= M_RCTRL;
		else
			Mode |= M_LCTRL;
		break;

	case SCAN_LSHIFT:
		Mode |= M_LSHIFT;
		break;

	case SCAN_RSHIFT:
		Mode |= M_RSHIFT;
		break;

	/* 弹起按键，清除松开按键在Mode中对应的标志位 */
	case SCAN_ALT + 0x80:
		if ( 0xE0 == expand )
			Mode &= ~M_RALT;
		else
			Mode &= ~M_LALT;
		break;

	case SCAN_CTRL + 0x80:
		if ( 0xE0 == expand )
			Mode &= ~M_RCTRL;
		else
			Mode &= ~M_LCTRL;
		break;

	case SCAN_LSHIFT + 0x80:
		Mode &= ~M_LSHIFT;
		break;

	case SCAN_RSHIFT + 0x80:
		Mode &= ~M_RSHIFT;
		break;

	/* 每次按键就反转切换标志状态位 */
	
	/* 
	 * M_DOWN_NUMLOCK用于表示NUMLOCK键已按下，还没有抬起的
	 * 状态。按下NumLock键需要反转状态，如果numlock键没有弹起
	 */
	case SCAN_NUMLOCK:
		isOK = Mode & M_DOWN_NUMLOCK;
		if ( !isOK )
		{
			Mode ^= M_NUMLOCK;
			Mode |= M_DOWN_NUMLOCK;
		}
		break;

	case SCAN_CAPSLOCK:
		isOK = Mode & M_DOWN_CAPSLOCK;
		if ( !isOK )
		{
			Mode ^= M_CAPSLOCK;
			Mode |= M_DOWN_CAPSLOCK;
		}
		break;

	case SCAN_SCRLOCK:
		isOK = Mode & M_DOWN_SCRLOCK;
		if ( !isOK )
		{
			Mode ^= M_SCRLOCK;
			Mode |= M_DOWN_SCRLOCK;
		}
		break;

	/* 抬起NumLock、CapsLock和ScrollLock键 */
	case SCAN_NUMLOCK + 0x80:
		Mode &= ~M_DOWN_NUMLOCK;
		break;

	case SCAN_CAPSLOCK + 0x80:
		Mode &= ~M_DOWN_CAPSLOCK;
		break;

	case SCAN_SCRLOCK + 0x80:
		Mode &= ~M_DOWN_SCRLOCK;
		break;

	/* Tab 键 - 切换焦点窗口 */
	case 0x0F:	/* Tab 扫描码 */
		if ( !(scanCode & 0x80) )  /* 只记录按下，不处理弹起 */
		{
			ToggleFocus();
		}
		break;

	/* 处理方向键扫描码以及翻页键 - 根据焦点窗口路由到对应窗口 */
	case SCAN_PAGEUP:
		if ( 0xE0 == expand )	/* Page Up 是 扩展码 */
		{
			if ( m_ScrollFocus == FOCUS_CRT )
				CRT::ScrollUp(5);	/* 终端窗口，向上滚动5行 */
			else
				Diagnose::ScrollUp(5);	/* 诊断窗口，向上滚动5行 */
		}
		break;

	case SCAN_PAGEDOWN:
		if ( 0xE0 == expand )	/* Page Down 是 扩展码 */
		{
			if ( m_ScrollFocus == FOCUS_CRT )
				CRT::ScrollDown(5);	/* 终端窗口，向下滚动5行 */
			else
				Diagnose::ScrollDown(5);	/* 诊断窗口，向下滚动5行 */
		}
		break;

	case SCAN_UP:
		if ( 0xE0 == expand )	/* 向上箭头键 是 扩展码 */
		{
			if ( m_ScrollFocus == FOCUS_CRT )
				CRT::ScrollUp(1);	/* 终端窗口，向上滚动1行 */
			else
				Diagnose::ScrollUp(1);	/* 诊断窗口，向上滚动1行 */
		}
		break;

	case SCAN_DOWN:
		if ( 0xE0 == expand )	/* 向下箭头键 是 扩展码 */
		{
			if ( m_ScrollFocus == FOCUS_CRT )
				CRT::ScrollDown(1);	/* 终端窗口，向下滚动1行 */
			else
				Diagnose::ScrollDown(1);	/* 诊断窗口，向下滚动1行 */
		}
		break;

	default:
		ch = Keyboard::ScanCodeTranslate(scanCode, expand);
		break;
	}
	if ( 0 != ch )
	{
		TTy* pTTy = Kernel::Instance().GetDeviceManager().GetCharDevice(DeviceManager::TTYDEV).m_TTy;
		if ( NULL != pTTy )
		{
			pTTy->TTyInput(ch);
		}
	}
}

char Keyboard::
ScanCodeTranslate(unsigned char scanCode, int expand)
{
	char ch = 0;
	bool bReverse = false;

	if ( 0xE1 == expand )	/* Pause Key */
	{
		ch = 0x05;	/* Pause ASCII */
	}
	else if ( scanCode < 0x45 )	/* 大小写字母和控制键 */
	{
		/* 将扫描码映射到相应的对应的字符ASCII码 */
		if ( (Mode & M_LSHIFT) || (Mode & M_RSHIFT) )
		{
			ch = Shift_Keymap[scanCode];
		}
		else
		{
			ch = Keymap[scanCode];
		}

		if ( ch >= 'a' && ch <= 'z' )
		{
			/* 对小写字符，判断是否已经Capslock了，如果是么转换成大写字符 */
			bReverse = ( (Mode & M_CAPSLOCK) ? 1 : 0 ) ^ ( (Mode & M_LSHIFT) || (Mode & M_RSHIFT) );

			if ( (Mode & M_LCTRL) || (Mode & M_RCTRL) )	/* 按下ctrl键转换 */
			{
				if('c' ==  ch)  /* ctrl+c --> SIGINT信号，中断前台 sched、shell 之外的所有进程 */
				{
					ch = 0;

					/* Flush队列 */
					TTy* pTTy = Kernel::Instance().GetDeviceManager().GetCharDevice(DeviceManager::TTYDEV).m_TTy;
					if ( NULL != pTTy )
					{
						pTTy->FlushTTy();
					}

					ProcessManager& procMgr = Kernel::Instance().GetProcessManager();
					for ( int killed = 0; killed < ProcessManager::NPROC ; killed ++ )
						if ( procMgr.process[killed].p_pid > 1)
							procMgr.process[killed].PSignal(User::SIGINT);
				}
				else
				{
					ch -= 'a';
				    ch++;	/* 转换从0x1 开始*/
				}
			}
			else if ( bReverse )
			{
				ch += 'A' - 'a';
			}
		}
	}
	else if ( scanCode < 0x58 )
	{
		bReverse = ( (Mode & M_NUMLOCK) ? 1 : 0 ) ^ ( (Mode & M_LSHIFT) || (Mode & M_RSHIFT) );

		if ( 0xE0 == expand )
		{
			ch = Shift_Keymap[scanCode];
		}
		else if ( bReverse )
		{
			ch = Keymap[scanCode];
		}
		else
		{
			ch = Shift_Keymap[scanCode];
		}
	}

	return ch;
}

void Keyboard::ToggleFocus()
{
	/* 切换焦点窗口 */
	if ( m_ScrollFocus == FOCUS_CRT )
	{
		m_ScrollFocus = FOCUS_DIAGNOSE;
		Diagnose::Write("[Focus: Diagnose Window]\n");
	}
	else
	{
		m_ScrollFocus = FOCUS_CRT;
		Diagnose::Write("[Focus: CRT Window]\n");
	}
}

