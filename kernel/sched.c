/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */

/*
 * 'sched.c' 是主要的内核文件。它包含了有关调度的原子语句（sleep_on, wakeup, schedule 等）
 * 以及一些简单的系统调用函数（类似 getpid 等，从当前任务获取一个字段）
 */
#include <linux/sched.h> 
#include <linux/kernel.h> 
#include <linux/sys.h> 
#include <linux/fdreg.h> // 软驱头文件，含有软盘控制器的一些定义
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

// 取信号 nr 在信号位图中对应位的二进制数值。信号编号从 1～32
// 比如信号 5 在信号位图中对应位的二进制数值: 00010000b = 16 = (1 << (5 - 1))    
#define _S(nr) (1<<((nr)-1))

// 除了 SIGKILL 和 SIGSTOP 之外的信号都是可以阻塞的
// 11111111111111011111111011111111b
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

/**
 * 内核调试函数：显示任务号 nr 的进程号，进程状态，以及内核堆栈空闲字节数
 *
 * nr: 任务号
 × p: 任务结构指针
*/
void show_task(int nr,struct task_struct * p)
{
        int i,j = 4096-sizeof(struct task_struct);

        printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state); // 打印任务号，进程号，进程状态
        i=0;
        // p 是任务结构所处开始的内存地址， p + 1 任务结构结束处的内存地址
        // ((char *) (p + 1)) 强制转换成字符指针，以后每次指针递增，递减都是1个字节  
        while (i<j && !((char *)(p+1))[i]) // 检查指定任务数据结构以后等于0的字节数
                i++;
        printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}


/**
 * 打印所有任务的任务号，进程号，进程状态，和内核堆栈空闲字节数
 */
void show_stat(void)
{
        int i;

        // 遍历任务进程数组
        for (i=0;i<NR_TASKS;i++)
                if (task[i])
                        show_task(i,task[i]);
}

// PC8253 定时芯片的输入时钟频率约为 1.193180MHz，
#define LATCH (1193180/HZ)

extern void mem_use(void); // 没有任何地方引用此函数 

extern int timer_interrupt(void); // 时钟处理中断处理程序 (kernel/system_call.s)
extern int system_call(void); // 系统中断处理程序 (kernel/system_call.s)

// 每个任务（进程）在内核态运行都有自己的内核态堆栈。这里定义了“内核态堆栈的”数据结构：
// 使用union，让一个“任务”数据结构和它的“内核态堆栈”放在一个内存页上
// 从堆栈段寄存器 ss 上也可以获得内核数据选择子 (tss.ss0 是被设置成 0x10, 也即内核数据段选择子)
union task_union {
        struct task_struct task; // 任务数据结构
        char stack[PAGE_SIZE]; // 任务的内核堆栈
};

// 初始任务的堆栈结构
static union task_union init_task = {INIT_TASK,};

// 从开机开始计算的滴答次数(10ms/次)　
// volatile 意味着这个变量不能被优化，不应该放到寄存器中，不能移动内存位置，而且每次都需从内存读取，以防资源竞争
long volatile jiffies=0;
// 开机时间，unix 格式(秒)
long startup_time=0;
// 当前任务的结构指针，指向当前运行的任务
struct task_struct *current = &(init_task.task);
// 最后一个使用数学协处理的任务指针
struct task_struct *last_task_used_math = NULL;

// 任务指针数组，第一项元素是指向＂初始任务＂数据结构的指针．．．
struct task_struct * task[NR_TASKS] = {&(init_task.task), };

// 定义用户堆栈，共1K项，容量是 4KB 
// 在内核初始化过程中，被用作内核堆栈，初始化完成后将被用作任务0的用户态堆栈
// 在运行任务 0 以前它是内核栈，以后用作任务 0 和 1 的用户态栈
long user_stack [ PAGE_SIZE>>2 ] ;

// 下面结构用于设置堆栈 ss:esp （数据选择符，指针）
// ss = 0x10 : 内核数据段选择符
// esp 指向 user_stack 数组最后一项后面，这是因为 Intel CPU 入栈的时候是减去 esp, 然后在 esp 指针处压入数据的！！！
struct {
        long * a; // 指向 user_stack 数组最后一项后面
        short b; // 内核数据段选择子 0x 10 
} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */

/**
 * 新任务被调度进来后：
 * 1. 把当前数学协处理的任务暂存到上一个任务的任务状态段 tss 的 i387成员上
 * 2. 如果新任务已经使用过数学协处理器，则把新任务状态段 tss 的 i387成员值恢复到数学协处理器上，反之则初始化数学协处理器
 */
