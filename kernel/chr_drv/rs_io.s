/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */

.text
/* 对外声明串行1,串行2的中断向量处理过程函数 */	
.globl rs1_interrupt,rs2_interrupt  

	// size是读写队列缓冲区的长度，必须是2的次方，并且必须与tty_io.c中的值相同
size	= 1024				/* must be power of two !
					   and must match the value
					   in tty_io.c!!! */

	/* these are the offsets into the read/write buffer structures */
	// 以下这些是读写缓冲结构中域的偏移值 
rs_addr = 0 # 串行端口号字段偏移值（端口是0x3f8或0x2f8）
head = 4 # 缓冲区头指针字段偏移值
tail = 8 # 缓冲区尾指针字段偏移值
proc_list = 12 # 等待该缓冲区的进程字段偏移值
buf = 16 # 缓冲区数据区的字段偏移值

	// 当一个写缓冲队列满后，内核就会把往写队列填字符的进程设置为等待状态
	// 当写缓冲队列还剩下最多256个字符时，中断处理程序就可以唤醒这些等待进程继续往写队列填入字符
startup	= 256		/* chars left in write queue when we restart it */

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
*/

	/*
	 * 这些是实际的中断处理程序：程序首先检查中断的来源，然后执行相应的处理
	 * 
	 */
.align 2
	/**
	 * 串行端口1中断程序入口点
	 *
	 * 
	 */
rs1_interrupt:
	pushl $table_list+8 # tty表(tty_io.c)中串口1对应的字符缓冲队列指针地址入栈
	jmp rs_int # 跳转到rs_int继续执行
.align 2
	
	/**
	 * 串行端口2中断程序入口点
	 * 
	 */	
rs2_interrupt:
	pushl $table_list+16 # tty表(tty_io.c)中串口2对应的字符缓冲区队列指针地址入栈

	/*
	 * 串口1和串口2公用了中断处理子程序rs_int
	 * 
	 */
rs_int:
	pushl %edx # edx, ecx, ebx, eax, es, ds 依次入栈
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds		/* as this is an interrupt, we cannot */
	pushl $0x10		/* know that bs is ok. Load it */
	pop %ds # ds = 0x10（内核数据段描述符选择子） 
	pushl $0x10
	pop %es # es = 0x10（内核数据段描述符选择子） 
	movl 24(%esp),%edx # 取最开始入栈的字符缓冲队列的地址(&table_list[1] 或 & table_list[2]) -> edx 
	movl (%edx),%edx # 取读缓冲区队列结构的地址(&tty_table[1].read_q 或 &tty_table[2].read_q) -> edx 
	movl rs_addr(%edx),%edx # 取串口1或串口2的端口基地址(&read_q->data) -> edx  （0x3f8或0x2f8）
	addl $2,%edx		/* interrupt ident. reg */ # 中断标识寄存器'IIR'端口地址（0x3fa或0x2fa）

	// 中断循环判断
rep_int:
	xorl %eax,%eax # eax清零
	inb %dx,%al # 读取中断标识字节，判断中断来源
	testb $1,%al # and 1 al : 测试是否有中断，如果位0是0，表示有中断
	jne end # 无中断，跳到end退出循环
	cmpb $6,%al # al > 6 这种状态无效
	ja end # 状态无效，直接退出循环
	movl 24(%esp),%ecx # 调用子程序前，把字符缓冲队列的指针地址放入ecx
	pushl %edx # edx 临时保存中断标识寄存器IIR的端口地址
	subl $2,%edx # edx恢复为串口端口基地址

	/*
	 * 当有中断需要处理的时候, al中位0是0，位2~1标明中断来源：
	 * 11: 接收状态有错
	 * 10: 已接收到数据
	 * 01: 发送保存寄存器空
	 * 00: modem状态改变
	 *
	 * 实际上al对应的各个值是 0b110, 0b100, 0b0100, 0b000，所以这里再乘以2就可以获得中断类型表中相应子程序的地址
	 * 
	 */
	call jmp_table(,%eax,2)	# 跳转对应的子程序函数指针去执行	/* NOTE! not *4, bit0 is 0 already */
	popl %edx # 还原临时保存的中断标识寄存器的IIR端口地址
	jmp rep_int # 重新检查是否还有中断需要处理

	// 中断退出处理
end:	movb $0x20,%al # 中断控制器端口0x20
	outb %al,$0x20	# 向中断控制器发送结束中断指令	/* EOI */
	pop %ds # 依次恢复入栈的ds, es, eax, ebx, ecx, edx
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4,%esp		# jump over _table_list entry # 抛弃最初入栈的字符缓冲结构指针
	iret # 中断返回

	// 中断类型子程序处理地址跳转表
	// 0: modem状态变化
	// 4: 写字符
	// 8: 读字符
	// 12: 线路状态有错
jmp_table:
	.long modem_status,write_char,read_char,line_status

	// modem状态变化
