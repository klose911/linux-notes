/*
 *  linux/kernel/keyboard.S
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	Thanks to Alfred Leung for US keyboard patches
 *		Wolfgang Thiel for German keyboard patches
 *		Marc Corsini for the French keyboard
 */

#include <linux/config.h>

.text

# 声明keyboard_interrupt函数，初始化时设置键盘中断描述符
.globl keyboard_interrupt 

/*
 * these are for the keyboard read functions
	*/

# tty_queue队列缓存长度
size	= 1024		/* must be a power of two ! And MUST be the same
			   as in tty_io.c !!!! */
# tty_queue的head域在tty_queue结构中的偏移
head = 4
# tty_queue的tail域在tty_queue结构中的偏移	
tail = 8
# tty_queue的proc_list域在tty_queue结构中的偏移	
proc_list = 12
# tty_queue的buf域在tty_queue结构中的偏移	
buf = 16

	/*
	 * 这里使用3个标志字节：mode是键盘特殊键（ctrl，alt或caps等）按下的标志，leds是键盘指示灯的状态标志，e0是当收到扫描码0xe0或0xe1时设置的标志
	 *
	 * 1) mode 各位的含义如下：
	 * 位7：caps键按下，位6：caps键的状态（与leds中对应caps的标志位一样），位5：右alt键按下，位4：左alt键按下
	 * 位3：右ctrl键按下，位2：左ctrl键按下，位1：右shift键按下，位0：左shift键按下
	 *
	 * 2) leds 各位的含义如下：
	 * 位7～3：全0不用，位2：caps-lock，位1：num-lock（初始设置为1），位0：scroll-lock
	 *
	 * 3) e0 各位的含义如下：
	 * 位７~2：全0不用
	 * 位1:值为1意味收到0xe1扫描码的标志，表示后面还跟着2个字符
	 * 位0:值为1意味收到0xe0扫描码的标志，表示后面还跟着1个字符
	 * 
	 */
mode:	.byte 0		/* caps, alt, ctrl and shift mode */
leds:	.byte 2		/* num-lock, caps, scroll-lock mode (nom-lock on) */
e0:	.byte 0

/*
 *  con_int is the real interrupt routine that reads the
 *  keyboard scan-code and converts it into the appropriate
 *  ascii character(s).
	*/

	/*
	 * 键盘中断处理程序入口点
	 * 
	 * 接收到用户的一个按键操作，就会向中断控制器发出一个键盘中断请求给键盘控制器IRQ1。当CPU响应该请求时就会执行键盘中断请求处理程序
	 * 本处理程序会从键盘控制器相应端口(0x60)读入按键扫描码，并调用对应的扫描码处理子程序进行处理：
	 * 1. 从0x60端口读入按键的扫描码
	 * 2. 判断该扫描码是否是0xe0或0xe1:
	 *    如果是的化，立刻对键盘控制器作出应答，并向中断控制器发出中断结束(EOI)信号，以允许键盘控制器能继续产生中断信号，从而接收后续的字符	 *    
	 * 3. 如果不是0xe0或0xe1的化，根据扫描码的值调用按键跳转表key_table中的相应按键处理子程序，把扫描码对应的字符放入到读字符缓冲队列read_q中
	 * 4. 对键盘控制器作出应答，并发送EOI信号
	 * 5. 调用函数do_tty_interrupt()把read_q中的字符经过处理后放到secodary辅助队列中
	 * 
	 */
keyboard_interrupt:
	pushl %eax # eax, ebx, ecx, edx寄存器压栈
	pushl %ebx
	pushl %ecx
	pushl %edx
	push %ds # ds, es 寄存器压栈
	push %es
	movl $0x10,%eax # eax=0x10 
	mov %ax,%ds # es, ds 都设置为0x10，也就是内核数据段
	mov %ax,%es
	xor %al,%al	# 清空al	/* %eax is scan code */ 
	inb $0x60,%al # 从端口0x60读取扫描码到al寄存器
	cmpb $0xe0,%al # 比较al和0xe0是否相等
	je set_e0 # 如果相同，则跳转到set_e0标号处的代码
	cmpb $0xe1,%al # 比较al和0xe0是否相等
	je set_e1 # 如果相同，则跳转到set_e1标号处的代码
	# 调用键盘处理程序表(key_table+eax * 4)处的子程序
	call key_table(,%eax,4) 
	movb $0,e0 # 调用之后e0标志复位
	/*
	 * 下面这段代码针对使用8255A的PC标准键盘电路进行硬件复位处理
	 * 
	 * 1. 禁止键盘
	 * 2. 立刻重新允许键盘
	 * 
	 */
