#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "Regs.h"

class Keyboard
{
	/* Const Member */
public:
	/* 键盘I/O端口地址 */
	static const unsigned short DATA_PORT = 0x60;	/* 键盘数据寄存器端口号 */
	static const unsigned short STATUS_PORT = 0x64;	/* 键盘状态寄存器端口号 */

	/* 状态寄存器的位域定义 (端口号：0x64) */
	static const unsigned char DATA_BUFFER_BUSY = 0x1;	/* 键盘数据缓冲区是否满 */
	static const unsigned char AUX_DATA = 0x20;	/* 当前数据来自鼠标 */

	/* 扫描码常量定义 */
	static const unsigned char SCAN_ALT = 0x38;
	static const unsigned char SCAN_CTRL = 0x1d;
	static const unsigned char SCAN_LSHIFT = 0x2a;
	static const unsigned char SCAN_RSHIFT = 0X36;
	static const unsigned char SCAN_ESC = 0x01;
	static const unsigned char SCAN_NUMLOCK = 0x45;
	static const unsigned char SCAN_CAPSLOCK = 0x3a;
	static const unsigned char SCAN_SCRLOCK = 0x46;

	/* 扩展扫描码 (需要 0xE0 前缀) */
	static const unsigned char SCAN_UP = 0x48;
	static const unsigned char SCAN_DOWN = 0x50;
	static const unsigned char SCAN_PAGEUP = 0x49;
	static const unsigned char SCAN_PAGEDOWN = 0x51;

	/* 用于焦点窗口状态 */
	enum ScrollFocus
	{
		FOCUS_CRT = 0,		/* 终端窗口（上窗口） */
		FOCUS_DIAGNOSE = 1	/* 日志窗口（下窗口） */
	};

	/* 下面为描绘键盘按下的状态在mode中各个位的定义 */
	static const int M_LCTRL = 0x01;
	static const int M_RCTRL = 0x02;
	static const int M_LALT = 0x04;
	static const int M_RALT = 0x08;
	static const int M_LSHIFT = 0x10;
	static const int M_RSHIFT = 0x20;
	static const int M_NUMLOCK = 0x40;
	static const int M_CAPSLOCK = 0x80;
	static const int M_SCRLOCK = 0x100;
	static const int M_DOWN_NUMLOCK = 0x200;
	static const int M_DOWN_CAPSLOCK = 0x400;
	static const int M_DOWN_SCRLOCK = 0x800;

	/* Functions */
public:
	/* 键盘中断设备驱动处理函数 */
	static void KeyboardHandler(struct pt_regs* reg, struct pt_context* context);

	/*
	 * 处理扫描码的处理函数 scanCode(扫描码) expand(扩展码)。
	 * expand表示是否是扩展的键，需要处理中断器找到ctrl，alt
	 */
	static void HandleScanCode(unsigned char scanCode, int expand);

	/* 将按键扫描码转换为对应ASCII码 */
	static char ScanCodeTranslate(unsigned char scanCode, int expand);

	/* 切换窗口焦点 */
	static void ToggleFocus();

	/* 获取当前窗口焦点 */
	static ScrollFocus GetFocus() { return m_ScrollFocus; }

	/* Members */
public:
	/*
	 * 键盘映射表：取得一个键的字母，加号，<fx>功能键没
	 * 有处理。如果返回0，asc字表示在该系统中没有映射。
	 * keymap 表示shift键没有按下的键对应扫描码的映射表。
	 * 这里最多0x45个扫描码，也就是小键盘和功能键的扫
	 * 描码，这里表示numlock键按下的情况
	 */
	static char Keymap[];

	/* shift_keymap表示shift按下的情况 */
	static char Shift_Keymap[];

	/* ctrl, alt, shift的状态以及numlock,capslock,scrlock的按下开关*/
	static int Mode;

	/* 当前窗口焦点状态 */
	static ScrollFocus m_ScrollFocus;
};

#endif
