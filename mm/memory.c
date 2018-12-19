/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h> // 信号头文件

#include <asm/system.h> // 系统汇编头文件

#include <linux/sched.h> // 系统进程头文件
#include <linux/head.h> // 定义了GDT，LDT，IDT, 页目录表，页表等一系列写保护模式有关的数据结构
#include <linux/kernel.h> // 声明了一系列系统常用函数

volatile void do_exit(long code);

static inline volatile void oom(void)
{
        printk("out of memory\n\r");
        do_exit(SIGSEGV);
}

// 刷新页变换寄存器
#define invalidate()                            \
        __asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000 // 主内存开始地址 1MB 
#define PAGING_MEMORY (15*1024*1024) // 主内存大小 15MB 
#define PAGING_PAGES (PAGING_MEMORY>>12) // 每页内存是4KB, 实际是2^12B，所以实际上主内存的页数：主内存右移12位
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12) // 物理地址对应的页面号码
#define USED 100 // 被占用

// 判断给定的物理地址是否位于当前进程的代码段中
// (((addr)+4095)&~4095) ：获得当前线性地址所在页面的末端地址
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) <                   \
                          current->start_code + current->end_code)

static long HIGH_MEMORY = 0; // 全局变量，用于存放主内存的最高地址

// 从from 处 复制 1页内存到 to 处
#define copy_page(from,to)                                              \
        __asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024))

