/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h> // 错误号头文件

#include <linux/sched.h> // 系统调度头文件
#include <linux/kernel.h> // 内核头文件
#include <asm/segment.h> // 段操作头文件
#include <asm/system.h> // 汇编宏头文件

// 写页面验证，若页面不可写，则复制页面
extern void write_verify(unsigned long address);

// 最新进程号，由 get_empty_process 组成
long last_pid=0;

/**
 * 进程空间区域写前验证函数
 *
 * addr: 内存地址偏移
 * size: 字节大小
 *
 * 无返回值
 *
 * 该函数对当前进程逻辑地址 addr 到 addr + size 这一段范围内以页为单位执行写操作前的检测工作，
 * 如果该页面是只读的，那么执行共享校验和复制页面操作（写时复制）
 */
void verify_area(void * addr,int size)
{
        unsigned long start;

        start = (unsigned long) addr;
        size += start & 0xfff; // start & 0xfff = 指定起始位置 addr 所在页面中的偏移值， size + 偏移值 = 扩展成 addr 页面起始位置开始的范围值
        start &= 0xfffff000; // “起始地址” start 调整为其所在页的左边界开始位置
        start += get_base(current->ldt[2]); // start + 进程数据段在线性地址空间中的基础地址 = start 的“线性地址”
// 循环进行页面验证
        while (size>0) {
                size -= 4096; // 验证的大小减小 4KB (一页字节数)
                write_verify(start); // 若页面不可写，则复制页面
                start += 4096; // 验证起始地址增加 4KB （下一页）
        }
}

/**
 * 在线性空间复制内存页表
 *
 * nr: 新任务槽（任务结构数组索引号）
 * p: 新任务结构指针
 *
 * 返回值：成功返回0，失败返回错误号
 *
 * 注意：这里所有的地址都是指的线性地址，而不是物理地址！！！
 */
int copy_mem(int nr,struct task_struct * p)
{
        unsigned long old_data_base,new_data_base,data_limit;
        unsigned long old_code_base,new_code_base,code_limit;

        code_limit=get_limit(0x0f); // 获取当前进程“代码段限长”（cs = 0x0f）
        data_limit=get_limit(0x17); // 获取当前进程“数据段限长”(ds = es = gs = 0x17) 
        old_code_base = get_base(current->ldt[1]); // 获取当前进程“代码段基地址”
        old_data_base = get_base(current->ldt[2]); // 获取当前进程“数据段基地址”
        if (old_data_base != old_code_base) // 注意：linux 进程的“代码段”，“数据段”，“堆栈段”基地址都一样的！！！
                panic("We don't support separate I&D");
        if (data_limit < code_limit) // 注意：linux 进程的“数据段”长度必须大于“代码段”长度
                panic("Bad data_limit");

        // 每个进程的“代码段”和“数据段”的基地址都是 "任务数组索引号 x 64MB"
        new_data_base = new_code_base = nr * 0x4000000;
        p->start_code = new_code_base; // 设置新任务结构中的“代码段线性地址“
        set_base(p->ldt[1],new_code_base); // 设置新任务结构中“局部描述表”里“代码段描述符”
        set_base(p->ldt[2],new_data_base); // 设置新任务结构中“局部描述表”里“数据段描述符”
        // 设置新进程的页目录表，和页表项：实际上就是复制当前进程（父进程）的“页目录表项”和“页表项”
        // 注意：此时父子进程共享同一段真实的物理页面，只有任意一个进程写这段页面，才会产生真正的物理内存页面复制动作（写时复制）
        if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
                printk("free_page_tables: from copy_mem\n");
                free_page_tables(new_data_base,data_limit); 
                return -ENOMEM; // 返回“内存不够”错误号
        }
        return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
