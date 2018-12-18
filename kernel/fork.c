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

int copy_mem(int nr,struct task_struct * p)
{
        unsigned long old_data_base,new_data_base,data_limit;
        unsigned long old_code_base,new_code_base,code_limit;

        code_limit=get_limit(0x0f);
        data_limit=get_limit(0x17);
        old_code_base = get_base(current->ldt[1]);
        old_data_base = get_base(current->ldt[2]);
        if (old_data_base != old_code_base)
                panic("We don't support separate I&D");
        if (data_limit < code_limit)
                panic("Bad data_limit");
        new_data_base = new_code_base = nr * 0x4000000;
        p->start_code = new_code_base;
        set_base(p->ldt[1],new_code_base);
        set_base(p->ldt[2],new_data_base);
        if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
                printk("free_page_tables: from copy_mem\n");
                free_page_tables(new_data_base,data_limit);
                return -ENOMEM;
        }
        return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
                 long ebx,long ecx,long edx,
                 long fs,long es,long ds,
                 long eip,long cs,long eflags,long esp,long ss)
{
        struct task_struct *p;
        int i;
        struct file *f;

        p = (struct task_struct *) get_free_page();
        if (!p)
                return -EAGAIN;
        task[nr] = p;
        *p = *current;	/* NOTE! this doesn't copy the supervisor stack */
        p->state = TASK_UNINTERRUPTIBLE;
        p->pid = last_pid;
        p->father = current->pid;
        p->counter = p->priority;
        p->signal = 0;
        p->alarm = 0;
        p->leader = 0;		/* process leadership doesn't inherit */
        p->utime = p->stime = 0;
        p->cutime = p->cstime = 0;
        p->start_time = jiffies;
        p->tss.back_link = 0;
        p->tss.esp0 = PAGE_SIZE + (long) p;
        p->tss.ss0 = 0x10;
        p->tss.eip = eip;
        p->tss.eflags = eflags;
        p->tss.eax = 0;
        p->tss.ecx = ecx;
        p->tss.edx = edx;
        p->tss.ebx = ebx;
        p->tss.esp = esp;
        p->tss.ebp = ebp;
        p->tss.esi = esi;
        p->tss.edi = edi;
        p->tss.es = es & 0xffff;
        p->tss.cs = cs & 0xffff;
        p->tss.ss = ss & 0xffff;
        p->tss.ds = ds & 0xffff;
        p->tss.fs = fs & 0xffff;
        p->tss.gs = gs & 0xffff;
        p->tss.ldt = _LDT(nr);
        p->tss.trace_bitmap = 0x80000000;
        if (last_task_used_math == current)
                __asm__("clts ; fnsave %0"::"m" (p->tss.i387));
        if (copy_mem(nr,p)) {
                task[nr] = NULL;
                free_page((long) p);
                return -EAGAIN;
        }
        for (i=0; i<NR_OPEN;i++)
                if ((f=p->filp[i]))
                        f->f_count++;
        if (current->pwd)
                current->pwd->i_count++;
        if (current->root)
                current->root->i_count++;
        if (current->executable)
                current->executable->i_count++;
        set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
        set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
        p->state = TASK_RUNNING;	/* do this last, just in case */
        return last_pid;
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