// 物理内存映射字节图：1字节代表1页内存
// 每个页面对应的值用于标记这个页面被引用（占用）的次数
// 它最大可被映射 15MB 内存
// 在初始化内存 mem_init 函数中，对于主内存不能被用的（高速缓存区以及可能的虚拟内存盘）都会被设置成 USED(100)
static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
unsigned long get_free_page(void)
{
        register unsigned long __res asm("ax");

        __asm__("std ; repne ; scasb\n\t"
                "jne 1f\n\t"
                "movb $1,1(%%edi)\n\t"
                "sall $12,%%ecx\n\t"
                "addl %2,%%ecx\n\t"
                "movl %%ecx,%%edx\n\t"
                "movl $1024,%%ecx\n\t"
                "leal 4092(%%edx),%%edi\n\t"
                "rep ; stosl\n\t"
                "movl %%edx,%%eax\n"
                "1:"
                :"=a" (__res)
                :"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
                 "D" (mem_map+PAGING_PAGES-1)
                );
        return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
/**
 * 释放物理地址 addr 开始的一个页面内存，会被 free_page_tables 函数使用
 * addr: 物理地址
 */
 
void free_page(unsigned long addr)
{
        if (addr < LOW_MEM) return;// 物理地址 1MB 以下的内存会被内核和缓冲使用，所以 addr 必须大于 1MB
        if (addr >= HIGH_MEMORY) // 大于最大物理地址，显示出错信息，内核停止工作
                panic("trying to free nonexistent page");
// 换算出页面号
        addr -= LOW_MEM;
        addr >>= 12;
        // 如果对应页面的字节映射值大于0，则递减1，结束
        if (mem_map[addr]--) return;
        // 如果此时的页面的字节映射值已经等于或小于0,意味着原本就是空闲的，说明内核出错，则显示出错信息，并停止内核
        mem_map[addr]=0;
        panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
/**
 * 释放页表连续的内存块，exit()需要该函数，该函数仅处理 4MB 为单位的长度
 * from: 起始线性地址，比如0x3fffff
 * size: 释放的字节长度
 */
// 根据指定的线性地址和限长（页表个数），释放对应内存页面指定的内存块，并设置表项空闲
// 页目录位于物理地址 0 开始，共 1024 项， 每项 4 字节， 共 4KB
// 每个目录项指定一个页表，页表从物理地址 0x1000 开始（紧接着页目录表），共 4个页表，
// 每个页表也是 1024 项目，每项 4 字节，因此也是 4KB（1页）内存。
// 各个进程（除了内核代码中的 0 和 1）的页表在进程被创建时由内核为其在主内存申请得到。
// 每个页表项对应一个物理内存页，1024项就是映射了 4MB 的内存
int free_page_tables(unsigned long from,unsigned long size)
{
        unsigned long *pg_table;
        unsigned long * dir, nr;

        if (from & 0x3fffff) // 检查线性地址是否在 4MB 的边界处，不在则报错
                panic("free_page_tables called with wrong alignment");
        if (!from) // 检查线性地址是否为 0，为0则报错
                panic("Trying to free up swapper memory space");
        // 计算 size 给出的长度占了多少个页目录项，每个页目录对应的 4MB (2 ^ 22)
        size = (size + 0x3fffff) >> 22; // 加上 0x3fffff 是为了用于得到进位整数倍的效果，假如 size = 4.01MB，那么可以得到 size = 2
        // 计算线性地址对应的起始页目录项
        // 页目录是从0开始，所以对应的目录项号为 from >> 22
        // 因为每个目录项占4字节，所以实际目录项指针 = 目录项号 << 2，也即 from << 20
        // 与上 "0xffc"( & 111111111100) 是为了保证目录项指针范围有效
        dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */

        // 遍历页目录项，每个页目录项，再遍历页表项，释放对应的页表的内存
        for ( ; size-->0 ; dir++) {
                if (!(1 & *dir)) // 当前页目录没有被使用
                        continue;
                pg_table = (unsigned long *) (0xfffff000 & *dir); //取页表地址
                //遍历页表项目
                for (nr=0 ; nr<1024 ; nr++) {
                        if (1 & *pg_table) // 当前页表被使用
                                free_page(0xfffff000 & *pg_table); // 释放页表对应的内存页
                        *pg_table = 0; // 当前页表的值设置为0，没使用
                        pg_table++; // 遍历下一项页表
                }
                free_page(0xfffff000 & *dir); // 释放页表所占的内存
                *dir = 0; // 页目录表项设置为 0 
        }
        invalidate(); // 刷新 CPU 变换高速缓冲
        return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */

/*
 * 注意1！ 并不复制内存块，内存块的地址必须是 4MB ，正好是一个页目录项对应的内存长度
 *
 * 注意2！！ 当 from == 0 的时候，说明是在为第一次 fork 调用复制内核空间。此时就不用
 * 复制整个页目录项对应的内存，因为这样做会严重浪费内存，只需复制开头的 160 个页面，
 * 也就是 640 KB，在低于 1MB 的空间内不执行写时复制。因此这是 nr == xxxx 的特殊情况（nr 在程序中指页面数）
 */
/**
 * 复制指定线性地址和长度（页表个数）内存对应的页目录项和页表项，
 * 使得被复制的页目录表和页表对应的物理内存区被共享使用
 * from: 起始线性地址（4MB对齐）
 * to: 目标线性地址(4MB对齐)
 *  size: 字节长度
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
        unsigned long * from_page_table;
        unsigned long * to_page_table;
        unsigned long this_page;
        unsigned long * from_dir, * to_dir;
        unsigned long nr;

        // 检查参数 from 和 to 的有效性
        if ((from&0x3fffff) || (to&0x3fffff))
                panic("copy_page_tables called with wrong alignment"); // 无效则死机
        // 计算“起始地址”对应的“页目录项”的指针
        from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
        // 计算“目标地址”对应的“页目录项”的指针
        to_dir = (unsigned long *) ((to>>20) & 0xffc);
        // 需要拷贝的字节占用的“页面数”
        size = ((unsigned) (size+0x3fffff)) >> 22;
        //遍历页目录表
        for( ; size-->0 ; from_dir++,to_dir++) {
                if (1 & *to_dir) // 判断“目的地址”对应的“页目录项“是否已经使用
                        panic("copy_page_tables: already exist"); // 如果已经被使用，则出错死机
                if (!(1 & *from_dir)) // “源地址”对应的”页目录项“如果没使用，则继续
                        continue;
                // 计算“源地址”对应的”页表指针“
                from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
                // 为“目的地址”的”页表“申请新的一页空闲内存页，申请成功返回0
                if (!(to_page_table = (unsigned long *) get_free_page())) 
                        return -1;	/* Out of memory, see freeing */ // 申请空内存页失败，直接返回 -1

                // 设置“目的地址”的”页目录项“信息：最后三位置位 '111' (xxxx | 7)
                // 表示对应的内存页面是用户级，并且可读写，存在 (Usr, R/W, Present)
                *to_dir = ((unsigned long) to_page_table) | 7;
                nr = (from==0)?0xA0:1024; // 设置需要复制的“页表项”数，如果“起始地址”在内核空间是 160项，反之是 1024项
                // 遍历页表项
                for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
                        this_page = *from_page_table;
                        if (!(1 & this_page)) // 判断当前”源页表“是否被用
                                continue; // 没有使用，无需复制
                        this_page &= ~2; // 重置页表项的 R/W 位为 0，表明对应的内存为只读 (xxxxx & 11111111111111111111111111111101)
                        *to_page_table = this_page; // 设置“目的地址”的”页表项“
                        // 如果该页面在 1MB 上，则需要设置 mem_map[] 中对应的值
                        // 低于 1MB 无须考虑设置 mem_map[]，同时也无需考虑是否可读写，一直可读写
                        if (this_page > LOW_MEM) { 
                                *from_page_table = this_page; // “源地址” 对应的页表项也可读
                                this_page -= LOW_MEM; // 计算对应的 mem_map 中的索引号（页面数）
                                this_page >>= 12;
                                mem_map[this_page]++; // mem_map 对应的值 + 1 
                        }
                }
        }
        invalidate(); // 刷新页变换高速缓冲
        return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
/**
 * 将一页内存映射到指定的线性地址处
 * page: 分配的主内存中某一页面(页帧，页框)的指针（物理地址）
 * address：线性地址
 *
 * 返回：成功则返回页面的物理地址
 *      失败（内存不够，在访问页表或页面时）返回 0
 */

/*
 * 把线性地址空间的指定地址 address 处的页面 映射到主内存区页面 page 上。
 * 主要工作是在相关页目录项和页表项上设置指定页面的信息。 
 * 在处理缺页异常的 do_not_page() 函数中会调用此函数。  
 * 对于缺页异常，由于任何的缺页缘故造成的对页表修改，都不需要刷新 CPU 的页变换缓冲 (TLB)， 
 * 即不需要把表项中的标志 P 从 0 修改成 1（因为无效页面不会被缓冲）。
 * 因此修改了任何一个无效的页表项，都不需要刷新。在此表现为不需要调用 invalidate() 函数
 */
unsigned long put_page(unsigned long page,unsigned long address)
{
        unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */
        /* 注意!!!  这里使用了页目录表基地址 _pg_dir = 0 的条件 */

        // 检查给定物理内存页面 page 的有效性
        if (page < LOW_MEM || page >= HIGH_MEMORY) // 是否低于主内存或高于最大地址
                printk("Trying to put page %p at %p\n",page,address); // page地址无效，打印错误报警
        if (mem_map[(page-LOW_MEM)>>12] != 1) // 检查对应的内存字节映射值是否为 1，其实是检查页面是否被申请
                printk("mem_map disagrees with %p at %p\n",page,address); // 没有被申请，则打印错误报警
        page_table = (unsigned long *) ((address>>20) & 0xffc); // 计算线性地址 address 在“页目录表”中的“页目录项”的指针
        if ((*page_table)&1) // 页表在内存中
                page_table = (unsigned long *) (0xfffff000 & *page_table); // 取得页表地址，放入 page_table 变量
        else {
                if (!(tmp=get_free_page())) // 申请一页新的页表
                        return 0;
                // 最后三位置位 '111' (xxxx | 7)
                // 表示对应的内存页面是用户级，并且可读写，存在 (Usr, R/W, Present)
                *page_table = tmp|7;
                page_table = (unsigned long *) tmp; // page_table 为新申请的 内存页
        }
        
        // 找到页表中对应的页表项，把 page 地址填入
        // 同样最后三位置位 '111' (xxxx | 7) 
        page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
        
        // 无须刷新页面缓冲
        return page;
}

/**
 * 取消页面写保护(un_wp_page: Un-Write-Protect Page)
 * table_entry: 页表项地址指针，指向一个内存页面物理地址
 *
 * 无返回
 */
/*
 * 内核创建进程时，新进程与父进程被设置成共享代码和数据内存页面，并且所有这些页面被设置成”只读“页面。
 * 而当子进程或父进程想要向这些内存页面写入数据时，则会产生页面写保护异常，触发 un_wp_page 函数被调用
 * 首先检查页面是否被共享，若没有，则把页面设置成可写，退出。
 * 反之，则申请一页新的页面，然后复制写页面内存，供写进程使用，同时共享被取消
 */
void un_wp_page(unsigned long * table_entry)
{
        unsigned long old_page,new_page;

        old_page = 0xfffff000 & *table_entry; // 获得页面的物理地址
        // 页面位于主内存，并且内存映射字节图的值为 1 (只有1个进程使用，没有被共享)
        if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
                *table_entry |= 2; // 修改 R/W 位 为可写
                invalidate(); // 刷新页变换缓冲
                return;
        }
        if (!(new_page=get_free_page())) // 尝试申请一页新的内存
                oom(); // 内存不够，打印信息，死机
        if (old_page >= LOW_MEM) 
                mem_map[MAP_NR(old_page)]--; // 取消页面共享
        *table_entry = new_page | 7; // 页表项指向新分配的页面，设置最后三位是 "111"
        copy_page(old_page,new_page); // 复制老的页面的内容到新的页面
        invalidate(); // 刷新页面变换缓冲
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
/**
 * 执行写保护页面处理
 * error_code 和 address 是进程在写”写保护页面“时由 CPU 产生异常时自动生成的
 * error_code: 出错类型
 * address: 产生异常的写页面线性地址
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
        
        if (CODE_SPACE(address)) // 检查线性地址是否位于内核区
                do_exit(SIGSEGV); // 终止内核运行
#endif
// 调用 un_wp_page () 来处理写保护异常，需要计算给定的“线性地址”的“页表项”指向的“物理地址”

// ((address>>10) & 0xffc) : 计算指定的“线性地址”的“页表项”在“页表”中的“偏移地址” 
// address >> 12 就是“页表项”中的“索引”，每项占4个字节，所以乘以4，因此为 address >> 10
// 与操作 0xffc 限制地址范围在一个页面内

// (address>>20) &0xffc) : 计算指定的“线性地址”的“目录项”在“页目录表”中“偏移地址”
// *((unsigned long *) ((address>>20) &0xffc)) : 指定的“线性地址”所对应的“页目录项”指向的“页表物理地址”
//  (0xfffff000 & *((unsigned long *) ((address>>20) &0xffc)))) : 屏蔽掉“页目录项”中的一些标志位

// 上面两部分合在一起，就是指向“页表项”的指针，可以获得“页表项”指向的“物理地址”
        un_wp_page((unsigned long *)
                   (((address>>10) & 0xffc) + (0xfffff000 &
                                               *((unsigned long *) ((address>>20) &0xffc)))));
}

/**
 * 写页面验证
 * address: 页面在4GB地址空间的线性地址
 * 无返回
 */
// 若页面不可写，则复制页面。在 fork.c 中被内存验证函数 verifiy_area() 调用
void write_verify(unsigned long address)
{
        unsigned long page;

        //取得线性地址在”页目录表“中的“页目录项”的内容
        //判断对应的 P 位是否打开 
        if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
                return; // P 位如果为 0，则直接返回，因为对于不存在的页面没有共享和写时复制可言
        page &= 0xfffff000; 
        page += ((address>>10) & 0xffc); // 计算“页表项”指向的“物理地址”
        // 检查该页表项的第 1 位(R/W)，第 0 位(P)
        if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
                un_wp_page((unsigned long *) page); // 如果该页面存在(P == 1 && R/W == 0)，执行写时复制
        return;
}

/**
 * 取得一页可用内存，并映射到指定的 address 线性地址处
 * address: 线性地址
 * 无返回
 */
// get_free_page 仅仅是申请到一页内存
// get_empty_page 还要调用 put_page 映射到指定的线性地址处
void get_empty_page(unsigned long address)
{
        unsigned long tmp;

        if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
                // free_page 的参数是 0 也没关系，可以直接返回
                free_page(tmp);		/* 0 is ok - ignored */
                oom();
        }
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */

/*
 * try_to_share()在任务 "p"中检查位于地址 "address"处的页面是否存在。如果干净的话，
 * 就与当前任务共享
 * 注意！已经假设 p != 当前任务，并且两者共享同一段代码段
 */
/**
 * 尝试对当前进程指定地址处的页面进行共享处理，可以认为当前进程是被 p 进程 fork 出来的子进程，
 * 因此他们之间的代码段是一样的。 如果 p 进程的 address 处的页面存在，并且没有被修改过的话，
 * 则让 当前进程 和 p进程 共享，同时还需验证指定地址处是否已经申请了页面
 * 
 * address: 是进程中的逻辑地址
 * p：将被共享页面的进程
 *
 * 返回: 1 - 页面共享处理成功; 0 - 失败
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
        unsigned long from;
        unsigned long to;
        unsigned long from_page;
        unsigned long to_page;
        unsigned long phys_addr;

        // 计算“逻辑地址” address 对应的“逻辑页目录项“索引
        from_page = to_page = ((address>>20) & 0xffc);
        // 根据进程空间 0~64MB 计算实际的”页目录项“索引
        // (p->start_code>>20) & 0xffc : 进程 p 在 4G 线性地址空间中起始地址对应的页目录项
        from_page += ((p->start_code>>20) & 0xffc); // p进程的”页目录项“
        to_page += ((current->start_code>>20) & 0xffc); // 当前进程的”页目录项“
/* is there a page-directory at from? */
        from = *(unsigned long *) from_page; // 获得 p 进程中对应的”页目录项“内容
        if (!(from & 1)) // P == 0， 页表不存在，直接返回
                return 0;
        from &= 0xfffff000; 
        from_page = from + ((address>>10) & 0xffc); // 取得 p 进程中对应的”页表项“
        phys_addr = *(unsigned long *) from_page; // 取得 p 进程中对应的”页表项“内容
/* is the page clean and present? */
        if ((phys_addr & 0x41) != 0x01) // 0x41 代表 (D == 0 & P == 1), 存在且干净
                return 0; // 如果不存在或不干净，直接返回
        phys_addr &= 0xfffff000; 
        if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM) // 检查对应的内存物理地址是否有效
                return 0; // 无效直接返回
        to = *(unsigned long *) to_page; // 获得当前进程 ”页目录项“的内容，也就是”页表项“
        if (!(to & 1)) { // P == 0，不存在，则分配一页新的内存
                if ((to = get_free_page()))
                        *(unsigned long *) to_page = to | 7; // 设置”页目录项“的内容为新分配的地址，最后三位设为 "111"
                else
                        oom();//无法分配新的内存，出错，死机
        }
        to &= 0xfffff000; 
        to_page = to + ((address>>10) & 0xffc); // 计算对应的页表项的指针
        if (1 & *(unsigned long *) to_page) // 检查对应的页表项的是否已经存在，原来是想共享父进程的页面，但子进程要共享的地方已经占有了物理页面，程序错误
                panic("try_to_share: to_page already exists"); // 如果存在，无法共享，打印错误，死机
