/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
*/

	/*
	 * system_call.s 包含了系统调用底层处理子程序。由于有些代码比较类似，所以也包含了时钟，硬盘，和软盘中断处理
	 *
	 * 注意：这段代码处理信号识别，在每次时钟中断和系统调用完毕后都会进行信号识别！
	 *      一般的中断处理是不处理信号识别，因为这往往会给系统带来混乱！ 
	 */
/*
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

	/*
	 * 从 ret_from_system_call 返回后的栈上的内容
	 */
SIG_CHLD	= 17 # SIG_CHLD 信号（子进程停止或结束）

EAX		= 0x00 # 堆栈中各个寄存器的偏移位置
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C

	# 这是任务结构(task_struct) 中属性的偏移值
state	= 0		# these are offsets into the task-struct. # 进程状态码 
counter	= 4 # 任务运行时间计数（递减）（滴答数），运行时间片
priority = 8 # 运行优先级，任务开始运行时 counter = priority ，越大则可以运行时间越长
signal	= 12 # 信号位图，每个位代表一种信号。信号值 = 位偏移值 + 1 
sigaction = 16		# MUST be 16 (=len of sigaction) # sigaction 的结构长度
blocked = (33*16) # 阻塞信号位图的偏移量

	# offsets within sigaction
	# sigaction 结构中属性的偏移量
sa_handler = 0 # 信号处理过程的句柄
sa_mask = 4 # 信号屏蔽码
sa_flags = 8 # 信号集
sa_restorer = 12 # 恢复函数指针

nr_system_calls = 72 # 系统函数调用总数

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
*/

	/*
	 * 在使用软驱时，我受到了并行打印机中断，很奇怪。呵，现在不去管它
	 */
.globl system_call,sys_fork,timer_interrupt,sys_execve
.globl hd_interrupt,floppy_interrupt,parallel_interrupt
.globl device_not_available, coprocessor_error

# 出错返回
.align 2 # 内存“4字节”对齐
bad_sys_call:
	movl $-1,%eax
	iret # 出错返回，返回值为 -1 

# 重新执行调度程序：调用功能C函数后，如果进程状态不就绪或者运行时间片用完，则跳转到这里
.align 2
reschedule:
	pushl $ret_from_sys_call # 将 ret_from_sys_call 的地址压栈，类似于把“返回地址”压栈
	jmp schedule # 跳到 /kernel/sched.c中的 schedule() 处执行。。调度程序 schedule() 返回时候就从 ret_from_system_call 处执行

#### int 0x80 -- linux 系统调用入口：eax寄存器中是调用号，ebx, ecx, edx用来传递参数	
.align 2
system_call:
	cmpl $nr_system_calls-1,%eax # 校验调用号
	ja bad_sys_call # 超出范围就出错
	push %ds # 保存 ds, es, fs 原寄存器值
	push %es
	push %fs
	# 一个系统调用最多可以有3个参数，其中ebx保存第一个，ecx存放第二个，edx存放第三个
	pushl %edx  # 系统调用参数 edx, ecx, ebx 压栈，这些是间接调用 sys_call_table 中的函数的时候作为参数
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	# 在保存过寄存器后，把ds, es 指向内核数据段，把fs指向当前局部数据段（执行本次系统调用的用户程序的数据段）
	# 注意：内核给任务分配的代码和数据段是重叠的，段基址和段限长是相同，只有可执行位不一致！！！
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	# 调用地址 = [sys_call_table + %eax * 4]
	# sys_call_table 是一个句柄（函数指针）数组，其中设置了对应的内核72个系统调用的 C 处理函数的地址
	call sys_call_table(,%eax,4) # 间接调用指定功能的 C函数
	pushl %eax # 把系统调用的返回值入栈
	movl current,%eax # 取当前任务（进程）数据结构指针 -> eax 
	cmpl $0,state(%eax)		# state 如果进程状态为0（不就绪），直接跳转到 reschedule 
	jne reschedule
	cmpl $0,counter(%eax)		# counter 如果进程运行时间片用完，直接跳转到 reschedule 
	je reschedule

	# 从系统调用C函数返回后，对信号进行识别。有些中断服务程序退出时也将跳到这里进行信号处理，才退出中断过程，比如int 16：处理器出错中断
ret_from_sys_call:
	# 首先判断当前任务是否是初始任务 task0, task0不需要处理信号
	movl current,%eax		# task[0] cannot have signals
	cmpl task,%eax # task 对应 sched.c 中的 task[] 数组，直接引用 task 相当于 task[0]
	je 3f # 如果 current == task[0] ，直接跳转到标号为”3“处 (popl %eax)
	# 通过对原调用程序代码段选择符的检查来判断是否是用户任务，如果是内核任务，则不需要处理信号
	# 因为内核态执行时不可抢占，不能进行信号处理
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ? # 比较原代码段的选择符是否为 0x000f (RPL = 3, 局部表，代码段)
	jne 3f # 如果不是，说明是原代码处在内核态，直接跳转到标号为”3“处
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ? # 比较原堆栈段的选择符是否为 0x0017 （原堆栈段不在用户段中）
	jne 3f # 如果不是，说明是原代码处在内核态，直接跳转到标号为”3“处
	movl signal(%eax),%ebx # 取信号位图 -> ebx ，每一位代表一种信号，共32个信号
	movl blocked(%eax),%ecx # 取信号屏蔽位图 -> ecx 
	notl %ecx # 每一位取反
	andl %ebx,%ecx # 获得许可的信号位图
	bsfl %ecx,%ecx # 从低位0开始扫描位图，看是否有1的位，如果有，则 ecx 保留该位的偏移值 (即地址位 0～31)
	je 3f # 如果没有信号位，直接跳转到标号为”3“处
	btrl %ecx,%ebx # 复位该信号
	movl %ebx,signal(%eax) # 重新设置信号位图
	incl %ecx # 信号值 = 地址位 + 1 （信号值是从 1 开始，而地址位从 0 开始）
	pushl %ecx # 信号值压栈，作为 do_signal 的参数之一
	call do_signal # 调用 C 函数中的信号处理过程（kernle/signal.c 中），其他参数通过开始压入栈的寄存器值来传递
	popl %eax # 弹出入栈的信号值