e0_e1:	inb $0x61,%al # 读取0x61端口到al，端口0x61是8255A的输出端口B的地址，该输出端口的第7位(PB7)用于禁止和允许对键盘数据的处理
	jmp 1f # 延迟一会
1:	jmp 1f
1:	orb $0x80,%al # a1的第7位置位（禁止键盘工作） 
	jmp 1f # 延迟一会
1:	jmp 1f
1:	outb %al,$0x61 # PPI PB7置位
	jmp 1f # 延迟一会
1:	jmp 1f 
1:	andb $0x7F,%al # a1的第7位复位（允许键盘工作）
	outb %al,$0x61
	movb $0x20,%al # PPI PB7复位
	outb %al,$0x20 # 向8259中断芯片发送EOI（中断结束）信号
	pushl $0 # 控制台号 tty=0作为参数入栈
	call do_tty_interrupt # 调用do_tty_interrupt函数（具体功能见tty_io.c中代码） 
	addl $4,%esp # 丢弃刚才入栈的参数
	pop %es # 弹出开始入栈的es，ds寄存器
	pop %ds 
	popl %edx # 依次恢复开始入栈的edx, ecx, ebx, eax寄存器的值
	popl %ecx
	popl %ebx
	popl %eax
	iret # 键盘中断处理返回
set_e0:	movb $1,e0 # 设置e0标志位为1
	jmp e0_e1 # 跳转到e0_e1标记处代码
set_e1:	movb $2,e0 # 设置e0标志位为2
	jmp e0_e1 # 跳转到e0_e1标记处代码

/*
 * This routine fills the buffer with max 8 bytes, taken from
 * %ebx:%eax. (%edx is high). The bytes are written in the
 * order %al,%ah,%eal,%eah,%bl,%bh ... until %eax is zero.
	*/

	/*
	 * 下面子程序把从ebx:eax中的最多8个字符放入到读缓冲队列中，（ebx是高字）字符依次放入的顺序是 al,ah,eal,eah,bl,bh,ebl,ebh 直到eax为空
	 * 
	 */
put_queue:
	pushl %ecx # ecx, edx依次入栈 
	pushl %edx
	# 把table_list变量地址处的值（实际上是&tty_table[0].read_q， 也就是“控制台”读队列的地址）放入到edx寄存器
	movl table_list,%edx		# read-queue for console 
	movl head(%edx),%ecx # 把控制台read_q结构里的head指针值放入到 ecx 
1:	movb %al,buf(%edx,%ecx) # 将al寄存器的值放入到read_q->buf[read_q->head]处
	incl %ecx # ecx + 1 
	andl $size-1,%ecx # 调整ecx的值，如果溢出，则回到开始处(0x0111111111 & 0x1000000000 = 0)  
	cmpl tail(%edx),%ecx		# buffer full - discard everything # 比较tail指针值和exc寄存器中的值是否一样
	je 3f # 一样，表示缓冲区已经满了，则跳转标号3处的代码：丢弃所有的扫描码
	shrdl $8,%ebx,%eax # 将ebx中的8个字符右移8位到eax中，ebx保持不变
	je 2f # ebx和eax是否一样，如果一样表示已经没有多余的字符了，跳转到标号2处的代码
	shrl $8,%ebx # ebx右移8位
	jmp 1b # 跳转到标号1，继续操作（总共可能最多会有8次读取al的操作）