/* share them: write-protect */
        /* 共享，写保护*/
        *(unsigned long *) from_page &= ~2; // p 进程的”页表项“的内容中的 R/W 位置 0
        *(unsigned long *) to_page = *(unsigned long *) from_page; // 复制 p 进程的“页表项”内容到 当前进程的”页表项“处 
        invalidate(); // 刷新 CPU 页变换缓冲
        phys_addr -= LOW_MEM;
        phys_addr >>= 12;
        mem_map[phys_addr]++; // 内存字节映射图对应的页面值 加 1
        return 1; // 成功返回
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
/*
 * share_page() 试图找到一个进程可以和当前进程共享页面。参数 address 是当前进程数据空间中期望进行共享的页面地址
 * 首先我们检查 executable->i_count 是否大于1，如果有其他进程已经共享了该 inode，它的值应该大于 1 
 */
/**
 * 共享页面处理
 *
 * address: 进程中的逻辑地址，即当前进程 想和 p进程进行页面共享的逻辑页面地址
 *
 * 返回：1 - 共享成功，0 - 共享失败
 */
// 在发生缺页异常的时候，首先看看能否与运行同一个执行文件的其他进程进行页面共享处理
// 首先判断在系统中是否有另一个进程也在运行当前进程一样的执行文件：
// 若有，则在系统当前所有任务中寻找这样的任务，找到后则与这样的任务则尝试与其进行页面共享
// 反之，那么共享页面的前提不存在，则函数立刻退出
static int share_page(unsigned long address)
{
        struct task_struct ** p;

        // 首先检验下当前进程的 executable 字段是否为空
        if (!current->executable)
                return 0; // 当前进程可执行段无效，直接退出
        if (current->executable->i_count < 2) // 当前进程的可执行段在内存中 i节点引用计数值是否小于 2 
                return 0; // 小于 2 意味着只有一个进程使用这个可执行段（当前进程），无法共享，直接退出
        // 搜索整个进程队列
        for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
                // 进程元素无效，继续下一个
                if (!*p)
                        continue;
                // 当前进程，继续下一个
                if (current == *p)
                        continue;
                // 进程的可执行段和当前进程可执行段不同，继续下一个
                if ((*p)->executable != current->executable)
                        continue;
                // 尝试共享页面
                if (try_to_share(address,*p))
                        return 1;
        }
        return 0;
}

