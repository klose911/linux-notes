#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64 // 系统中最多同时的任务（进程）数
#define HZ 100 // 定义系统时钟滴答频率（100Hz，每个滴答10ms）

#define FIRST_TASK task[0] // 任务0比较特殊，所以特意给他单独定义一个符号
#define LAST_TASK task[NR_TASKS-1] // 任务数组中的最后一个

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

// 任务运行的状态值
#define TASK_RUNNING		0 // 运行或就绪
#define TASK_INTERRUPTIBLE	1 // 可中断等待状态
#define TASK_UNINTERRUPTIBLE	2 // 不可中断等待状态，主要用于 I/O 操作等待
#define TASK_ZOMBIE		3 // 僵死状态，实际已经停止，但是父进程还没发信号
#define TASK_STOPPED		4 // 进程已经停止

#ifndef NULL
#define NULL ((void *) 0) // 定义 NULL 空指针
#endif

// 复制进程的页目录表 (mm/memory.c)
extern int copy_page_tables(unsigned long from, unsigned long to, long size);
// 释放页表所指定的内存块和页表本身 (mm/memory.c)
extern int free_page_tables(unsigned long from, unsigned long size);

// 调度程序的初始化函数 (kernel/sched.c) 
extern void sched_init(void);
// 进程调度函数 (kernel/sched.c) 
extern void schedule(void);
// 异常（陷阱）中断处理初始化函数：设置中断调用门，并允许中断请求信号 (kernel/sched.c) 
extern void trap_init(void);
#ifndef PANIC
// 显示内核出错信息，然后死机
volatile void panic(const char * str);
#endif
// 往 tty 上写指定长度的字符串 (kernel/chr_drv/tty_io.c) 
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)(); // 定义函数指针类型：fn_ptr是指向一个无参数的，返回 int 的函数的指针

// 数学协处理器结构，主要用于保存进程切换时候 i387 的执行状态
struct i387_struct {
        long	cwd; // 控制字 (control word)
        long	swd; // 状态字 (status word)
        long	twd; // 标记字 (tag word)
        long	fip; // 协处理器代码指针 
        long	fcs; // 协处理器代码段寄存器
        long	foo; // 内存操作数的偏移位置
        long	fos; // 内存操作数的段值
        // 8 个 10 字节的协处理器累加器
        long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */ 
};

