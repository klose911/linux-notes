/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h> // 调度程序头文件
#include <linux/kernel.h> // 内核头文件
#include <asm/segment.h> // 段操作头文件

#include <signal.h> // 信号头文件

volatile void do_exit(int error_code);

/**
 * 获取当前进程的信号屏蔽码（位图）
 * sgetmask 是 signal-get-mask 的缩写
 *
 * 返回值：当前进程的信号屏蔽码
 */
int sys_sgetmask()
{
	return current->blocked;
}

/**
 * 设置当前进程的信号屏蔽码，信号 SIGKILL 无法被屏蔽
 *
 * newmask：新的信号屏蔽码
 *
 * 返回值：旧的信号屏蔽码
 */
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

// 复制 sigaction 结构到 fs 数据段的 to 处（从内核空间复制到用户进程数据段中）
static inline void save_old(char * from,char * to)
{
        int i;

        // 首先验证 to 处的内存空间是否足够大
        verify_area(to, sizeof(struct sigaction));
        // 把一个sigaction结构一个字节一个字节的从 from 复制到　to 处
        for (i=0 ; i< sizeof(struct sigaction) ; i++) {
                put_fs_byte(*from,to);
                from++;
                to++;
        }
}

// 把 sigaction 结构从 fs数据段 from 处复制到内核数据段 to 处（从用户进程数据段复制到内核数据段中）
static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

/**
 * signal() 系统调用：为指定的信号安装新的信号处理句柄
 *
 * signum: 信号数字标识
 * handler： 信号处理句柄，可以是用户自定义函数指针，也可以是 SIG_DFL（默认） 或者 SIG_IGN（忽略）
 * restorer: 恢复函数指针，libc库提供，用于清除“调用信号处理函数”遗留的用户堆栈
 *
 * 返回值：成功返回旧的信号处理句柄，失败返回 -1 
 *
 * 这种信号处理方式是不可靠的，会丢失信号的！！！
 */
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

    // 首先验证信号数字是否有效，并且信号不能是 SIGKILL ，因为这个信号无法被捕获，否则内核可能就无法杀死进程
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
    // 初始化信号处理结构
	tmp.sa_handler = (void (*)(int)) handler; // 设置信号处理句柄
	tmp.sa_mask = 0; // 信号处理句柄被调用时，不屏蔽任何信号
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK; // 信号处理结构是临时的（信号处理句柄调用完毕后，该信号处理会恢复成默认设置），而且在信号处理句柄被调用时，不屏蔽该信号
	tmp.sa_restorer = (void (*)(void)) restorer; // 设置默认的恢复函数指针
    // 获取旧的信号处理句柄，作为返回值
	handler = (long) current->sigaction[signum-1].sa_handler;
    // 为信号设置新的“信号处理”结构
	current->sigaction[signum-1] = tmp;
	return handler;
}

/**
 * sigaction系统调用，为指定信号安装新的信号处理结构
 *
 * signum：信号数字标识
 * action：信号结构指针，如果非空，则安装新的信号处理结构
 * oldaction：信号结构指针，如果非空，则把旧的信号处理结构复制到这里
 *
 * 返回：成功为0，失败为 -1
 */
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

    // 首先验证信号数字是否有效，并且信号不能是 SIGKILL ，因为这个信号无法被捕获，否则内核可能就无法杀死进程
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;

    // 获取老的信号处理结构 
    tmp = current->sigaction[signum-1];
    // 把新的信号处理结构（用户进程数据段中）复制到当前进程的 current->sigaction[signum - 1] 处（内核数据段）  
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
    // 如果 oldaction 不为空，则把临时信号处理结构（内核数据段）复制到 oldaction（用户数据段中）
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
    // 检查 SA_NOMASK 标志是否置位
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
            current->sigaction[signum-1].sa_mask = 0; // 如果置位，处理方式和旧的signal相同，信号句柄被调用期间，不屏蔽任何信号
	else
            current->sigaction[signum-1].sa_mask |= (1<<(signum-1)); // 反之，信号句柄调用期间，则自动屏蔽 signum 对应的信号
	return 0; 
}

void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));
	}
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