/**
 * 执行缺页处理
 *
 * error_code: 出错类型
 * address: 产生异常的页面线性地址
 * error_code, address 由进程在访问页面时由 CPU 因缺页异常而自动产生
 */
void do_no_page(unsigned long error_code,unsigned long address)
{
        int nr[4];
        unsigned long tmp;
        unsigned long page;
        int block,i;

        address &= 0xfffff000; // address 处缺页页面的地址
        tmp = address - current->start_code; // address 处的逻辑地址
        // 当前进程没有可执行段 或者 逻辑地址大于可执行段：这意味着逻辑地址处于数据段
        if (!current->executable || tmp >= current->end_data) {
                get_empty_page(address); // 动态申请一页内存页面，返回
                return;
        }
        if (share_page(tmp)) // 对于可执行段执行共享操作，
                return; // 成功则直接返回
        if (!(page = get_free_page())) // 尝试申请一页新的物理页面
                oom(); // 申请失败，则内存不够，死机
/* remember that 1 block is used for header */
        /* 记住“程序头”要使用一个“数据块” */
        block = 1 + tmp/BLOCK_SIZE; // 计算块的数量，每个块是 1024 字节
        for (i=0 ; i<4 ; block++,i++) // 读一个页面，实际上等于 4个逻辑块
                // 根据“执行文件的i节点”和”块数“，就可以从对应的“块设备”中找到对应的设备”逻辑块号“，保存在 nr[]数组 中
                nr[i] = bmap(current->executable,block); 
        bread_page(page,current->executable->i_dev,nr); // 利用 bread_page 把这四个逻辑块从设备读入到物理页面

        // 在读设备逻辑块的时候可能出现一种情况：可执行文件的读取位置到文件末尾小于 1个页面
        // 此时要清空最后那些无效的数据
        i = tmp + 4096 - current->end_data; // 计算无效的字节长度 
        tmp = page + 4096; // tmp 指向页面末端
        while (i-- > 0) { 
                tmp--;
                *(char *)tmp = 0; // 内存字节清零
        }
        // 把引起缺页异常的”物理页面“映射到指定“线性地址”处
        if (put_page(page,address))
                return; // 成功则直接返回
        // 映射失败，则释放内存页，显示内存不够，死机
        free_page(page);
        oom();
}

