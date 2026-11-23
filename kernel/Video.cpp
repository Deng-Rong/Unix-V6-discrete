#include "Video.h"

unsigned short* Diagnose::m_VideoMemory = (unsigned short *)(0xB8000 + 0xC0000000);
unsigned int Diagnose::m_Row = 10;
unsigned int Diagnose::m_Column = 0;

unsigned int Diagnose::ROWS = 10;

bool Diagnose::trace_on = true;

/* 历史缓冲区数据 */
unsigned short* Diagnose::m_HistoryBuffer =
	(unsigned short*)Diagnose::HISTORY_BUFFER_ADDRESS;
unsigned int Diagnose::m_TotalLines = 0;
unsigned int Diagnose::m_ViewStartLine = 0;
bool Diagnose::m_AutoScroll = true;

Diagnose::Diagnose()
{
	// 全局变量static成员变量都不需要在构造函数中初始化的。
}

Diagnose::~Diagnose()
{
	//this is an empty dtor
}

void Diagnose::TraceOn()
{
	Diagnose::trace_on = 1;
}

void Diagnose::TraceOff()
{
	Diagnose::trace_on = 0;
}

/*
	能够格式化输出字符串，目前只识别了一些%d %x %s 和%n;
	没有加检查功能，% 和 值匹配要自己注意。
*/
void Diagnose::Write(const char* fmt, ...)
{
	if ( false == Diagnose::trace_on )
	{
		return;
	}
	// 使用va_arg传递参数fmt后 的下一个参数的 所在的内存地址
	// fmt存储的内容本身是字符串的首地址(这不是最重要的)，而&fmt + 1是下一个参数的地址
	// 参考UNIX v6中的函数prf.c/printf(fmt, x1,x2,x3,x4,x5,x6,x7,x8,x9,xa,xb,xc)
	unsigned int * va_arg = (unsigned int *)&fmt + 1;
	const char * ch = fmt;
	
	while(1)
	{
		while(*ch != '%' && *ch != '\n')
		{
			if(*ch == '\0')
				return;
			if(*ch == '\n')
				break;
			/* 注意： '\n'应一个当一个字符处理，是'\\'和 'n'两个字符的连接， 
			猜测：字符串"\nHello World!!"，这样比较 if(*ch == '\\' && *(ch+1) == '\n' ) 的话，
			是相当恨粗糙的！*/
			WriteChar(*ch++);
		}
		
		ch++;	//skip the '%' or '\n'   

		if(*ch == 'd' || *ch == 'x')
		{//%d 和 %x 格式化输出，虽然要区分八进制和二进制也不困难，但还是懒得管了
			int value = (int)(*va_arg);
			va_arg++;
			if(*ch == 'x')
				Write("0x");   //as prefix for HEX value
			PrintInt(value, *ch == 'd' ? 10 : 16);
			ch++;	//skip the 'd' or 'x'
		}
		
		else if(*ch == 's')
		{//%s 格式化输出
			ch++;	//skip the 's'
			char *str = (char *)(*va_arg);
			va_arg++;
			while(char tmp = *str++)
			{
				WriteChar(tmp);
			}
		}
		else /* if(*(ch-1) == '\n') */
		{
			Diagnose::NextLine();
		}
	}
}

/*
	参考UNIX v6中的函数prf.c/printn(n,b)
	这个函数的功能是将一个值value以base进制的方式表示输出
*/
void Diagnose::PrintInt(unsigned int value, int base)
{
	// 因为数字0~9 和 A~F的ASCII值之间不是连续的，所以不能简单通过
	// ASCII(i) = i + '0'直接计算得到，而需要用Digits字符查找表。
	static char Digits[] = "0123456789ABCDEF";
	int i;
	
	if((i = value / base) != 0)
		PrintInt(i ,base);
	WriteChar(Digits[value % base]);
}

void Diagnose::NextLine()
{
	if ( m_TotalLines == 0 )
	{
		m_TotalLines = ROWS;
		m_Row = 0;
		for ( unsigned int i = 0; i < HISTORY_LINES * COLUMNS; i++ )
			m_HistoryBuffer[i] = (unsigned short)' ' | Diagnose::COLOR;
	}

	m_Row += 1;
	m_Column = 0;

	/* 检查是否超出总行数 */
	if ( m_Row >= m_TotalLines )
	{
		if ( m_TotalLines < HISTORY_LINES )
		{
			m_TotalLines++;
		}
		else
		{
			ScrollScreen();
			m_Row = HISTORY_LINES - 1;
		}
	}

	/* 新输出始终回到最新内容，包括只有换行的输出 */
	if ( !m_AutoScroll )
		m_AutoScroll = true;
	if ( m_TotalLines > ROWS )
	{
		m_ViewStartLine = m_TotalLines - ROWS;
	}
	else
	{
		m_ViewStartLine = 0;
	}
	RefreshScreen();
}

