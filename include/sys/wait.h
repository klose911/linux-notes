/**
 * 此头文件等待进程终止相关的信息，包括一些符号常数和 wait(), waitpid()等函数原型声明
 */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h> 

#define _LOW(v)		( (v) & 0377) // 取低位字节
#define _HIGH(v)	( ((v) >> 8) & 0377) // 取高位字节

/* options for waitpid, WUNTRACED not supported */
// 以下常数用于 waitpid 的 options 选项
#define WNOHANG		1 // 如果没有状态，也不要挂起，并立刻返回
#define WUNTRACED	2 // 报告停止执行的子进程状态

/*
 * 下列宏用于判断 wait 系列调用返回的状态字，宏里的 s 指的是 wait 和 waipid 中的 stat_loc 字
 * 
 * xxxxxxxx11111111 : 表示子进程正常退出，低位字节全是1，高位字节表示退出的状态码
 * 00000000xxxxxxxx : 表示子进程因为信号退出，高位字节全是0，低位字节表示引起退出的信号
 * xxxxxxxx01111111 : 表示子进程正停止，低位字节第8位是0，其余全是1，高位字节表示引起停止的信号
 */
#define WIFEXITED(s)	(!((s)&0xFF) // 如果子进程正常退出，则为真（s 的低8位必须是 00000000）
#define WIFSTOPPED(s)	(((s)&0xFF)==0x7F) // 如果子进程正停止，则为真（s 的低8位是 01111111 == 0x7F ）
#define WEXITSTATUS(s)	(((s)>>8)&0xFF) // 如果进程正常退出，返回退出的状态（ s的高8位） 
#define WTERMSIG(s)	((s)&0x7F) // 返回导致进程终止的信号
#define WSTOPSIG(s)	(((s)>>8)&0xFF) // 返回导致进程停止的信号
#define WIFSIGNALED(s)	(((unsigned int)(s)-1 & 0xFFFF) < 0xFF)  // 如果子进程是因为信号而退出，则为真

// 挂起当前进程，直到当前进程的一个子进程退出或终止，或者收到一个要求该进程终止的信号，或者是调用一个信号句柄
pid_t wait(int *stat_loc);
// 挂起当前进程，直到当前进程的进程号为 pid 的子进程退出或终止，或者收到一个要求该进程终止的信号，或者是调用一个信号句柄
pid_t waitpid(pid_t pid, int *stat_loc, int options);

#endif