/**
 * 内存初始化函数：物理内存管理初始化
 * start_mem: 可用作页面分配的开始地址（已去除RAMDISK）
 * end_mem: 实际物理内存的最大地址
 */
void mem_init(long start_mem, long end_mem)
{
        int i;

        HIGH_MEMORY = end_mem; // 设置物理内存最大地址
        // 先把所有可分配的页面标志位设置为占用
        for (i=0 ; i<PAGING_PAGES ; i++)  
                mem_map[i] = USED; 
        i = MAP_NR(start_mem); // 计算可分配页面最开始的地址的页面号码
        end_mem -= start_mem; // 可用内存大小
        end_mem >>= 12; // 可用内存的页面数
        // 从可用内存的第一块页面开始到最后一块可用内存，设置 mem_map 中对应的值为0（可用）
        while (end_mem-->0) 
                mem_map[i++]=0;
}

/**
 * 打印当前内存使用状况  
 */
void calc_mem(void)
{
        int i,j,k,free=0;
        long * pg_tbl;

        // 显示主内存有多少可用的页面数
        for(i=0 ; i<PAGING_PAGES ; i++)
                if (!mem_map[i]) free++;
        printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
        //　打印每个页目录表中对应的目表项占用的页面数 
        for(i=2 ; i<1024 ; i++) { // 从２开始遍历，因为１给了进程０使用
                // pg_dir[i] = 1, 代表这个页目录项被使用
                if (1 & pg_dir[i]) {
                        // 取得页目录对应的页面表的地址
                        pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
                        // 遍历页面表
                        for(j=k=0 ; j<1024 ; j++)
                                if (pg_tbl[j]&1) // 页面被使用
                                        k++;
                        printk("Pg-dir[%d] uses %d pages\n",i,k);
                }
        }
}
