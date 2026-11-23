#include "CRT.h"
#include "IOPort.h"

unsigned short* CRT::m_VideoMemory = (unsigned short*)(0xB8000 + 0xC0000000);
unsigned int CRT::m_CursorX = 0;
unsigned int CRT::m_CursorY = 0;
char* CRT::m_Position = 0;
char* CRT::m_BeginChar = 0;

unsigned int CRT::ROWS = 15;

/* 历史缓冲区数据 */
unsigned short* CRT::m_HistoryBuffer =
	(unsigned short*)CRT::HISTORY_BUFFER_ADDRESS;
unsigned int CRT::m_TotalLines = 0;
unsigned int CRT::m_ViewStartLine = 0;
bool CRT::m_AutoScroll = true;

void CRT::CRTStart(TTy* pTTy)
{
	char ch;
	if (0 == CRT::m_BeginChar)
	{
		m_BeginChar = pTTy->t_outq.CurrentChar();
	}
	if (0 == m_Position)
	{
		m_Position = m_BeginChar;
	}

	while ((ch = pTTy->t_outq.GetChar()) != TTy::GET_ERROR)
	{
		switch (ch)
		{
		case '\n':
			NextLine();
			CRT::m_BeginChar = pTTy->t_outq.CurrentChar();
			m_Position = CRT::m_BeginChar;
			break;

		case 0x15:
			//del_line();
			break;

		case '\b':
			if (m_Position != CRT::m_BeginChar)
			{
				BackSpace();
				m_Position--;
			}
			break;

		case '\t':
			Tab();
			m_Position++;
			break;

		default:	/* 在屏幕上回显普通字符 */
			WriteChar(ch);
			m_Position++;
			break;
		}
	}
}

void CRT::MoveCursor(unsigned int col, unsigned int row)
{
	if ((col < 0 || col >= CRT::COLUMNS) || (row < 0 || row >= CRT::ROWS))
	{
		return;
	}

	/* 计算光标偏移量 */
	unsigned short cursorPosition = row * CRT::COLUMNS + col;

	/* 选择r14和r15寄存器，分别为光标位置的高8位和低8位 */
	IOPort::OutByte(CRT::VIDEO_ADDR_PORT, 14);
	IOPort::OutByte(CRT::VIDEO_DATA_PORT, cursorPosition >> 8);
	IOPort::OutByte(CRT::VIDEO_ADDR_PORT, 15);
	IOPort::OutByte(CRT::VIDEO_DATA_PORT, cursorPosition & 0xFF);
}

void CRT::NextLine()
{
	if (m_TotalLines == 0)
	{
		m_TotalLines = ROWS;
		for (unsigned int i = 0; i < HISTORY_LINES * COLUMNS; i++)
			m_HistoryBuffer[i] = (unsigned short)' ' | CRT::COLOR;
	}

	m_CursorX = 0;
	m_CursorY += 1;

	/* 检查是否超出当前总行数，需要增加一行 */
	if (m_CursorY >= m_TotalLines)
	{
		/* 只保存最多HISTORY_LINES行的历史记录，不清屏 */
		if (m_TotalLines < HISTORY_LINES)
		{
			m_TotalLines++;
		}
		else
		{
			/* 历史缓冲区已满，需要循环覆盖 */
			ScrollScreen();
			m_CursorY = HISTORY_LINES - 1;
		}
	}

	/* 新输出始终回到最新内容，包括只有换行的输出 */
	if (!m_AutoScroll)
		m_AutoScroll = true;
	if (m_TotalLines > ROWS)
	{
		m_ViewStartLine = m_TotalLines - ROWS;
	}
	else
	{
		m_ViewStartLine = 0;
	}
	RefreshScreen();

	MoveCursor(m_CursorX, m_CursorY - m_ViewStartLine);
}

void CRT::BackSpace()
{
	if (m_CursorX == 0)
	{
		if (m_CursorY == 0)
			return;
		m_CursorX = CRT::COLUMNS - 1;
		m_CursorY--;
	}
	else
		m_CursorX--;

	if (!m_AutoScroll)
	{
		m_AutoScroll = true;
		m_ViewStartLine = (m_TotalLines > ROWS) ? m_TotalLines - ROWS : 0;
		RefreshScreen();
	}

	/* 更新历史缓冲区 */
	unsigned int bufferPos = m_CursorY * COLUMNS + m_CursorX;
	m_HistoryBuffer[bufferPos] = ' ' | CRT::COLOR;

	/* 如果AutoScroll，则刷新可见显示窗口 */
	if (m_AutoScroll && m_CursorY >= m_ViewStartLine && m_CursorY < m_ViewStartLine + ROWS)
	{
		unsigned int displayY = m_CursorY - m_ViewStartLine;
		m_VideoMemory[displayY * COLUMNS + m_CursorX] = ' ' | CRT::COLOR;
	}

	if (m_CursorY >= m_ViewStartLine && m_CursorY < m_ViewStartLine + ROWS)
		MoveCursor(m_CursorX, m_CursorY - m_ViewStartLine);
}