// 任务状态段数据结构
struct tss_struct {
	long	back_link;	/* 16 high bits zero */ 
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

// 任务（进程）数据结构，也被称为进程描述符
struct task_struct {
/* these are hardcoded - don't touch */
        // 任务状态：-1 不可运行，0 运行或就绪，> 0 等待或停止
        long state;	/* -1 unrunnable, 0 runnable, >0 stopped */ 
        long counter; // 任务运行时间计数（递减），运行时间片（滴答数）
        long priority; // 任务优先级，任务开始时 counter = priority, 越大表示可以运行的时间越长
        long signal; // 信号位图，每个比特位表示一个信号，信号值 = 位偏移值 + 1 
        struct sigaction sigaction[32]; // 信号执行属性结构，对应信号将要执行的操作和标志信息
        // 进程信号屏蔽位图，类似信号位图
        long blocked;	/* bitmap of masked signals */ 
/* various fields */
        int exit_code; // 任务执行停止的退出码，父进程会取
        // 代码段地址（线性地址），代码段长度（字节数），代码长度 + 数据长度（字节数），总长度（字节数），堆栈段地址
        unsigned long start_code,end_code,end_data,brk,start_stack;
        // 进程号，父进程号，进程组号，会话号，会话首领的进程号
        long pid,father,pgrp,session,leader;
        // 用户标识号，有效用户标识号，保存的用户标识号
        unsigned short uid,euid,suid;
        // 组标识号，有效组标识号，保存的组标识号
        unsigned short gid,egid,sgid;
        // 报警定时值（滴答数）
        long alarm;
        // 用户态运行时间（滴答数），内核态运行时间（滴答数），子进程用户态运行时间（滴答数），子进程内核态运行时间（滴答数），进程开始时刻（unix 时间格式，秒）
        long utime,stime,cutime,cstime,start_time;
        // 是否使用数学协处理器
        unsigned short used_math;
/* file system info */
        // 进程使用 tty 终端的子设备号（-1 表示未使用终端）
        int tty;		/* -1 if no tty, so it must be signed */
        unsigned short umask; // 文件创建属性屏蔽位
        struct m_inode * pwd; // 当前工作目录 i 节点结构的指针
        struct m_inode * root; // 根目录 i 节点结构的指针 
        struct m_inode * executable; // 执行文件 i 节点结构的指针 
        unsigned long close_on_exec; // 执行时候关闭文件句柄位图标志 (include/fcntl.h) 
        struct file * filp[NR_OPEN]; // 文件结构指针表，最多 32 项，表项号即是文件描述符值
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
        // 局部描述符号表： 0 -- 空， 1 -- 代码段， 2 -- 数据和堆栈段
        struct desc_struct ldt[3]; 
/* tss for this task */
        // 进程的任务状态段结构
        struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */

/*
 * INIT_TASK 用于设置第一个任务表，如果修改，后果自负！！！
 * 基地址 = 0，段长 = 0x9ffff (640KB) 
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, \
                                /* ec,brk... */	0,0,0,0,0,0, \
        /* pid etc.. */	0,-1,0,0,0, \
        /* uid etc */	0,0,0,0,0,0, \
        /* alarm */	0,0,0,0,0,0, \
        /* math */	0, \
        /* fs info */	-1,0022,NULL,NULL,NULL,0, \
        /* filp */	{NULL,}, \
	{ \
            {0,0}, \
                    /* ldt */	{0x9f,0xc0fa00}, \
                                                 {0x9f,0xc0f200}, \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir, \
                                             0,0,0,0,0,0,0,0, \
                                             0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
                                             _LDT(0),0x80000000, \
		{} \
	}, \
}


/* #define INIT_TASK \  */
/*         /\* state etc *\/	{ 0,15,15, \ // state, counter, priority */
/* /\* signals *\/	0,{{},},0, \ // signal, sigaction[32], blocked */
/*         /\* ec,brk... *\/	0,0,0,0,0,0, \ // exit_code, start_code, end_code, end_data, brk, start_stack */
/*         /\* pid etc.. *\/	0,-1,0,0,0, \ // pid, father, pgrp, session, leader */
/*         /\* uid etc *\/	0,0,0,0,0,0, \ // uid, euid, suid, gid, egid, sgid */
/*         /\* alarm *\/	0,0,0,0,0,0, \ // alarm, utime, stime, cutime, cstime, start_time */
/*         /\* math *\/	0, \ // used_math （没使用） */
/*         /\* fs info *\/	-1,0022,NULL,NULL,NULL,0, \ // tty （没使用），umask (0022), pwd, root, executable, close-on-exec */
/*         /\* filp *\/	{NULL,}, \ // file */
/* 	{ \ */
/*             {0,0}, \ //ldt[0] */
/*                     /\* ldt *\/	{0x9f,0xc0fa00}, \ //ldt[1]: 代码段长640KB，基地址 0x0, G=1, D=1, DPL=3, P=1, Type=0xa */
/*                                                  {0x9f,0xc0f200}, \ //ldt[2]: 数据段长640KB，基地址 0x0, G=1, D=1, DPL=3, P=1, Type=0x2 */
/* 	}, \  */
/*        \                      //tss结构  */
/*        \                      //backlink, esp0, ss0, esp1, ss1, esp2,ss2,cr3 */
/* /\*tss*\/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir, \    */
/*                                              0,0,0,0,0,0,0,0, \ //寄存器都为 0 */
/*                                              0,0,0x17,0x17,0x17,0x17,0x17,0x17, \ // es = cs = ss = ds = fs = gs = 0x17 （段选择符） */
/*                                              _LDT(0), 0x80000000, \ //ldt表入口，trace_bitmap */
/* 		{} \ */
/* 	}, \ */
/* } */

extern struct task_struct *task[NR_TASKS]; // 任务指针数组，64个元素，每个元素都是一个指针，指向任务结构
extern struct task_struct *last_task_used_math; // 上一个使用过数学协处理器的任务指针
extern struct task_struct *current; // 当前任务指针
extern long volatile jiffies; // 开机到现在经过的滴答数（10ms/滴答）
extern long startup_time; // 开机时间，从 1970:0:0:0开始计时的秒数

#define CURRENT_TIME (startup_time+jiffies/HZ) //当前时间（秒数）

extern void add_timer(long jiffies, void (*fn)(void)); // 
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */

/* 
 * 在 GDT 表中寻找第一个 TSS 描述符选择子
 * 0 : 空白，1：内核cs段，2：内核ds段，3：系统调用段（未使用），4：TSS0，5：LDT0，6：TSS1，7：LDT1 。。。 
 */
#define FIRST_TSS_ENTRY 4 // GDT 表中第一个任务的状态段(TSS)描述符选择子的索引号
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1) //  GDT 表中第一个任务的局部段(lDT)描述符选择子的索引号
// 每个描述符占用8个字节，所以用 FIRST_TSS_ENTRY<<3 来表示第一个任务的TSS在GDT中的偏移量
// 一对TSS和LDT总共占用16个字节，所以用 (((unsigned long) n)<<4) 来表示第 n 个任务的 TSS 和 第一个任务的TSS 的偏移量
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3)) // 计算第 n 个任务在 GDT 表中 TSS 描述符选择子的偏移量（字节）
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3)) // 计算第 n 个任务在 GDT 表中 LDT 描述符选择子的偏移量（字节）
// 把第 n 个任务的 TSS段加载到“任务寄存器” TR 中
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
// 把第 n 个任务的 LDT段加载到“局部描述符表寄存器” LDTR中
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))

