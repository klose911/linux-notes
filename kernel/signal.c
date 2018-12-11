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

/**
 * 系统调用里的中断处理程序中“信号预处理”程序(kernel/system_call.s中的ret_from_syscall标号后)
 * 这里主要的作用是为调用“信号处理句柄”准备“进程用户态下”的堆栈！
 *
 * signr: 信号数字标识
 * eax, ebx, ecx, edx, fs, es, ds, eip, cs, eflags, esp, ss 这些都是在kernel/system_call.s里的system_call标号处压入栈的，这些寄存器的值对应于进程用户态时候的寄存器的值，它们分别由以下部分组成：
 * 1. CPU执行中断指令压入的用户栈地址 ss 和 esp, 标志寄存器 eflags, 返回地址 cs 和 eip
 * 2. 刚进入system_call标号时候的 ds, es, fs, edx, ecs, ebx 寄存器值
 * 3. 中断调用返回的结果值 eax，注意：当前版本会把原始的eax值丢弃掉，后面版本会在edx后增加一个orig_eax参数
 * 4. ret_from_system_call 处压入的 signr
 *
 * 无返回值
 * 
 */
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
    // 获得进程中对应信号的处理结构指针
	struct sigaction * sa = current->sigaction + signr - 1; // struct sigaction *sa = current->sigaction[signr - 1] 
	int longs;
	unsigned long * tmp_esp;

    // 获得对应信号的处理句柄
	sa_handler = (unsigned long) sa->sa_handler;
    // 如果信号处理句柄为SIG_IGN(1, 忽略句柄)，直接返回
	if (sa_handler==1)
            return;
    // 信号处理句柄为SIG_DFL(0, 默认句柄)
	if (!sa_handler) {
            // 如果信号是 SIGCHLD ，那么也直接忽略返回
		if (signr==SIGCHLD)
			return;
        //其他情况下，则关闭进程，用对应的信号数字作为返回值
		else
			do_exit(1<<(signr-1));
	}

    // 如果该信号只需要处理一次(SA_ONESHOT)，那么把信号的句柄置空
	if (sa->sa_flags & SA_ONESHOT)
            sa->sa_handler = NULL; // 注意：马上将要调用的信号句柄已经暂存在本函数的局部变量 sa_handler 中
    /*
     * 在系统调用进入内核时，用户程序的返回地址(eip, cs) 被保存在内核栈中
     * 下面这段代码会修改内核栈上用户调用系统调用时的代码指针 eip 为“指向信号调用句柄”
     * 接着会将 sa_restorer, signr, 信号屏蔽码（如果 SA_NOMASK没有置位），eax, ecs, edx，作为参数，以及原系统调用时候的返回指针(eip 和 cs)以及标志寄存器的值一一压入进程用户级栈上
     * 因此在本次系统调用返回后，会先执行信号句柄，然后继续执行用户程序 :-) 
     */

    // 将内核态栈上用户调用系统调用的下一条代码指令指针（原eip）指向信号处理句柄
    // 注意：给 eip 变量内存处赋值必须使用 "*(&eip)" 的形式
    // 这种形式修改调用参数在C语言中无效，因为 C 语言里编译器会在函数调用完毕后会自动丢弃调用前压入栈的参数
    // 这里可以起作用是因为 do_signal() 是被汇编程序调用，需要手动清理压栈的参数！！！
    *(&eip) = sa_handler; 

    // 调用信号句柄需要压入用户态栈的参数，默认是 8 个参数
	longs = (sa->sa_flags & SA_NOMASK)?7:8; // 如果 SA_NOMASK 置位，则不需要压入调用信号句柄过程中的屏蔽位图，则只需要 7 个参数
	*(&esp) -= longs; // 将原来的用户堆栈的指针向下扩展7个或者8个长字（用来存放调用信号句柄的参数等）
    // 检查当前堆栈是否有内存超页的情况，如果有，则需要重新分配内存
	verify_area(esp,longs*4);
    
    // 在任务的用户态堆栈上依次压入调用信号句柄需要的参数
    // 注意：实际上信号调用句柄只需要一个 signr 作为参数，其他一些参数基本上都是被restorer函数调用
    // restorer 主要的作用是把系统调用后的返回值 eax, 系统调用前的 ecx, edx eflags 等出栈到寄存器中，以及是否恢复原来的进程屏蔽码
    // 在libc标准库函数中的 signal(), sigaction() 等包裹函数会使用restorer函数, 而当链接器在做用户程序链接的时候就会自动把 restorer 函数调用植入到用户程序中！ 
	tmp_esp=esp; // tmp_esp 赋值为任务的用户态堆栈指针
	put_fs_long((long) sa->sa_restorer,tmp_esp++); // 进程的用户态堆栈压入 restorer函数指针
	put_fs_long(signr,tmp_esp++); // 进程的用户态堆栈压入 signr 信号数字
	if (!(sa->sa_flags & SA_NOMASK))
            put_fs_long(current->blocked,tmp_esp++); // 进程的用户态堆栈压入调用“信号处理句柄”过程中的“信号屏蔽码”
	put_fs_long(eax,tmp_esp++); // 进程的用户态堆栈压入系统调用后的返回值 eax 
	put_fs_long(ecx,tmp_esp++); // 进程的用户态堆栈压入系统调用前 ecx 
	put_fs_long(edx,tmp_esp++); // 进程的用户态堆栈压入系统调用前 edx
	put_fs_long(eflags,tmp_esp++); // 进程的用户态堆栈压入系统调用前 eflags
    // old_eip 是信号句柄调用完毕后会执行的下一条指令！！！
	put_fs_long(old_eip,tmp_esp++); // 进程的用户态堆栈压入系统调用前的下一条指令指针 old_eip
    // 当前进程的信号阻塞码加入信号处理结构中的阻塞码
	current->blocked |= sa->sa_mask;
    // 接下去将会执行信号处理句柄
}
