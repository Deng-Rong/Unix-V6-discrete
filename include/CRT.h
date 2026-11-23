#ifndef CRT_H
#define CRT_H
#include "TTy.h"

class CRT
{
	/* Const Member */
public:
	/* 显示控制寄存器I/O端口地址 */
	static const unsigned short VIDEO_ADDR_PORT = 0x3d4;	/* 显示控制器地址寄存器端口号 */
	static const unsigned short VIDEO_DATA_PORT = 0x3d5;	/* 显示控制器数据寄存器端口号 */

	/* 屏幕大小为80 * 25 */
	static const unsigned int COLUMNS = 80;
	static unsigned int ROWS;

	/* 历史缓冲区常量 */
	static const unsigned int HISTORY_LINES = 1500;	/* 最多保存多少行历史 */
	static const unsigned long HISTORY_BUFFER_ADDRESS = 0xC0380000;
	static const unsigned short COLOR = 0x0F00;		/* char in white color */

	/* Functions */
public:
	/* 将队列中的输出字符输出到屏幕 */
	static void CRTStart(TTy* pTTy);
	/* 移动光标位置 */
	static void MoveCursor(unsigned int x, unsigned int y);
	/* 换行处理函数 */
	static void NextLine();
	/* 退格处理函数 */
	static void BackSpace();

	/* Tab处理函数 */
	static void Tab();
	/* 显示普通字符 */
	static void WriteChar(char ch);
	/* 清空屏幕 */
	static void ClearScreen();
	/* 屏幕滚动删除第一行 */
	static void ScrollScreen();

	/* 视图窗口滚动 - 向上滚动查看历史 */
	static void ScrollUp(unsigned int lines = 1);
	/* 视图窗口滚动 - 向下滚动查看最新 */
	static void ScrollDown(unsigned int lines = 1);
	/* 刷新显示 - 从历史缓冲区复制数据到屏幕 */
	static void RefreshScreen();

	/* Members */
public:
	static unsigned short* m_VideoMemory;
	static unsigned int m_CursorX;
	static unsigned int m_CursorY;

	/* 指向输出队列中当前要输出的字符 */
	static char* m_Position;

	/* 指向输出队列中未确认的输入字符的开始，用户可以通过Backspace键删除这部分数据，
	 * 但一旦回车之前输入将成为确认的数据，不可被Backspace键删除。
	 */
	static char* m_BeginChar;

	/* 历史缓冲区相关 */
	static unsigned short* m_HistoryBuffer;			/* 历史缓冲区指针 */
	static unsigned int m_TotalLines;				/* 当前总共缓存了多少行 */
	static unsigned int m_ViewStartLine;			/* 当前视图窗口的起始行号 */
	static bool m_AutoScroll;						/* 是否自动滚动到最新内容 */
};

#endif