/*
 * 取当前运行进程的任务号（任务数组中的索引值，与进程号 pid 不同）
 *
 * 返回 n : 当前任务号
 */
// 1. 将任务寄存器中 TSS 段的值复制到 ax 寄存器
// 2. (eax - FIRST_TSS_ENTRY * 8) -> eax
// 3. (eax / 16) -> eax 这就是当前任务号 
#define str(n)         \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */

/*
 * 切换当前任务到任务号 n, 如果要切换到的任务，还使用过数学协处理器，那还需要清空控制寄存器(CR0)的 TS 标志位 
 * 
 */

/**
 * 跳转到一个任务的 TSS 段选择符组成的地址，可以让 CPU 进行任务切换操作
 * 
 * 输入： %0 -- 指向 __tmp.a , %1 -- 指向 __tmp.b
 *        dx -- 新任务n的选择符，ecx -- 新任务n的任务结构指针 task[n]
 * 
 * 临时数据结构 __tmp 是用来做长跳转的(ljmp)指令的操作数，其中它由4字节偏移地址和2字节的段选择符组成。
 * 因此 __tmp.a 是32位偏移值，而 __tmp.b 的低2字节是新TSS段选择符，高2字节未用。实际上这里 a 值未用
 * 
 * ljmp *%0 内存间接跳转指令，使用6字节操作数作为跳转目的的长跳转指令
 * 格式为：ljmp 16位段选择子，32位段偏移值。注意：内存中保存的顺序，实际上是和这里指令中参数顺序相反 
 */
// 1. 校验当前任务是否就是要跳转的任务 n ，如果是则直接退出
// 2. 把dx中 TSS(n) 存入到 __tmp.b
// 3. 长跳转切换到任务 n
// 4. 比较“最后一个使用数学协处理器”的任务指针 (last_task_used_math) 和“任务n”的指针是否一样
// 5. 如果一样则清空 cr0 中的 任务切换标志位 (TS) : clts 
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,current\n\t" \
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \
	"xchgl %%ecx,current\n\t" \
	"ljmp *%0\n\t" \
	"cmpl %%ecx,last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