void CRT::Tab()
{
	if (!m_AutoScroll)
	{
		m_AutoScroll = true;
		m_ViewStartLine = (m_TotalLines > ROWS) ? m_TotalLines - ROWS : 0;
		RefreshScreen();
	}

	m_CursorX &= 0xFFFFFFF8;	/* 向左对齐到前一个Tab边界 */
	m_CursorX += 8;
	// const int TabWidth = 10;
	// m_CursorX -= m_CursorX % TabWidth;
	// m_CursorX += TabWidth;
	if (m_CursorX >= CRT::COLUMNS)
		NextLine();
	else
		MoveCursor(m_CursorX, m_CursorY - m_ViewStartLine);
}

void CRT::WriteChar(char ch)
{
	/* 确保历史缓冲区大小 */
	if (m_TotalLines == 0)
	{
		m_TotalLines = ROWS;
		/* 初始化历史缓冲区为空格 */
		for (unsigned int i = 0; i < HISTORY_LINES * COLUMNS; i++)
		{
			m_HistoryBuffer[i] = (unsigned short)' ' | CRT::COLOR;
		}
	}

	/* 如果用户在查看历史缓冲区，有新输入后需要自动滚动到最新内容 */
	if (!m_AutoScroll)
	{
		m_AutoScroll = true;
		if (m_TotalLines > ROWS)
		{
			m_ViewStartLine = m_TotalLines - ROWS;
		}
		else
		{
			m_ViewStartLine = 0;
		}
		RefreshScreen();
	}

	/* 写入历史缓冲区的当前位置 */
	unsigned int bufferPos = m_CursorY * COLUMNS + m_CursorX;
	m_HistoryBuffer[bufferPos] = (unsigned char)ch | CRT::COLOR;

	/* 显示在可见显示窗口 */
	if (m_CursorY >= m_ViewStartLine && m_CursorY < m_ViewStartLine + ROWS)
	{
		unsigned int displayY = m_CursorY - m_ViewStartLine;
		m_VideoMemory[displayY * COLUMNS + m_CursorX] = m_HistoryBuffer[bufferPos];
	}

	m_CursorX++;

	if (m_CursorX >= CRT::COLUMNS)
	{
		NextLine();
	}
	else
	{
		MoveCursor(m_CursorX, m_CursorY - m_ViewStartLine);
	}
}

void CRT::ClearScreen()
{
	unsigned int i;

	for (i = 0; i < COLUMNS * ROWS; i++)
	{
		m_VideoMemory[i] = (unsigned short)' ' | CRT::COLOR;
	}
}

void CRT::ScrollScreen()
{
	unsigned int i;

	/* 将历史缓冲区中第二行到最后一行的数据复制到前一行 */
	for (i = 0; i < COLUMNS * (HISTORY_LINES - 1); i++)
	{
		m_HistoryBuffer[i] = m_HistoryBuffer[i + COLUMNS];
	}

	/* 清空最后一行 */
	for (i = COLUMNS * (HISTORY_LINES - 1); i < COLUMNS * HISTORY_LINES; i++)
	{
		m_HistoryBuffer[i] = (unsigned short)' ' | CRT::COLOR;
	}
}

void CRT::RefreshScreen()
{
	/* 从历史缓冲区的数据复制到屏幕 */
	unsigned int displayLines = (m_TotalLines < ROWS) ? m_TotalLines : ROWS;

	for (unsigned int row = 0; row < displayLines; row++)
	{
		unsigned int historyRow = m_ViewStartLine + row;
		for (unsigned int col = 0; col < COLUMNS; col++)
		{
			m_VideoMemory[row * COLUMNS + col] = m_HistoryBuffer[historyRow * COLUMNS + col];
		}
	}

	/* 清空其余多余的空格 */
	for (unsigned int row = displayLines; row < ROWS; row++)
	{
		for (unsigned int col = 0; col < COLUMNS; col++)
		{
			m_VideoMemory[row * COLUMNS + col] = (unsigned short)' ' | CRT::COLOR;
		}
	}
}

void CRT::ScrollUp(unsigned int lines)
{
	/* 向上翻页 */
	if (m_ViewStartLine >= lines)
	{
		m_ViewStartLine -= lines;
	}
	else
	{
		m_ViewStartLine = 0;
	}

	m_AutoScroll = false;
	RefreshScreen();

	/* 更新光标显示位置 */
	unsigned int displayY = m_CursorY - m_ViewStartLine;
	if (displayY < ROWS)
	{
		MoveCursor(m_CursorX, displayY);
	}
}

void CRT::ScrollDown(unsigned int lines)
{
	/* 向下翻页，检查是否到达底部 */
	unsigned int maxStartLine = (m_TotalLines > ROWS) ? (m_TotalLines - ROWS) : 0;

	if (lines < maxStartLine - m_ViewStartLine)
	{
		m_ViewStartLine += lines;
	}
	else
	{
		m_ViewStartLine = maxStartLine;
	}
	m_AutoScroll = (m_ViewStartLine == maxStartLine);

	RefreshScreen();

	/* 更新光标显示位置 */
	unsigned int displayY = m_CursorY - m_ViewStartLine;
	if (displayY < ROWS)
	{
		MoveCursor(m_CursorX, displayY);
	}
}