.align 2
modem_status:
	addl $6,%edx		/* clear intr by reading modem status reg */ # [0x3f8 + 0x6 = 0x3fe] modem状态寄存器MSR(0x3fe或0x2fe)
	inb %dx,%al # 通过读MSR寄存器来进行modem复位操作
	ret

	// 线路状态出错
.align 2
line_status:
	addl $5,%edx		/* clear intr by reading line status reg. */ # [0x3f8 + 0x5 = 0x3fd] 读线路状态寄存器LSR  
	inb %dx,%al # 通过读LSR寄存器来进行读线路复位操作
	ret

	// 从串口的接收缓存寄存器中读字符
.align 2
read_char:
	inb %dx,%al # 读取接收缓冲寄存器RBR中的字符 -> al 
	movl %ecx,%edx # 当前串口读写队列地址 -> edx 
	subl $table_list,%edx # 当前串口读写队列地址 - 缓冲队列数组表首地址 -> edx 
	shrl $3,%edx # 差值 / 8 可以获得窗口号，结果1对应串口1, 结果2对应窗口2 
	movl (%ecx),%ecx # 取读缓冲区队列结构的地址 -> ecx
	movl head(%ecx),%ebx # 取读队列头指针 -> ebx
	// 这里的逻辑和PUTCH(c,queue)一样，只是用汇编实现
	movb %al,buf(%ecx,%ebx) # 将字符放入读队列缓冲区的头指针处
	incl %ebx # 读队列头指针 + 1 
	andl $size-1,%ebx # 用读队列长度对头指针做取模操作 -> ebx 
	cmpl tail(%ecx),%ebx # 读队列头指针与尾指针做比较
	je 1f # 如果两者相等，表示读队列缓冲区已满，不保存头指针，直接跳转到1标号处
	movl %ebx,head(%ecx) # 保存修改过的头指针
1:	pushl %edx # edx入栈（串口号：标识终端）
	call do_tty_interrupt # 调用do_tty_interrupt：实际上是copy_to_cooked()，把读入的字符放入到规范模式缓冲队列中
	addl $4,%esp # 丢弃入栈参数
	ret # 返回到rep_int处

	// 从写缓冲队列中写字符到串口发送寄存器：
	// 由于设置了发送保存寄存器允许此中断标志，说明对应的串行终端写缓存队列中有字符需要发送
.align 2
write_char:
	movl 4(%ecx),%ecx		# write-queue // 取写缓冲队列结构地址 -> ecx 
	movl head(%ecx),%ebx # 取写队列头指针 -> ebx 
	subl tail(%ecx),%ebx # 计算尾指针 - 头指针 -> ebx
	andl $size-1,%ebx		# nr chars in queue // size -1 & ebx：如果结果为0，则“尾指针==头指针”，因此写队列为空
	je write_buffer_empty # 写队列为空，跳转到write_buffer_empty处理
	cmpl $startup,%ebx # 比较队列中字符是否超过256个
	ja 1f # 超过256个字符，跳转到标号1执行
	movl proc_list(%ecx),%ebx	# wake up sleeping process // 等待终端的进程结构指针地址 -> ebx：少于256个字符，唤醒写终端的进程！
	testl %ebx,%ebx			# is there any? // 测试是否存在等待终端写的进程
	je 1f # 没有等待终端写的进程，跳转到标号1处执行
	movl $0,(%ebx) # 设置等待进程的状态为可执行状态(0)，注意：可执行状态是进程结构的第一个字段，因此可以用(%ebx)来表示
	// 这段逻辑可以和GETCH宏做比较
1:	movl tail(%ecx),%ebx # 取尾指针 -> ebx 
	movb buf(%ecx,%ebx),%al # 从写队列的数据缓冲区取一个字符 -> al 
	outb %al,%dx # 发送要写的字符到发送保存寄存器（端口0x3f8或0x2f8）
	incl %ebx # 尾指针 + 1
	andl $size-1,%ebx # size - 1 & 尾指针 -> ebx 
	movl %ebx,tail(%ecx) # 保存已经修改过的尾指针
	cmpl head(%ecx),%ebx # 再次比较头指针和修改过的尾指针
	je write_buffer_empty # 如果两者相同，则跳转到write_buffer_empty（处理写队列为空的情况）
	ret
	
	// 写队列为空：
	// 1. 唤醒等待终端写的进程
	// 2. 暂时禁止发送保存寄存器THR空时发出中断
.align 2
write_buffer_empty:
	movl proc_list(%ecx),%ebx	# wake up sleeping process
	testl %ebx,%ebx			# is there any?
	je 1f
	movl $0,(%ebx) // 这段逻辑和上面一样
1:	incl %edx // 串行端口基地址 + 1 : 中断允许寄存器IER(0x3f9或0x2f9)
	inb %dx,%al　// 读取中断允许寄存器的状态字 -> al 
	jmp 1f　// 空指令
1:	jmp 1f // 空指令
1:	andb $0xd,%al	// 位1设置为0：禁止发送保存寄存器THR空时发出中断　/* disable transmit interrupt */
	outb %al,%dx // 写入中断允许寄存器IER
	ret