void math_state_restore()
{
        // 如果任务没改变，则直接返回（上一个任务就是当前任务）
        if (last_task_used_math == current)
                return;
        // 在发送协处理器命令前必须先发送 wait 指令
        __asm__("fwait");
        // 如果上个任务使用了数学协处理器，则保存数学协处理的状态到上个任务的状态段 tss 的 i387 成员上
        if (last_task_used_math) {
                __asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
        }
        last_task_used_math=current; // last_task_used_math 指向当前任务

        // 如果当前任务使用过数学协处理器，则把数学协处理恢复成当前任务的状态段 tss 的 i387 成员值
        if (current->used_math) {
                __asm__("frstor %0"::"m" (current->tss.i387));
        } else {
                __asm__("fninit"::); // 向数学协处理器发送初始化指令
                current->used_math=1; // 设置已使用数学协处理
        }
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */

/**
 * 'schedule()' 是进程调度函数。这是很好的代码。一般情况下没有任何理由去修改它，因为它可以在任何情况下工作得很好（比如能够对 IO 边界响应程序处理得很好）
 * 唯一需要注意的是这里对于信号处理的代码
 *
 * 注意!!! 任务 0 是个闲置(idle)任务，只有当没有其他任务可以运行时候才会调用它
 * 因此任务 0 无法被杀死，也无法被终止， task[0] 中的 state 字段实际上是永远不会被用到
 */
void schedule(void)
{
        int i,next,c;
        struct task_struct ** p; // 任务结构指针的指针

/* check alarm, wake up any interruptible tasks that have got a signal */
// 检查 alarm (进程的报警定时值)，唤醒任何已经“得到信号”的“可中断任务”
        // 从任务数组的最后一个任务开始循环
        for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
                // 任务结构指针不为空
                if (*p) {
                        // 任务设置过定时值，并且已经超时
                        if ((*p)->alarm && (*p)->alarm < jiffies) {
                                // 信号位图对应的 SIGALRM 位设置为 1，等价于向任务发送 SIGALRM 信号，这个信号默认的操作是终止进程
                                (*p)->signal |= (1<<(SIGALRM-1));
                                // 进程定时器归零
                                (*p)->alarm = 0; 
                        }
                        // (*p)->signal : 信号位图中有信号
                        // ~(_BLOCKABLE & (*p)->blocked) : 信号位图中的信号不在信号屏蔽位图中。注意：SIGKILL 和 SIGSTOP 信号无法被屏蔽
                        if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
                            (*p)->state==TASK_INTERRUPTIBLE) // 任务状态是“可中断的休眠”状态
                                (*p)->state=TASK_RUNNING; // 状态设置为“就绪”，等价于”唤醒“进程
                }

/* this is the scheduler proper: */

        // 调度主程序
        while (1) {
                c = -1; // 临时 counter 值
                next = 0; // 下一个运行任务的数组索引
                i = NR_TASKS; // 64 
                p = &task[NR_TASKS]; // 指向任务结构指针数组的最后一项
                // 从任务数组最后开始循环
                while (--i) {
                        // 跳过任务结构指针为空的元素
                        if (!*--p) 
                                continue;
                        // 查找处于就绪状态下 counter 值最大的任务
                        if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
                                c = (*p)->counter, next = i;
                }
                if (c) break; // 如果有可执行的任务，则跳出当前循环，进行任务切换操作 -> switch_to(next)

                //系统中每个可运行的任务时间片都用完，那么更新每个任务的 counter 值，然后从 while(1) 从新开始循环
                for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
                        if (*p)
                                // 新 counter 值 = 老 counter 值 / 2 + priority 注意：这里无视进程状态 
                                (*p)->counter = ((*p)->counter >> 1) +
                                        (*p)->priority;
        }
        // 切换任务到 next 处的任务
        // 由于 next 被初始化为 0，所以当没有任何其他任务时，next始终为0，调度函数只能选择执行任务0
        // 任务0 会执行 'pause()' 系统调用，这个系统调用又会调用本函数 'schedule()'
        switch_to(next); 
}

/**
 * 'pause()' 系统调用。转换当前任务为可中断状态，并且重新调度
 *
 * 该系统调用将导致进程进入睡眠状态，直到收到一个信号。该信号用于终止进程，或者使进程调用一个信号捕获（处理）函数
 * 只有当收到一个信号，并且信号捕获函数返回，'pause()' 才会返回，此时返回值是 -1，并且 errno 被置为 EINTR （实际上这里还没有完全实现！！！）
 */
int sys_pause(void)
{
        current->state = TASK_INTERRUPTIBLE;
        schedule();
        return 0;
}

void sleep_on(struct task_struct **p)
{
        struct task_struct *tmp;

        if (!p)
                return;
        if (current == &(init_task.task))
                panic("task[0] trying to sleep");
        tmp = *p;
        *p = current;
        current->state = TASK_UNINTERRUPTIBLE;
        schedule();
        if (tmp)
                tmp->state=0;
}

