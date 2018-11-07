/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

/*
 * asm.s 包含大部分的硬件故障（或出错）处理的低层次代码
 * 页异常由 mm/page.s 处理，所以不在这里
*/

	# 本代码主要针对 intel 保留中断 int 0 ~ int16 的处理 （int 17 ~ int 31 留作今后使用）
	# 以下是一些全局函数名的声明，其原型在 trap.c 中声明
.globl divide_error,debug,nmi,int3,overflow,bounds,invalid_op
.globl double_fault,coprocessor_segment_overrun
.globl invalid_TSS,segment_not_present,stack_segment
.globl general_protection,coprocessor_error,irq13,reserved

	#int 0 -- 处理被零除出错的情况。 类型：错误，出错号：无
	# 在执行 div 或 idiv 指令时，若除数是 0，CPU就会产生这个异常
	# 当 EAX(AX, AL) 包含不了一个合法除操作结果时， 也会产生这个异常
	# 标号 '$do_divide_error' 实际上是C语言函数do_divide_error()编译后生成的模块中的对应的名称，
	# 函数 ''do_divide_error' 在 traps.c 中实现
divide_error:
	pushl $do_divide_error # 首先把将要调用的函数地址入栈，这里的栈是用户进程的内核态的堆栈，不是用户进程的用户态使用的堆栈！！！
	# 下面这段程序统一处理无出错号的情况
	# 注意，参数入栈，栈顶指针 esp 减小！！！
	#      参数出栈，栈顶指针 esp 增加！！！ 
no_error_code:
	# 注意：用户进程的用户态下的原SS, 原ESP, 原 eflags, 原 cs, 原 eip 已经被保存在用户进程的内核栈上！！！
	xchgl %eax,(%esp) # _do_divide_error -> eax, 原eax 被交换入栈
	pushl %ebx # ebx, ecx, edx, edi, esi, ebp 依次入栈（这些寄存器都是用户进程的内核态时候的值）
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds # !! 16位的段寄存器入栈也要占用4个字节
	push %es # ds, es, fs 依次入栈
	push %fs
	# do_divide_error(long esp, long error_code) 下面依次把 error_code 和 esp 作为调用的参数入栈
	# 注意：参数入栈的顺序是和参数声明的顺序正好相反 !!! 
	pushl $0		# "error code" 将数值 0 作为错误码入栈 (error_code 参数入栈)
	# lea 相当于 C 语言中的 & ，取地址的意思
	lea 44(%esp),%edx # 把 (esp + 44) 的地址加载到 edx 寄存器，这个地址是“中断结束后返回的地址”     
	pushl %edx # 中断返回后的地址，压入堆栈 (esp0 参数入栈) 
	movl $0x10,%edx # edx = 0x10 (内核数据段选择符号)
	mov %dx,%ds # ds = 0x10 
	mov %dx,%es # es = 0x10 
	mov %dx,%fs # fs = 0x10
	# * 类似于 C 语言的 获取指针内容的含义
	call *%eax # 把 eax 中的内容作为函数调用的地址，实际上就是调用 do_divide_error(esp, error_code)
	addl $8,%esp # 调用完毕后，前面入栈的两个参数 error_code 和 esp 已经没用，直接抛弃掉，栈顶指针 + 8 
	pop %fs # 依次恢复入栈的 fs, es, ds 寄存器
	pop %es
	pop %ds
	popl %ebp # 依次恢复入栈的 ebp, esi, edi, edx, ecx, ebx, eax 寄存器
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret # 中断返回，最初保存的eip, cs, eflags, esp, ss 依次出栈恢复
	# 注意这里不仅有特权级变化，还有堆栈变化（用户进程内核态的内核堆栈 -> 用户进程的用户态堆栈）！！！

	# int 1 -- debug 调试中断入口。类型：错误/陷阱，出错号：无
	# 当 eflags 的 TF 位置位时引发的中断：
	# 当发现硬件断点，或 开启了指令跟踪陷阱，或 任务交换陷阱，或 调试器访问寄存器无效（错误） 
debug:
	pushl $do_int3		# _do_debug
	jmp no_error_code

	# int 2 -- 非屏蔽中断调用入口。类型：陷阱，错误号：无
	# 仅有的被赋予固定中断号的硬件中断。每当接受到一个 nmi 信号，CPU立刻产生中断向量 2，并执行相应的中断过程，因此很节约时间
	# NMI 通常保留为极为重要的硬件事件使用，当收到并且执行中断处理过程后，会忽略所有其后的硬件中断
nmi:
	pushl $do_nmi
	jmp no_error_code

	# int 3 -- 断点指令引发的中断。类型：陷阱，错误号：无
	# 调试器插入被调试程序的代码中
int3:
	pushl $do_int3
	jmp no_error_code

	# int 4 -- 溢出出错中断处理入口。类型：陷阱，错误号：无
	# EFLAGS 的 OF 位被置位时 CPU 执行 INTO 指令时候触发
	# 通常被用于编译器跟踪算术计算溢出
overflow:
	pushl $do_overflow
	jmp no_error_code

	# int 5 -- 边界检查出错中断入口。类型：错误，错误号：无
	# 当操作数在有效范围以外时候触发的中断，当 BOUND 指令测试失败则会触发
	# BOUND 指令有 3个操作数，如果第一个不在后两个之间，被认为测试失败