2:	movl %ecx,head(%edx) # ecx寄存器中的值写入到read_q的head域 
	movl proc_list(%edx),%ecx # ecx 设置为read_q -> proc_list 
	testl %ecx,%ecx # ecx是否为0,
	je 3f #如果为0(NULL),表示没有等待该控制台的进程，直接跳转到标号3处
	# 注意：每个进程的第一个字段就是进程状态，因此这里可以用 (%ecx) 表示进程的状态字段
	movl $0,(%ecx) # 唤醒该等待的进程（直接置改进成的状态为0：就绪状态）
3:	popl %edx # 依次恢复入栈的edx,ecx的值
	popl %ecx
	ret # 子程序返回

	/**
	 * 下面这段代码根据ctrl或alt的扫描码，分别设置mode标志中的相应位
	 * 
	 * 如果在该扫描码前收到过0xe0扫描码（e0标志置位），则说明按下的是键盘右边的ctrl或alt键，则对应设置ctrl或alt在模式标志mode中的相应位
	 *
	 */
ctrl:	movb $0x04,%al # 0x04是mode标志中左ctrl键对应的位（位2）
	jmp 1f # 跳转到下面标号1处的代码
alt:	movb $0x10,%al # 0x10是mode标志中左alt键对应的位（位4）
1:	cmpb $0,e0 # e0是否为0
	je 2f # e0没有置位，则跳转到下面标号2处的代码
	# 0x04 + 0x04 = 0x08（位3），0x10 + 0x10 = 0x20（位5）
	addb %al,%al # e0已经置位，则按下的是右ctrl或右alt，设置相应右键ctrl或alt的标志位（位3或位5） 
2:	orb %al,mode # 设置mode中对应的位
	ret

	/**
	 * 处理ctrl或alt的松开时的扫描码，分别复位mode标志中的相应位
	 * 
	 * 如果在该扫描码前收到过0xe0扫描码（e0标志置位），则说明按下的是键盘右边的ctrl或alt键，则对应复位ctrl或alt在模式标志mode中的相应位
	 * 
	 */
unctrl:	movb $0x04,%al # 0x04是mode标志中左ctrl键对应的位（位2）
	jmp 1f # 跳转到下面标号1处的代码
unalt:	movb $0x10,%al # 0x10是mode标志中左alt键对应的位（位4）
1:	cmpb $0,e0 # e0是否为0
	je 2f # e0没有置位，则跳转到下面标号2处的代码
	addb %al,%al # e0已经置位，则按下的是右ctrl或右alt，复位相应右键ctrl或alt的标志位（位3或位5）
2:	notb %al # 复位标志中相应的标志位
	andb %al,mode 
	ret

	/*
	 * 处理左shift，按下和松开时的扫描码，分别置位和复位 mode标志中相应位
	 * 
	 */
lshift:
	orb $0x01,mode # 左shift按下，设置mode中的位0
	ret
unlshift:
	andb $0xfe,mode # 左shift松开，复位mode中的位0
	ret
	
	/*
	 * 处理右shift，按下和松开时的扫描码，分别置位和复位 mode标志中相应位
	 * 
	 */
rshift:
	orb $0x02,mode # 右shift按下，设置mode中的位1
	ret
unrshift:
	andb $0xfd,mode # 右shift松开，复位mode中的位1
	ret

	/*
	 * 收到caps键扫描码的处理
	 * 
	 * 通过mode中的第7位可以知道当前caps键是否已经按下的状态：
	 * 若是，则返回
	 * 否则就翻转mode中的caps键按下的（位6）和leds标志中caps-lock（位2），设置mode标志中caps已经按下的标志（位7）
	 * 
	 */
caps:	testb $0x80,mode # 测试mode中第7位是否已经按下（即caps是否在按下状态）
	jne 1f # caps已经按下，直接跳转到下面的标号1处（curs2后面的1）
	xorb $4,leds # 翻转leds标志中的位2（caps-lock位）
	xorb $0x40,mode # 翻转mode中的位6（caps位）
	orb $0x80,mode # 设置mode中的位7（caps已经按下状态位）

	/*
	 * 根据leds标志位，关闭或打开键盘指示灯
	 * 
	 */