void interruptible_sleep_on(struct task_struct **p)
{
        struct task_struct *tmp;

        if (!p)
                return;
        if (current == &(init_task.task))
                panic("task[0] trying to sleep");
        tmp=*p;
        *p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
        schedule();
        if (*p && *p != current) {
                (**p).state=0;
                goto repeat;
        }
        *p=NULL;
        if (tmp)
                tmp->state=0;
}

void wake_up(struct task_struct **p)
{
        if (p && *p) {
                (**p).state=0;
                *p=NULL;
        }
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
        extern unsigned char selected;
        unsigned char mask = 0x10 << nr;

        if (nr>3)
                panic("floppy_on: nr>3");
        moff_timer[nr]=10000;		/* 100 s = very big :-) */
        cli();				/* use floppy_off to turn it off */
        mask |= current_DOR;
        if (!selected) {
                mask &= 0xFC;
                mask |= nr;
        }
        if (mask != current_DOR) {
                outb(mask,FD_DOR);
                if ((mask ^ current_DOR) & 0xf0)
                        mon_timer[nr] = HZ/2;
                else if (mon_timer[nr] < 2)
                        mon_timer[nr] = 2;
                current_DOR = mask;
        }
        sti();
        return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
        cli();
        while (ticks_to_floppy_on(nr))
                sleep_on(nr+wait_motor);
        sti();
}

void floppy_off(unsigned int nr)
{
        moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
        int i;
        unsigned char mask = 0x10;

        for (i=0 ; i<4 ; i++,mask <<= 1) {
                if (!(mask & current_DOR))
                        continue;
                if (mon_timer[i]) {
                        if (!--mon_timer[i])
                                wake_up(i+wait_motor);
                } else if (!moff_timer[i]) {
                        current_DOR &= ~mask;
                        outb(current_DOR,FD_DOR);
                } else
                        moff_timer[i]--;
        }
}

#define TIME_REQUESTS 64

static struct timer_list {
        long jiffies;
        void (*fn)();
        struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;

void add_timer(long jiffies, void (*fn)(void))
{
        struct timer_list * p;

        if (!fn)
                return;
        cli();
        if (jiffies <= 0)
                (fn)();
        else {
                for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
                        if (!p->fn)
                                break;
                if (p >= timer_list + TIME_REQUESTS)
                        panic("No more time requests free");
                p->fn = fn;
                p->jiffies = jiffies;
                p->next = next_timer;
                next_timer = p;
                while (p->next && p->next->jiffies < p->jiffies) {
                        p->jiffies -= p->next->jiffies;
                        fn = p->fn;
                        p->fn = p->next->fn;
                        p->next->fn = fn;
                        jiffies = p->jiffies;
                        p->jiffies = p->next->jiffies;
                        p->next->jiffies = jiffies;
                        p = p->next;
                }
        }
        sti();
}

void do_timer(long cpl)
{
        extern int beepcount;
        extern void sysbeepstop(void);

        if (beepcount)
                if (!--beepcount)
                        sysbeepstop();

        if (cpl)
                current->utime++;
        else
                current->stime++;

        if (next_timer) {
                next_timer->jiffies--;
                while (next_timer && next_timer->jiffies <= 0) {
                        void (*fn)(void);
			
                        fn = next_timer->fn;
                        next_timer->fn = NULL;
                        next_timer = next_timer->next;
                        (fn)();
                }
        }
        if (current_DOR & 0xf0)
                do_floppy_timer();
        if ((--current->counter)>0) return;
        current->counter=0;
        if (!cpl) return;
        schedule();
}

int sys_alarm(long seconds)
{
        int old = current->alarm;

        if (old)
                old = (old - jiffies) / HZ;
        current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
        return (old);
}

int sys_getpid(void)
{
        return current->pid;
}

int sys_getppid(void)
{
        return current->father;
}

int sys_getuid(void)
{
        return current->uid;
}

int sys_geteuid(void)
{
        return current->euid;
}

int sys_getgid(void)
{
        return current->gid;
}

int sys_getegid(void)
{
        return current->egid;
}

int sys_nice(long increment)
{
        if (current->priority-increment>0)
                current->priority -= increment;
        return 0;
}

void sched_init(void)
{
        int i;
        struct desc_struct * p;

        if (sizeof(struct sigaction) != 16)
                panic("Struct sigaction MUST be 16 bytes");
        set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
        set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
        p = gdt+2+FIRST_TSS_ENTRY;
        for(i=1;i<NR_TASKS;i++) {
                task[i] = NULL;
                p->a=p->b=0;
                p++;
                p->a=p->b=0;
                p++;
        }
/* Clear NT, so that we won't have troubles with that later on */
        __asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
        ltr(0);
        lldt(0);
        outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
        outb_p(LATCH & 0xff , 0x40);	/* LSB */
        outb(LATCH >> 8 , 0x40);	/* MSB */
        set_intr_gate(0x20,&timer_interrupt);
        outb(inb_p(0x21)&~0x01,0x21);
        set_system_gate(0x80,&system_call);
}