3:	popl %eax # 弹出系统调用的返回值，这个是在 'call sys_call_table(,%eax,4)' 后被压入栈
	popl %ebx # 依次弹出原先压栈的寄存器变量
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret # 中断返回

	#### int 16 -- 处理器错误中断。类型：错误，无出错码
	# 这是一个外部的基于硬件的异常，当协处理器检测到自己发生错误时，就会通过 ERROR 引脚通知 CPU
	# 下面代码用于处理协处理器的出错信号，并跳转执行 C 函数 math_error() (kernel/math/math_emulate.c)
	# 返回后跳转到标号 ret_from_sys_call 处继续执行
.align 2
coprocessor_error:
	push %ds # 寄存器压栈
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax # eax = $0x10 
	mov %ax,%ds # ds = $0x10  ds, es 指向内核数据段 
	mov %ax,%es # es = $0x10
	movl $0x17,%eax # eax = 0x17 
	mov %ax,%fs # fs = $0x17 fs 置为出错进程的数据段
	pushl $ret_from_sys_call # math_error 调用完毕后的执行地址压栈
	jmp math_error

	#### int 7 -- 设备不存在或协处理器不存在。类型：错误，无出错码
	# 1. 如果控制寄存器 CR0 中 EM (模拟)标志置位，则当 CPU 执行一个协处理器指令时就会引发该中断，这样 CPU 就有机会让这个中断程序模拟协处理器指令
	# 2. CR0 中的 交换标志 TS 是在 CPU 执行任务交换时设置的，可以用来确定什么时候协处理中的内容和 CPU 正在执行的任务不匹配了，
	#    当 CPU 在运行一个协处理器转移指令时，发现 TS 被置位时，就会引发该中断。此时就可以保存前一个任务的协处理内容，并恢复新任务的协处理状态
	# 中断最后将转移到 ret_from_sys_call 处执行下去（检测并处理信号）
.align 2
device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	# 清理 CR0 中的 TS 位（任务交换标志）
	clts				# clear TS so that we can use math
	movl %cr0,%eax # cr0 -> eax
	# 测试 CR0 中的 EM (模拟标志) 是否被置位
	testl $0x4,%eax			# EM (math emulation bit)
	# EM 没有置位，则恢复“协处理器”为“新任务状态”
	je math_state_restore
	# 若 EM 置位，则去执行 math_emulate (kernel/math/math_emulate.c) 
	pushl %ebp # 为 math_emulate 进行参数压栈
	pushl %esi
	pushl %edi
	call math_emulate 
	popl %edi
	popl %esi
	popl %ebp
	ret # 返回执行 ret_from_sys_call 

	#### int 32 -- (int 0x20) 时间中断处理程序
	# 中断频率设置为 100Hz(include/linux/sched.h)
	# 定时芯片 8253/8254 是在 sched.c 初始化的。因此这里 jiffies 为 10毫秒加1，这段代码将 jiffies 加1，发送结束中断指令给 8259 控制器
	# 然后用当前特权级作为参数调用 C 函数 do_timer(long CPL)。最后调用返回时检测并处理信号
.align 2
timer_interrupt:
	# 保存 ds, es 和 fs 寄存器，并把 ds, es 指向 内核数据段
	# fs 指向“被中断进程”的数据段，fs 会被 system_call 用到
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	# 下面保存 eax, ecx, edx 寄存器，因为 gcc 编译器在调用函数时不会保存它们
	# 这里也保存 ebx 寄存器，因为以后将在 ret_from_sys_call 时候使用到
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl jiffies # jiffies 增1
	# 由于初始化中断芯片的时候没有采用自动 EOI，所以这里必须手动发送指令来结束硬件中断
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax # 从堆栈中取出"执行系统调用代码"的选择符（CS段寄存器）到 eax 寄存器
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor) # 获得代码的运行级别，0: 特权级， 3: 用户级
	pushl %eax # 特权级作为 do_timer 的调用参数压栈
	# 调用 do_timer (kernel/sched.c) 中执行任务切换，计时等工作
	call do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ... # 丢弃堆栈中不再需要的“特权级参数”
	jmp ret_from_sys_call # 返回 ret_from_sys_call 

.align 2
sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call do_execve
	addl $4,%esp
	ret

.align 2
sys_fork:
	call find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call copy_process
	addl $20,%esp
1:	ret

hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