set_leds:
	call kb_wait # 等待键盘输入缓存空
	movb $0xed,%al		/* set leds command */
	outb %al,$0x60 # 发送键盘命令0xed到0x60端口
	call kb_wait # 等待键盘输入缓存空
	movb leds,%al # 设置al为leds标志
	outb %al,$0x60 # 向0x60端口发送leds标志（开启或关闭指示灯的命令参数）
	ret

	/*
	 * 松开caps键扫描码
	 * 
	 */
uncaps:	andb $0x7f,mode # 复位mode中的位7
	ret

	/*
	 * 按下scroll键的扫描码
	 * 
	 */
scroll:
	xorb $1,leds # 翻转leds标志中的位0
	jmp set_leds # 关闭或打开键盘指示灯

	/*
	 * 按下num键的扫描码
	 * 
	 */
num:	xorb $2,leds # 翻转leds标志中的位1 
	jmp set_leds # 关闭或打开键盘指示灯

/*
 *  curosr-key/numeric keypad cursor keys are handled here.
 *  checking for numeric keypad etc.
	*/

	/*
	 * 处理方向键/数字小键盘上的方向键/数字键
	 * 
	 */
cursor:
	subb $0x47,%al # al - 0x47
	# 扫描码如果是数字小键盘上的键，其扫描码 >= 0x47 ? 
	jb 1f # 扫描码 <= 0x47，不处理
	# 0x53 - 0x47 = 12 
	cmpb $12,%al # 比较12和al
	ja 1f # 扫描码 > 0x53，同样不处理
	jne cur2 # 不等于12，转而执行cur2处的代码
	# 12对应于del键盘，那么需要检查是否是ctrl+alt+del
	testb $0x0c,mode # 检查mode中的位2和位3，如果置位说明ctrl被按下
	je cur2 # 没有按下ctrl，跳转执行cur2处的代码
	testb $0x30,mode # 检查mode中的位4和位5，如果置位说明alt被按下
	jne reboot # 按下了alt（实际上按下了ctrl+alt+del），执行重启函数reboot
	# e0标志被置位了吗？
cur2:	cmpb $0x01,e0		/* e0 forces cursor movement */ 
	je cur # e0置位则跳转到cur处执行（光标移动处理）
	# e0没有置位，测试leds的num-lock是否置位
	testb $0x02,leds	/* not num-lock forces cursor */ 
	je cur # 没有置位，也跳转到cur处执行
	# 测试mode中的位0和位1是否置位，置位说明shift被按下
	testb $0x03,mode	/* shift forces cursor */
	jne cur # shift没有被置位，跳转到cur处执行
	xorl %ebx,%ebx # 清空ebx
	movb num_table(%eax),%al # 以eax为索引，取对应数字字符到al： num_table[%eax] -> al
	# 注意，由于要放入的字符数 <= 4，所以必须把ebx清零
	jmp put_queue # 把al中的字符放入读缓冲队列
1:	ret

	/*
	 * 处理光标移动或插入删除按键
	 * 
	 */
cur:	movb cur_table(%eax),%al # 以eax为索引，取光标字符表中相应键的代表字符到al：cur_table[%eax] -> al 
	cmpb $'9,%al # 如果字符 <= '9' (5,6,2,3)，说明是上一页，下一页，插入，删除键，则功能字符序列中要添加'~'
	ja ok_cur # 非功能键，跳转到ok_cur处执行
	movb $'~,%ah # 上一页，下一页，插入，删除键，则功能字符序列中要添加'~'
ok_cur:	shll $16,%eax # 将ax中的内容移到eax的高字处
	movw $0x5b1b,%ax # 把'esc['放入到ax中，与eaxo高字中字符组成移动序列 
	xorl %ebx,%ebx # ebx清零
	jmp put_queue # 把eax中的移动序列放入读缓冲队列

	# 数字小键盘上对应的数字的ASCII码表
#if defined(KBD_FR)
num_table:
	.ascii "789 456 1230."
#else
num_table:
	.ascii "789 456 1230,"
