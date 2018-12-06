#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h> // 类型头文件

typedef int sig_atomic_t; // 信号原子操作类型
typedef unsigned int sigset_t;		/* 32 bits */ // 信号集类型，32位非负整数

#define _NSIG             32 // 信号数量，因为信号集采用了32位非负整数，所以最多只能32个
#define NSIG		_NSIG

/**
 * 以下是预定义的信号类型，其中包括POSIX.1要求的20个信号，以及2个自定义信号
 */
#define SIGHUP		 1 // Hang Up 挂断控制终端或进程
#define SIGINT		 2 // Interrup 键盘的中断 Ctrl+C 
#define SIGQUIT		 3 // Quit 键盘的退出
#define SIGILL		 4 // Illegal 非法指令
#define SIGTRAP		 5 // Trap 跟踪断点
#define SIGABRT		 6 // Abort 异常结束
#define SIGIOT		 6 // IO Trap 同上
#define SIGUNUSED	 7 // Unused 没有使用
#define SIGFPE		 8 // FPE 协处理出错
#define SIGKILL		 9 // Kill 强迫进程终止 kill -9 
#define SIGUSR1		10 // User1 用户自定义1
#define SIGSEGV		11 // Segment Violation 无效的内存引用
#define SIGUSR2		12 // User2 用户自定义2 
#define SIGPIPE		13 // Pipe 管道写出错，无读者
#define SIGALRM		14 // ALarm 实时定时器报警
#define SIGTERM		15 // Terminate 进程终止
#define SIGSTKFLT	16 // Stack Fault 栈出错（协处理器）
#define SIGCHLD		17 // Child 子进程停止或终止
#define SIGCONT		18 // Continue 恢复进程执行
#define SIGSTOP		19 // Stop 停止进程执行 Ctrl + Z 
#define SIGTSTP		20 // TTY Stop -- tty 发出停止进程，可忽略
#define SIGTTIN		21 // TTY in -- 后台进程请求输入
#define SIGTTOU		22 // TTY out -- 后台进程请求输出

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */

// sa_flags 标志段可取的符号常量值
#define SA_NOCLDSTOP	1  // 当子进程处于停止状态，就不对 SIGCHLD 信号进行处理
#define SA_NOMASK	0x40000000 // 不阻止在信号处理程序中再次收到该信号
#define SA_ONESHOT	0x80000000 // 信号句柄一旦被调用就恢复到默认处理句柄

// 以下常量用于 sigprocmask(how, ) : 改变阻塞信号集（屏蔽码）。用于改变该函数的行为
#define SIG_BLOCK          0	/* for blocking signals */ // 在阻塞信号集加上指定的信号
#define SIG_UNBLOCK        1	/* for unblocking signals */ //在阻塞信号集上删除指定的信号
#define SIG_SETMASK        2	/* for setting the signal mask */ // 设置阻塞信号集

// 以下两个符号参数都表示函数指针，这个函数有一个整形参数，无返回值
// 其中 0 和 1 表示函数指针的地址值，实际上这是不可能出现的函数地址值，以此来区分特殊的信号处理函数
#define SIG_DFL		((void (*)(int))0)	/* default signal handling */ // 默认信号处理句柄
#define SIG_IGN		((void (*)(int))1)	/* ignore signal */ // 忽略信号处理句柄

// 信号处理结构
// 注意：如果 sa_flags 中 SA_NOMASK 没有被设置，那触发信号处理句柄的信号 也会被自动加入屏蔽中！！！
struct sigaction {
        void (*sa_handler)(int); // 信号处理句柄，可以自定义，也可以是上面的 SIG_DFL, SIG_IGN 
        sigset_t sa_mask; //信号的屏蔽码，在 sa_handler 被调用的时候，将阻塞这些信号，在 sa_handler 返回后恢复
        int sa_flags; // 信号处理过程行为的标志位，可以是 SA_NOMASK, SA_ONESHOT, SA_NOCLDSTOP 等
        void (*sa_restorer)(void); // 恢复函数指针，由函数库 libc 提供，用于清理信号句柄调用期间的用户态堆栈
};

// 不可靠的为信号 _sig 安装新的处理句柄
void (*signal(int _sig, void (*_func)(int)))(int);
// 向当前进程发送信号 kernel/exit.c 
int raise(int sig);
// 向任一进程发送信号 kernel/exit.c 
int kill(pid_t pid, int sig);
// 以下五个函数用于操作进程信号集类型的变量
int sigaddset(sigset_t *mask, int signo); // 从信号集添加某个信号
int sigdelset(sigset_t *mask, int signo); // 从信号集删除某个信号
int sigemptyset(sigset_t *mask); // 清空信号集
int sigfillset(sigset_t *mask); // 信号集置入所有信号
// 信号集中某个特定信号的状况：1 - 允许，2 - 屏蔽 , -1 - 出错
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
// 对 set 中的信号集进行测试，看是否有被挂起的信号，在 set 中返回进程中当前被阻塞的信号集
int sigpending(sigset_t *set);
// 修改当前进程的信号屏蔽码
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
// 临时替换当前进程的信号屏蔽码，然后挂起进程，直到进程收到一个信号
// 如果捕获到一个信号，而且从该信号处理句柄返回，则 sigsuspend 函数也返回，并且恢复原来的信号屏蔽码
int sigsuspend(sigset_t *sigmask);
// 可靠的为 sig 信号安装新的信号处理句柄
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