void Diagnose::WriteChar(const char ch)
{
	/* 初始化历史缓冲区 */
	if ( m_TotalLines == 0 )
	{
		m_TotalLines = ROWS;
		m_Row = 0;
		for ( unsigned int i = 0; i < HISTORY_LINES * COLUMNS; i++ )
		{
			m_HistoryBuffer[i] = (unsigned short)' ' | Diagnose::COLOR;
		}
	}

	/* 如果自动滚动被关闭，有新输入后需要自动滚动到最新内容 */
	if ( !m_AutoScroll )
	{
		m_AutoScroll = true;
		if ( m_TotalLines > ROWS )
		{
			m_ViewStartLine = m_TotalLines - ROWS;
		}
		else
		{
			m_ViewStartLine = 0;
		}
		RefreshScreen();
	}

	if(Diagnose::m_Column >= Diagnose::COLUMNS)
	{
		NextLine();
	}

	/* 写入历史缓冲区 */
	unsigned int bufferPos = m_Row * COLUMNS + m_Column;
	m_HistoryBuffer[bufferPos] = (unsigned char) ch | Diagnose::COLOR;

	/* 显示到显示内存 */
	if ( m_Row >= m_ViewStartLine && m_Row < m_ViewStartLine + ROWS )
	{
		unsigned int screenRow = m_Row - m_ViewStartLine + (SCREEN_ROWS - ROWS);
		Diagnose::m_VideoMemory[screenRow * COLUMNS + m_Column] = m_HistoryBuffer[bufferPos];
	}

	Diagnose::m_Column++;
}

void Diagnose::ClearScreen()
{
	unsigned int i;

	Diagnose::m_Row = Diagnose::SCREEN_ROWS - Diagnose::ROWS;
	Diagnose::m_Column = 0;

	for(i = 0; i < (COLUMNS * ROWS); i++)
	{
		Diagnose::m_VideoMemory[i + m_Row * COLUMNS] = (unsigned char) ' ' | Diagnose::COLOR;
	}
}

void Diagnose::ScrollScreen()
{
	unsigned int i;

	/* 将历史缓冲区中第二行到最后一行的数据复制到前一行 */
	for ( i = 0; i < COLUMNS * (HISTORY_LINES - 1); i++ )
	{
		m_HistoryBuffer[i] = m_HistoryBuffer[i + COLUMNS];
	}

	/* 清空最后一行 */
	for ( i = COLUMNS * (HISTORY_LINES - 1); i < COLUMNS * HISTORY_LINES; i++ )
	{
		m_HistoryBuffer[i] = (unsigned short)' ' | Diagnose::COLOR;
	}
}

void Diagnose::RefreshScreen()
{
	unsigned int startRow = SCREEN_ROWS - ROWS;
	unsigned int displayLines = (m_TotalLines < ROWS) ? m_TotalLines : ROWS;

	/* 从历史缓冲区数据复制到屏幕 */
	for ( unsigned int row = 0; row < displayLines; row++ )
	{
		unsigned int historyRow = m_ViewStartLine + row;
		for ( unsigned int col = 0; col < COLUMNS; col++ )
		{
			m_VideoMemory[(startRow + row) * COLUMNS + col] = m_HistoryBuffer[historyRow * COLUMNS + col];
		}
	}

	/* 清空其余多余的空白行 */
	for ( unsigned int row = displayLines; row < ROWS; row++ )
	{
		for ( unsigned int col = 0; col < COLUMNS; col++ )
		{
			m_VideoMemory[(startRow + row) * COLUMNS + col] = (unsigned short)' ' | Diagnose::COLOR;
		}
	}
}

void Diagnose::ScrollUp(unsigned int lines)
{
	if ( m_ViewStartLine >= lines )
	{
		m_ViewStartLine -= lines;
	}
	else
	{
		m_ViewStartLine = 0;
	}

	m_AutoScroll = false;
	RefreshScreen();
}

void Diagnose::ScrollDown(unsigned int lines)
{
	unsigned int maxStartLine = (m_TotalLines > ROWS) ? (m_TotalLines - ROWS) : 0;

	if ( lines < maxStartLine - m_ViewStartLine )
	{
		m_ViewStartLine += lines;
	}
	else
	{
		m_ViewStartLine = maxStartLine;
	}
	m_AutoScroll = (m_ViewStartLine == maxStartLine);

	RefreshScreen();
}
