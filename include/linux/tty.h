/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */

#ifndef _TTY_H
#define _TTY_H

#include <termios.h> // 终端输入输出头文件

#define TTY_BUF_SIZE 1024 // 终端缓冲区大小1024字节

/**
 * tty字符缓冲队列的数据结构
 *
 * 用于tty_struct结构中的读、写和辅助（规范）缓冲队列
 * 
 */
struct tty_queue {
        unsigned long data; // 队列缓冲区含有的字符行数值（不是字符数），如果是串行端口，则保存串行端口的端口地址
        unsigned long head; // 缓冲区中数据头指针
        unsigned long tail; // 缓冲区中数据尾指针
        struct task_struct * proc_list; // 等待本队列的进程列表
        char buf[TTY_BUF_SIZE]; // 队列的数据缓冲区
};

/**
 * 定义了tty队列的中缓冲区操作宏
 *
 * 注意：tail在前，head在后！！！
 * 
 */

// 缓冲区指针前移1字节，如果超出缓冲区右侧，则循环
#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))
// 缓冲区指针后移1字节，如果超出缓冲区左侧，则循环
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))
// 清空缓冲区
#define EMPTY(a) ((a).head == (a).tail)
// 缓冲区还可存放的字符个数（空闲区长度） 
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1))
// 缓冲区最后一个位置
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)])
// 缓冲区是否已满
#define FULL(a) (!LEFT(a))
// 缓冲区已存放字符的长度（字符数）
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))
// 从缓冲区取走一个字符（在tail处，并且tail的值+1）
#define GETCH(queue,c)                                              \
        (void)({c=(queue).buf[(queue).tail];INC((queue).tail);})
// 把一个字符放入缓冲区（在head处，并且head的值+1）
#define PUTCH(c,queue)                                              \
        (void)({(queue).buf[(queue).head]=(c);INC((queue).head);})

/**
 * 判断终端键盘字符类型
 *
 */
#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR]) // 中断符，发中断信号SIGINT
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT]) // 退出符，发退出信号SIGQUIT
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE]) //擦除符，擦除一个字符
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL]) // 删除行，删除一行字符
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF]) // 文件结束符
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART]) // 开始符，恢复输出
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP]) // 停止符，停止输出 
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP]) // 挂起符，发挂起信号SIGTSTP 


/**
 * tty终端数据结构
 */
        struct tty_struct {
                struct termios termios; // 终端io属性和控制字符数据结构
                int pgrp; // 所属进程组
                int stopped; // 停止标志
                void (*write)(struct tty_struct * tty); // 写函数指针
                struct tty_queue read_q; // 读队列
                struct tty_queue write_q; // 写队列
                struct tty_queue secondary; // 辅助（规范模式）队列
        };

extern struct tty_struct tty_table[]; // tty结构数组

/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/

/**
 * 这里定义了termios结构中c_cc[]数组的初始值（八进制值）
 *
 * POSIX定义了11个特殊字符，而linux又定义了其他6个特殊字符
 * 如果某一项被定义成了_POSIX_VDISABLE(\0): 表示相应的特殊字符被禁止使用
 * 
 */
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

void rs_init(void);
void con_init(void);
void tty_init(void);

int tty_read(unsigned c, char * buf, int n);
int tty_write(unsigned c, char * buf, int n);

void rs_write(struct tty_struct * tty);
void con_write(struct tty_struct * tty);

void copy_to_cooked(struct tty_struct * tty);

#endif