// 页面地址对准，内核未使用        
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)

/**
 * 设置位于地址 addr 处描述符的基地址字段，基地址是 base 
 * 
 * 输入：%0 -- 地址 addr 偏移 2 , %1 -- 地址 addr 偏移 4 , %2 -- 地址 addr 偏移 7 
 *      edx -- 基地址 base 
 */
// 1. 基地址 base 中的低16位(0 ~ 15) -> [addr + 2]
// 2. edx 中高16位 (16 ~ 31) -> dx
// 3. dx 中低16位 (16 ~ 23) -> [addr + 4]
// 4. dx 中高16位 （24 ~ 31） -> [addr + 7]
#define _set_base(addr,base)  \
__asm__ ("push %%edx\n\t" \
	"movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2\n\t" \
	"pop %%edx" \
	::"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "d" (base) \
	)

/**
 * 设置位于地址 addr 处描述符的段限长字段，段限长是 limit 
 * 
 * 输入：%0 -- 地址 addr , %1 -- 地址 addr 偏移 6
 *      edx -- 段长值 limit  
 */
// 1. 段长 limit 的低16位(0 ~ 15) -> [addr]
// 2. edx 中的高4位(16 ~ 19) -> dl
// 3. 取[addr + 6] 中的字节 -> dh，其中高4位是一些标志位
// 4. 清空 dh 中的低4位
// 5. 合并 dh 中的高4位，和 dl 中的低4位成一个字节
// 6. 把合成后的字节 -> [addr + 6]
#define _set_limit(addr,limit) \
__asm__ ("push %%edx\n\t" \
	"movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1\n\t" \
	"pop %%edx" \
	::"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "d" (limit) \
	)

// 设置局部描述符表中 ldt 的基地址字段 base 
#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , (base) )
// 设置局部描述符表中 ldt 的段限长字段 limit 
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

/**
 * 从地址 addr 处获取描述符中的段基址
 * 输出：edx - 存放段基址
 * 输入：%1 - 地址 addr 偏移 2 ，%2 - 地址 addr 偏移 4 ， %3 - 地址 addr 偏移 7 
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \  取[addr + 7] 处的基址高16位的高8位到（位 31～24） -> dh 寄存器 
	"movb %2,%%dl\n\t" \  取[addr + 4] 处的基址高16位的低8位到（位 23～16） -> dl 寄存器
	"shll $16,%%edx\n\t" \ 把基地址高16位移动到 edx 中高16位处
	"movw %1,%%dx" \ 取 [addr + 2] 处的基址的低16位到（位 15 ~ 0） -> dx  
	:"=d" (__base) \ 从而 edx 中含有 32 位的段基础地址
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)) \
        :"memory"); \
__base;})
**/


static inline unsigned long _get_base(char * addr)
{
      
         unsigned long __base;
         __asm__("movb %3,%%dh\n\t"
                 "movb %2,%%dl\n\t"
                 "shll $16,%%edx\n\t"
                 "movw %1,%%dx"
                 :"=&d" (__base)
                 :"m" (*((addr)+2)),
                  "m" (*((addr)+4)),
                  "m" (*((addr)+7)));
         return __base; 
}

/*
 * 取局部描述符表中 ldt 所指段描述符中的基地址
 */
#define get_base(ldt) _get_base( ((char *)&(ldt)) )

/* 取段选择符子 segment 指定的描述符中的段限长度值
 * 指令 lsl是 load segment limit 的缩写：从指定的“段描述符”中取出分散的“限长比特位”拼成完整的段限长值放入指定的寄存器中
 * 所得的段限长度，是实际字节数减 1，所以最后这里还得加上 1，才返回
 * %0 -- 存放段长值（字节数）， %1 -- 段选择符 segment
 */
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