/**
 * 复制进程
 *
 * 它的所有参数都来自于“系统调用” system_call.s 逐步压栈
 * 1. CPU执行中断调用时自动压入的用户栈 ss, esp, 标志寄存器 eflags, 用户栈 cs, eip
 * 2. 刚进入 system_call 函数时压入的段寄存器 ds, es, fs, 和通用寄存器 edx, ecx, ebx
 * 3. 调用 sys_call_table 中 sys_fork 时压入的返回地址：参数 none
 * 4. sys_fork 中调用 copy_process 前入栈的 gs, esi, edi, ebp, eax(参数nr)
 *
 * 返回值：成功返回“当前最新进程号”，失败返回错误号
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
                 long ebx,long ecx,long edx,
                 long fs,long es,long ds,
                 long eip,long cs,long eflags,long esp,long ss)
{
        struct task_struct *p;
        int i;
        struct file *f;

        // 为新的任务结构分配一页新的物理内存
        // 注意，这里的 p 返回的是物理地址，而非线性地址！！
        // 内核的段基础地址都是0, 对内核而言线性地址等价于逻辑地址
        // 而内核的页目录表也是从索引0开始，对内核而言线性地址=物理地址，因此可以把物理地址直接作为指针 :-)
        p = (struct task_struct *) get_free_page(); 
        if (!p)
                return -EAGAIN; // 如果分配内存出错，返回 -EAGAIN 
        task[nr] = p; // 设置内核结构数组中对应的任务槽
        // 复制当前进程的结构到新进程结构中（会有字段拷贝）
        // 注意：这不会复制内核堆栈！！！
        *p = *current;	/* NOTE! this doesn't copy the supervisor stack */
        p->state = TASK_UNINTERRUPTIBLE; // 新进程状态为”信号不可中断睡眠“
        p->pid = last_pid; // 设置新进程ID
        p->father = current->pid; // 设置新进程的父进程ID为当前进程ID  
        p->counter = p->priority; // 设置任务运行时间片（滴答数，一般为15）
        p->signal = 0; // 设置新进程信号位图
        p->alarm = 0; // 设置新进程的计时器（滴答数）
        // 设置进程的领头进程ID，注意：这个不能被继承
        p->leader = 0;		/* process leadership doesn't inherit */
        p->utime = p->stime = 0; // 设置新进程的用户运行时间，内核运行时间为0
        p->cutime = p->cstime = 0; // 设置新进程的子进程运行时间，子进程内核运行时间为0
        p->start_time = jiffies; // 设置新进程的开始运行时间为”当前滴答数“
        // 开始设置任务状态段数据
        p->tss.back_link = 0;
        // 由于 p 处于一个新分配的页面物理内存开始处，所以 PAGE_SIZE + p 正好处于下一个物理页面的开始处
        p->tss.esp0 = PAGE_SIZE + (long) p; // 设置新进程的内和堆栈段指针
        p->tss.ss0 = 0x10; // 设置内核态堆栈段选择子
        p->tss.eip = eip; // 用户态代码指针
        p->tss.eflags = eflags; // 标志寄存器 
        p->tss.eax = 0; // eax 寄存器的值，作为 sys_fork 系统调用返回的值，这也是为什么fork()函数新进程返回 0 的原因！！！
        // 新进程的 ecx, edx, ebx, esp, ebp, esi, edi 都和老进程保持一致
        p->tss.ecx = ecx;  
        p->tss.edx = edx;
        p->tss.ebx = ebx;
        p->tss.esp = esp;
        p->tss.ebp = ebp;
        p->tss.esi = esi;
        p->tss.edi = edi;
        // 新进程的用户态 es, cs, ss, ds, fs, gs 也和老进程一样，但这些寄存器仅仅16位有效
        p->tss.es = es & 0xffff;
        p->tss.cs = cs & 0xffff;
        p->tss.ss = ss & 0xffff;
        p->tss.ds = ds & 0xffff;
        p->tss.fs = fs & 0xffff;
        p->tss.gs = gs & 0xffff;
        p->tss.ldt = _LDT(nr); // 任务局部描述符的选择子(ldt(nr) 在 GDT 中的偏移字节量 + 一些标志位)
        p->tss.trace_bitmap = 0x80000000; // 高16位有效
        
        // 如果当前进程使用了数学协处理器，同样也需要在任务段中保存其上下文：
        // 1. clts: 用于清理控制寄存器 cr0 中任务已交换的(TS)标志位，每当发生任务切换时，CPU 都会设置该标志，用于管理数学协处理器
        // 如果该标志位被设置：那么每个 ESC 指令都会被捕获（异常7）。如果协处理器存在标志位MP也被设置，那么每个 WAIT 指令也会被捕获
        // 因此，如果任务切换发生一个 ESC 指令开始执行之后，那么协处理器的内容就需要在执行下一个 ESC 指令前保存起来
        // 异常捕获处理句柄会保存协处理器的内容，并复位 TS 标志位
        // 2. fnsave: 把协处理中的状态保存到目标数指定的内存区域中 (p->tss.i387) 
        if (last_task_used_math == current)
                __asm__("clts ; fnsave %0"::"m" (p->tss.i387));

        // 复制进程页表：
        // 1. 在新进程任务结构的”局部描述符表“中设置对应”局部代码段“和”局部数据段“的描述符
        // 2. 复制当前进程的”页目录项“和”页表项“
        if (copy_mem(nr,p)) {
                // 复制进程页表出错
                task[nr] = NULL; 
                free_page((long) p);
                return -EAGAIN; // 返回 -EAGAIN 
        }
        
        // 复制当前进程的文件描述符表
        for (i=0; i<NR_OPEN;i++)
                // 如果当前进程打开了某个文件，那么子进程会共享这个文件
                if ((f=p->filp[i]))
                        f->f_count++; // 这个文件打开次数会加 1
        // 当前进程如果引用了 当前目录 pwd, 根目录 root ，可执行文件 executable的 i 节点，
        // 那么新创建的子进程也会引用这些节点，所以节点的引用数也必须加 1  
        if (current->pwd)
                current->pwd->i_count++;
        if (current->root)
                current->root->i_count++;
        if (current->executable)
                current->executable->i_count++;
        
        // 最后在 GDT 表中设置新任务对应的 TSS(nr) 和 LDT(nr) 的描述符
        // 其段基础地址用的是 p-> tss , p-> ldt 的指针值，而段限长均为 104字节
        // 对于内核而言，逻辑地址 = 线性地址 = 物理地址，还记得吗？ :-) 
        set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
        set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
        p->state = TASK_RUNNING; // 子进程的状态设置”就绪“	/* do this last, just in case */
        return last_pid; // 父进程返回”最新的进程ID“
}


/**
 * 为新进程获取新的进程号
 *
 * 无参数
 *
 * 成功返回任务结构数组中的索引号，失败则返回 -EAGAIN
 */
int find_empty_process(void)
{
        int i;

repeat:
        if ((++last_pid)<0)
                last_pid=1; // 注意：如果 last_pid 超出正整数范围，则重新从1开始
        for(i=0 ; i<NR_TASKS ; i++)
                if (task[i] && task[i]->pid == last_pid) // last_pid 已经被使用，则重新自增开始循环
                        goto repeat;

// 遍历任务数组，找到一个空的槽，并返回这个槽的索引
        for(i=1 ; i<NR_TASKS ; i++) // 注意：任务0被排除
                if (!task[i])
                        return i;

        // 如果任务结构数组64项全部被占用，则返回错误 -EAGAIN
        return -EAGAIN;
}
