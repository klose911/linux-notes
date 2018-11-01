/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */

/**
 *  页异常处理程序（中断 14），主要分两种情况处理：
 *	1. 由于缺页引起的异常，由 do_no_page(error_code, address) 处理
 *	2. 由页写保护引起的异常，由 do_wp_page(error_code, address) 处理
 *
 *   出错码 error_code 是由 CPU 自动产生并压入堆栈的，
 *   出现异常访问的线性地址是由控制寄存器 CR2 中取得的， CR2 专门用来存放页出错时的线性地址
 */ 
.globl page_fault # 声明为全局变量，将在 trap.c 中用于设置”页异常描述符“

page_fault:
	xchgl %eax,(%esp) # 交换 eax 和 (%esp) 处的内容，实际上就是从栈上取得出错码
	pushl %ecx # 参数压栈
	pushl %edx 
	push %ds
	push %es
	push %fs
	movl $0x10,%edx # 置内核段选择符
	mov %dx,%ds # ds = dx 
	mov %dx,%es # es = dx
	mov %dx,%fs # fs = dx 
	movl %cr2,%edx # 从”cr2控制寄存器“ 获得 出错的”线性地址“ 到 edx 
	pushl %edx # 调用 C 函数 参数入栈，先是出错地址，再是出错码
	pushl %eax # 注意：”参数入栈“ 的顺序 和 ”函数声明参数” 的顺序相反！！！
	testl $1,%eax # 测试“线性地址”的“页存在标志位” 是否为 1 (P == 1)，实际是置Flag寄存器的ZF位，如果 P == 1 则  ZF = 1, 反之 ZF = 0  
	jne 1f # 如果 ZF = 1 (P == 1) 则跳跃到 标号2 处 （jne 意味着 ZF = 1 时候跳跃，实际上就是 jnz）
	call do_no_page # 调用缺页处理函数 (P == 0)
	jmp 2f # 缺页处理结束
1:	call do_wp_page # 调用写保护处理函数 (P == 1) 
2:	addl $8,%esp # 丢弃压入栈的两个函数，增加栈寄存器
	pop %fs # 恢复压栈的参数
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret # 结束中断