#endif
	# 小键盘上方向键或插入，删除键等对应的移动表示字符表
cur_table:
	.ascii "HA5 DGC YB623"

/*
 * this routine handles function keys
	*/

	/*
	 * 处理功能键F1~F12
	 */
func:
	# 调用显示状态show_stat函数
	pushl %eax
	pushl %ecx
	pushl %edx
	call show_stat 
	popl %edx
	popl %ecx
	popl %eax
	subb $0x3B,%al # al中放入功能键的索引号（键'F1'的扫描码是0x3B）
	jb end_func # 如果小于0x3B，则不处理直接返回
	cmpb $9,%al # 功能键是否是F1~F10
	jbe ok_func # 是的：执行ok_func处的代码
	subb $18,%al # F11,F12的扫描码是0x57,0x58(0x57-0x3B=0x1c=28，这里为了获得索引所以只减去18) 
	cmpb $10,%al # al是否等于10 
	jb end_func # 等于10：是功能键F11
	cmpb $11,%al # al是否等于11
	ja end_func # 等于11：是功能键F12
ok_func:
	cmpl $4,%ecx		/* check that there is enough room */ #检查空间
	jl end_func # 需要放入4个字符，如果放不下，则返回？？？
	movl func_table(,%eax,4),%eax # 取功能键对应的字符序列，放入到eax中
	xorl %ebx,%ebx # 清空ebx 
	jmp put_queue # 把eax中的ASCII码序列放入读缓冲队列
end_func:
	ret

/*
 * function keys send F1:'esc [ [ A' F2:'esc [ [ B' etc.
*/
	
	/*
	 * 功能键F1~F12对应的发送到缓冲队列中的字符序列（ASCII码）
	 * F1:'esc [ [ A'，F2:'esc [ [ B', ... , F12:'esc [ [ L'
	 *
	 * 注意：下面表示的是对应字符的ASCII码，而且顺序和发送的正好相反
	 * 比如,'A'的ASCII码是0x41, '['的ASCII码是0x5b，'esc'的ASCII码是0x1b
	 * 
 	 */
func_table:
	.long 0x415b5b1b,0x425b5b1b,0x435b5b1b,0x445b5b1b
	.long 0x455b5b1b,0x465b5b1b,0x475b5b1b,0x485b5b1b
	.long 0x495b5b1b,0x4a5b5b1b,0x4b5b5b1b,0x4c5b5b1b

	/*
	 * 扫描码对应的ASCII码映射表：将相应的扫描码映射成ASCII码
	 * 
	 */
#if	defined(KBD_FINNISH)
key_map:
	.byte 0,27
	.ascii "1234567890+'"
	.byte 127,9
	.ascii "qwertyuiop}"
	.byte 0,13,0
	.ascii "asdfghjkl|{"
	.byte 0,0
	.ascii "'zxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTYUIOP]^"
	.byte 13,0
	.ascii "ASDFGHJKL\\["
	.byte 0,0
	.ascii "*ZXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#elif defined(KBD_US) # US键盘

key_map:
	.byte 0,27 # 扫描码 0x00(0:NUL),0x01(27:Esc)对应的ASCII码
	.ascii "1234567890-=" # 扫描码 0x02,0x03...0x0c,0x0d对应的ASCII码
	.byte 127,9 
	.ascii "qwertyuiop[]"
	.byte 13,0 
	.ascii "asdfghjkl;'"
	.byte '`,0
	.ascii "\\zxcvbnm,./"
	.byte 0,'*,0,32		/* 36-39 */ # 扫描码 0x36～0x39对应的ASCII码
	.fill 16,1,0		/* 3A-49 */ # 扫描码 0x3A～0x49对应的ASCII码
	.byte '-,0,0,0,'+	/* 4A-4E */ # 扫描码 0x4A～0x4E对应的ASCII码
	.byte 0,0,0,0,0,0,0	/* 4F-55 */ # 扫描码 0x36～0x39对应的ASCII码
	.byte '<
	.fill 10,1,0