bounds:
	pushl $do_bounds
	jmp no_error_code

	# int 6 -- 无效指令出错中断入口。类型：错误，错误号：无
	# CPU 检测到一个无效的操作码
invalid_op:
	pushl $do_invalid_op
	jmp no_error_code

	# int 7 -- 协处理器段超出中断入口。类型：错误，错误号：无
	# 等同于协处理器出错保护，在浮点指令操作过大时，有机会来加载或保存超出数据段的浮点值
coprocessor_segment_overrun:
	pushl $do_coprocessor_segment_overrun
	jmp no_error_code

	# int 15 -- 保留中断的入口。类型：无，错误号：无
	# 保留给未来使用
reserved:
	pushl $do_reserved
	jmp no_error_code

	# int 45(0x20 + 13) -- 数学协处理器硬件中断
	# 80387 在执行计算时，CPU 会等待其计算完成
	# 当协处理器执行完一个操作时，就会发出 IRQ 13 中断信号，以通知 CPU 操作完成。
irq13:
	pushl %eax 
	xorb %al,%al # 0xF0 是协处理器端口，用于清忙锁存器。通过写端口，消除 CPU 的 BUSY 延续信号，并重新激活 80387 的扩展请求引脚 PEREQ
	outb %al,$0xF0 # 该操作主要是为了确保再继续执行 80387 的任何指令前， CPU 响应本中断
	movb $0x20,%al
	outb %al,$0x20 # 向 8259 主中断控制芯片发送 EOI （中断结束） 信号
	jmp 1f # 这两个跳转指令起延时作用
1:	jmp 1f
1:	outb %al,$0xA0 # 再向 8259 从中断控制芯片发送 EOI （中断结束） 信号
	popl %eax
	jmp coprocessor_error # 该函数现在在 system_call.s 中

	# int 8 -- 双出错故障。类型：放弃，出错号：有
	# 通常当 CPU 在调用前一个异常的处理程序过程中又检测到一个新的异常时，一般这两个异常会被串行执行
	# 但也有很少的情况，CPU 无法进行串行执行，这时候就会触发这个异常
double_fault:
	pushl $do_double_fault
	# 以下中断在调用时 CPU 会在中断返回地址之后，再将出错号压入堆栈，因此返回时候也要将出错号弹出
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax # eax 原来的值被保存在堆栈，error code 被交换到 eax 寄存器
	xchgl %ebx,(%esp)		# &function <-> %ebx # ebx 原来的值被保存在堆栈，中断调用地址被保存到 ebx 寄存器
	pushl %ecx # ecx, edx, edi, esi, ebp 依次压栈
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds # ds, es, fs 依次压栈
	push %es
	push %fs
	pushl %eax			# error code # error code 压栈（调用参数）
	lea 44(%esp),%eax		# offset # 原来的 esp 地址 载入到 eax 寄存器
	pushl %eax # 中断返回后的地址，压入堆栈 (esp0 参数入栈) 
	movl $0x10,%eax # eax = 0x10 （内核段数据选择符）
	mov %ax,%ds # ds = ax 
	mov %ax,%es # es = ax 
	mov %ax,%fs # fs = ax 
	call *%ebx # 调用 do_double_fault(long esp, long error_code)
	addl $8,%esp # 调用完毕后，前面入栈的两个参数 error_code 和 esp 已经没用，直接抛弃掉，栈顶指针 + 8
	pop %fs # 依次恢复入栈的 fs, es, ds 寄存器
	pop %es
	pop %ds
	popl %ebp # 依次恢复入栈的 ebp, esi, edi, edx, ecx, ebx, eax 寄存器
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret # 中断返回，最初保存的eip, cs, eflags, esp, ss 依次出栈恢复
	# 注意这里不仅有特权级变化，还有堆栈变化（用户进程内核态的内核堆栈 -> 用户进程的用户态堆栈）！！！

	# int 10 -- 无效的任务状态段 TSS 。类型：错误，出错号：有
	# CPU 企图换到一个进程，而该进程的 TSS 无效
	# 当由于 TSS 长度超过 104 字节时，这个异常在当前任务中产生，导致切换被终止，反之则在切换后的新任务中产生该异常
invalid_TSS:
	pushl $do_invalid_TSS
	jmp error_code

	# int 11 -- 段不存在。类型：错误，出错号：有
	# 被引用的段不存在。段描述符中的标志着段不在内存中
segment_not_present:
	pushl $do_segment_not_present
	jmp error_code

	# int 12 -- 堆栈段错误。类型：错误，出错号：有
	# 指令操作试图超越堆栈段范围，或者堆栈段不在内存中
	# 这是异常 11 和 13 的特例，有些操作系统可以利用这个异常来确定什么时候需要为程序分配更多的栈空间
stack_segment:
	pushl $do_stack_segment
	jmp error_code

	# int 13 -- 一般保护性出错。类型：错误，出错号：有
	# 若一个异常出现时没有对应的处理向量(0 ~ 16)，通常就会归为此类
general_protection:
	pushl $do_general_protection
	jmp error_code

