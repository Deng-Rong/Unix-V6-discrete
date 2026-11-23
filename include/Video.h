//Video.h
#ifndef DIAGNOSE_H
#define DIAGNOSE_H

class Diagnose
{
public:
	static unsigned int ROWS;

	/* static const member */
	static const unsigned int COLUMNS = 80;
	static const unsigned short COLOR = 0x0B00;	/* char in bright CYAN */
	static const unsigned int SCREEN_ROWS = 25;	/* full screen rows */

	/* 历史缓冲区常量 */
	static const unsigned int HISTORY_LINES = 1500;	/* 最多保存多少行历史 */
	static const unsigned long HISTORY_BUFFER_ADDRESS = 0xC03BB000;

public:
	Diagnose();
	~Diagnose();

	static void TraceOn();
	static void TraceOff();

	static void Write(const char* fmt, ...);
	static void ClearScreen();
	static void ScrollScreen();

	/* 视图窗口滚动  */
	static void ScrollUp(unsigned int lines = 1);
	static void ScrollDown(unsigned int lines = 1);
	static void RefreshScreen();

private:	
	static void PrintInt(unsigned int value, int base);
	static void NextLine();
	static void WriteChar(const char ch);

public:
	static unsigned int		m_Row;
	static unsigned int		m_Column;

private:
	static unsigned short*	m_VideoMemory;
	/* Debug输出开关 */
	static bool trace_on;

	/* 历史缓冲区相关变量  */
	static unsigned short* m_HistoryBuffer;
	static unsigned int m_TotalLines;
	static unsigned int m_ViewStartLine;
	static bool m_AutoScroll;
};

#endif