shift_map: # Shift键同时按下时候的扫描码
	.byte 0,27
	.ascii "!@#$%^&*()_+"
	.byte 127,9
	.ascii "QWERTYUIOP{}"
	.byte 13,0
	.ascii "ASDFGHJKL:\""
	.byte '~,0
	.ascii "|ZXCVBNM<>?"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map: # Alt键同时按下时对应的扫描码
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#elif defined(KBD_GR)

key_map:
	.byte 0,27
	.ascii "1234567890\\'"
	.byte 127,9
	.ascii "qwertzuiop@+"
	.byte 13,0
	.ascii "asdfghjkl[]^"
	.byte 0,'#
	.ascii "yxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTZUIOP\\*"
	.byte 13,0
	.ascii "ASDFGHJKL{}~"
	.byte 0,''
	.ascii "YXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0


#elif defined(KBD_FR)

key_map:
	.byte 0,27
	.ascii "&{\"'(-}_/@)="
	.byte 127,9
	.ascii "azertyuiop^$"
	.byte 13,0
	.ascii "qsdfghjklm|"
	.byte '`,0,42		/* coin sup gauche, don't know, [*|mu] */
	.ascii "wxcvbn,;:!"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "1234567890]+"
	.byte 127,9
	.ascii "AZERTYUIOP<>"
	.byte 13,0
	.ascii "QSDFGHJKLM%"
	.byte '~,0,'#
	.ascii "WXCVBN?./\\"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0~#{[|`\\^@]}"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#else
#error "KBD-type not defined"
#endif
/*
 * do_self handles "normal" keys, ie keys that don't change meaning
 * and which have just one character returns.
 */
do_self:
	lea alt_map,%ebx
	testb $0x20,mode		/* alt-gr */
	jne 1f
	lea shift_map,%ebx
	testb $0x03,mode
	jne 1f
	lea key_map,%ebx
1:	movb (%ebx,%eax),%al
	orb %al,%al
	je none
	testb $0x4c,mode		/* ctrl or caps */
	je 2f
	cmpb $'a,%al
	jb 2f
	cmpb $'},%al
	ja 2f
	subb $32,%al
2:	testb $0x0c,mode		/* ctrl */
	je 3f
	cmpb $64,%al
	jb 3f
	cmpb $64+32,%al
	jae 3f
	subb $64,%al
3:	testb $0x10,mode		/* left alt */
	je 4f
	orb $0x80,%al
4:	andl $0xff,%eax
	xorl %ebx,%ebx
	call put_queue
none:	ret

/*
 * minus has a routine of it's own, as a 'E0h' before
 * the scan code for minus means that the numeric keypad
 * slash was pushed.
 */
minus:	cmpb $1,e0
	jne do_self
	movl $'/,%eax
	xorl %ebx,%ebx
	jmp put_queue

/*
 * This table decides which routine to call when a scan-code has been
 * gotten. Most routines just call do_self, or none, depending if
 * they are make or break.
 */
key_table:
	.long none,do_self,do_self,do_self	/* 00-03 s0 esc 1 2 */
	.long do_self,do_self,do_self,do_self	/* 04-07 3 4 5 6 */
	.long do_self,do_self,do_self,do_self	/* 08-0B 7 8 9 0 */
	.long do_self,do_self,do_self,do_self	/* 0C-0F + ' bs tab */
	.long do_self,do_self,do_self,do_self	/* 10-13 q w e r */
	.long do_self,do_self,do_self,do_self	/* 14-17 t y u i */
	.long do_self,do_self,do_self,do_self	/* 18-1B o p } ^ */
	.long do_self,ctrl,do_self,do_self	/* 1C-1F enter ctrl a s */
	.long do_self,do_self,do_self,do_self	/* 20-23 d f g h */
	.long do_self,do_self,do_self,do_self	/* 24-27 j k l | */
	.long do_self,do_self,lshift,do_self	/* 28-2B { para lshift , */
	.long do_self,do_self,do_self,do_self	/* 2C-2F z x c v */
	.long do_self,do_self,do_self,do_self	/* 30-33 b n m , */
	.long do_self,minus,rshift,do_self	/* 34-37 . - rshift * */
	.long alt,do_self,caps,func		/* 38-3B alt sp caps f1 */
	.long func,func,func,func		/* 3C-3F f2 f3 f4 f5 */
	.long func,func,func,func		/* 40-43 f6 f7 f8 f9 */
	.long func,num,scroll,cursor		/* 44-47 f10 num scr home */
	.long cursor,cursor,do_self,cursor	/* 48-4B up pgup - left */
	.long cursor,cursor,do_self,cursor	/* 4C-4F n5 right + end */
	.long cursor,cursor,cursor,cursor	/* 50-53 dn pgdn ins del */
	.long none,none,do_self,func		/* 54-57 sysreq ? < f11 */
	.long func,none,none,none		/* 58-5B f12 ? ? ? */
	.long none,none,none,none		/* 5C-5F ? ? ? ? */
	.long none,none,none,none		/* 60-63 ? ? ? ? */
	.long none,none,none,none		/* 64-67 ? ? ? ? */
	.long none,none,none,none		/* 68-6B ? ? ? ? */
	.long none,none,none,none		/* 6C-6F ? ? ? ? */
	.long none,none,none,none		/* 70-73 ? ? ? ? */
	.long none,none,none,none		/* 74-77 ? ? ? ? */
	.long none,none,none,none		/* 78-7B ? ? ? ? */
	.long none,none,none,none		/* 7C-7F ? ? ? ? */
	.long none,none,none,none		/* 80-83 ? br br br */
	.long none,none,none,none		/* 84-87 br br br br */
	.long none,none,none,none		/* 88-8B br br br br */
	.long none,none,none,none		/* 8C-8F br br br br */
	.long none,none,none,none		/* 90-93 br br br br */
	.long none,none,none,none		/* 94-97 br br br br */
	.long none,none,none,none		/* 98-9B br br br br */
	.long none,unctrl,none,none		/* 9C-9F br unctrl br br */
	.long none,none,none,none		/* A0-A3 br br br br */
	.long none,none,none,none		/* A4-A7 br br br br */
	.long none,none,unlshift,none		/* A8-AB br br unlshift br */
	.long none,none,none,none		/* AC-AF br br br br */
	.long none,none,none,none		/* B0-B3 br br br br */
	.long none,none,unrshift,none		/* B4-B7 br br unrshift br */
	.long unalt,none,uncaps,none		/* B8-BB unalt br uncaps br */
	.long none,none,none,none		/* BC-BF br br br br */
	.long none,none,none,none		/* C0-C3 br br br br */
	.long none,none,none,none		/* C4-C7 br br br br */
	.long none,none,none,none		/* C8-CB br br br br */
	.long none,none,none,none		/* CC-CF br br br br */
	.long none,none,none,none		/* D0-D3 br br br br */
	.long none,none,none,none		/* D4-D7 br br br br */
	.long none,none,none,none		/* D8-DB br ? ? ? */
	.long none,none,none,none		/* DC-DF ? ? ? ? */
	.long none,none,none,none		/* E0-E3 e0 e1 ? ? */
	.long none,none,none,none		/* E4-E7 ? ? ? ? */
	.long none,none,none,none		/* E8-EB ? ? ? ? */
	.long none,none,none,none		/* EC-EF ? ? ? ? */
	.long none,none,none,none		/* F0-F3 ? ? ? ? */
	.long none,none,none,none		/* F4-F7 ? ? ? ? */
	.long none,none,none,none		/* F8-FB ? ? ? ? */
	.long none,none,none,none		/* FC-FF ? ? ? ? */

/*
 * kb_wait waits for the keyboard controller buffer to empty.
 * there is no timeout - if the buffer doesn't empty, we hang.
 */
kb_wait:
	pushl %eax
1:	inb $0x64,%al
	testb $0x02,%al
	jne 1b
	popl %eax
	ret
/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low.
 */
reboot:
	call kb_wait
	movw $0x1234,0x472	/* don't do memory check */
	movb $0xfc,%al		/* pulse reset and A20 low */
	outb %al,$0x64
die:	jmp die
