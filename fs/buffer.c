/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */

/**
 * 'buffer.c' 用于实现缓冲区高速缓冲功能
 * 为了避免竞争条件，有意不让中断处理过程来分配和回收缓冲区（除了修改缓冲区内数据），而是让调用者来修改缓冲区
 *
 * 注意：由于中断可以唤醒一个调用者，所以这里需要使用cli-sti指令检测是否由于睡眠而休眠，当然关闭，重开中断需要相当相当快（我希望是这样！）
 */
#include <stdarg.h> // 标准参数头文件
 
#include <linux/config.h> // 内核配置头文件，定义键盘语言，硬盘类型(HD_TYPE)等
#include <linux/sched.h> // 内核调度头文件
#include <linux/kernel.h> // 内核头文件
#include <asm/system.h> // 段操作符等定义
#include <asm/io.h> // io 操作头文件，定义了端口读写宏

/*
 * 变量 end 由链接程序 ld 生成，用于指明内核代码（内核模块）的末端（也可以从内核编译后生成的 System.map 文件中查出）
 */
extern int end;
extern void put_super(int);
extern void invalidate_inodes(int);

/*
 * 缓冲头结构指针：指向高速缓冲区的开始（内核代码的末端）
 */
struct buffer_head * start_buffer = (struct buffer_head *) &end;

/* 高速缓冲头对应的 hash 表
 * 哈希函数：(设备号 ^ 逻辑块号) Mod NR_HASH, 实际上就是这个数组的下标
 * 这个数组的每一项：一个双向“缓冲头结构指针”的链表
 */
struct buffer_head * hash_table[NR_HASH];

/*
 * “空闲缓冲块”双向链表的表头指针
 * 注意：实际上并不是完全空闲，只有其中的 b_lock = 0 & b_count = 0 才可以被认为是完全空闲
 */
static struct buffer_head * free_list;

/*
 * 等待空闲缓冲块而休眠的任务队列头指针
 * 注意：buffer_wait 是为了申请一个空闲缓冲块而正好遇到缺乏可用缓冲块时候，当前任务就会被添加这个队列中
 *      b_wait 是专门为了供等待某个特定的缓冲块的队列头指针！！！
 */
static struct task_struct * buffer_wait = NULL;

/*
 * 系统缓冲区中缓冲块的个数
 * 
 * NR_BUFFERS 是一个定义在 linux/fs.h 中的宏，其值即是变量 nr_buffers，并被声明为一个全局变量
 * 这里使用大写宏来定义变量，是为了强调 nr_buffers 是一个常数，并且在初始化以后就不再被改变！！！
 */
int NR_BUFFERS = 0;

/*
 * 等待特定的缓冲块解锁
 *
 * bh: 特定的缓冲块头指针
 * 无返回值
 *
 * 如果指定的缓冲块 bh 已经上锁，就让当前进程不可中断地休眠在该缓冲块的等待队列 b_wait 中
 * 在缓冲块解锁时，其等待队列上的所有进程都会被唤醒
 * 
 */
static inline void wait_on_buffer(struct buffer_head * bh)
{
        // 虽然这里关闭了中断，但并不会影响其他上下文响应中断!!! 
        // 因为每个进程都在自己的 TSS 段上保存了自己的 EFLAGS 字段，而在进程切换时 CPU 中当前 EFLAGS 会随之自动切换
        cli(); // 关中断
        while (bh->b_lock) // 如果缓冲块已经被上锁，则进程进入不可中断地睡眠，等待其解锁 
                sleep_on(&bh->b_wait); // 使用 sleep_on 进入睡眠状态，需要调用 wake_on 明确地唤醒
        sti(); // 开中断
}

/**
 * 同步设备和高速缓冲块的数据
 *
 * 无参数
 * 成功返回 0 
 */
int sys_sync(void)
{
        int i;
        struct buffer_head * bh;

        // 调用 i节点 同步函数，把“内存i节点表”中所有修改过的“i节点”写入高速缓冲区中
        sync_inodes();		/* write out inodes into buffers */ // sync_inodes函数定义在 inodes.c 文件中
        bh = start_buffer; // bh 指向缓冲区开始处
        // 扫描所有的高速缓冲区
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                wait_on_buffer(bh); // 等待缓冲区解锁（如果已经上锁）
                if (bh->b_dirt) // 如果缓冲块已经被修改
                        ll_rw_block(WRITE,bh); // 产生写设备块请求（ll_rw_block 由块设备驱动提供，定义在 ll_rw_blk.c ）
        }
        return 0;
}

/**
 * 对指定设备进行高速缓冲区数据与设备上数据进行同步
 *
 * dev: 设备号
 * 返回：0
 */
int sync_dev(int dev)
{
        int i;
        struct buffer_head * bh;

        // 首先对高速缓冲区内所有该设备的缓冲块进行同步
        bh = start_buffer;
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                if (bh->b_dev != dev)
                        continue;
                wait_on_buffer(bh);
                // 因为在休眠期间，可能该缓冲块已经被释放，或者挪作它用，所以需要再次检查 dev 设备号
                if (bh->b_dev == dev && bh->b_dirt)
                        ll_rw_block(WRITE,bh);
        }
        // 同步“内存中的i节点表”后，再次对高速缓冲区内所有该设备的缓冲块进行同步
        // 这里采用两遍操作是为了提高内核效率：第一遍同步操作，可以让内核中许多“脏块”变干净，使得 i节点的同步操作能够提高效率
        // 这里的第二次操作则把那些由于 i 节点同步操作而又变脏的缓冲块与设备中数据进行同步！！！ 
        sync_inodes();
        bh = start_buffer;
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                if (bh->b_dev != dev)
                        continue;
                wait_on_buffer(bh);
                if (bh->b_dev == dev && bh->b_dirt)
                        ll_rw_block(WRITE,bh);
        }
        return 0;
}

/**
 * 使指定设备中的高速缓冲块数据无效
 *
 * dev: 设备号
 * 无返回值
 */
void invalidate_buffers(int dev)
{
        int i;
        struct buffer_head * bh;

        bh = start_buffer;
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                if (bh->b_dev != dev)
                        continue;
                wait_on_buffer(bh);
                if (bh->b_dev == dev) 
                        bh->b_uptodate = bh->b_dirt = 0; // 缓冲块对应的有效标志，修改标志都设为0
        }
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */
void check_disk_change(int dev)
{
        int i;

        if (MAJOR(dev) != 2)
                return;
        if (!floppy_change(dev & 0x03))
                return;
        for (i=0 ; i<NR_SUPER ; i++)
                if (super_block[i].s_dev == dev)
                        put_super(super_block[i].s_dev);
        invalidate_inodes(dev);
        invalidate_buffers(dev);
}

/**
 * 下面两行代码是 hash 函数定义和 hash 表项的计算宏
 * 
 * hash 表的作用是减少查找比较元素所花费的时间。通过在元素的存储位置与关键字之间建立一个对应关系（hash函数），就可以直接通过函数计算立刻查询到指定的元素
 * 
 * 建立 hash 函数的条件是：尽量确保散列到任何数组项的概率基本相等。这里采用了关键字除以一个质数求余法
 * 因为寻找的缓冲块有两个条件：设备号 dev 和逻辑块号 block，因此 hash 函数一定需要这两个条件
 * 这里使用的是 dev 和 block 异或操作，然后再取余，保证计算的值都在 hash 表数组项之内
 * 
 */
// 根据“设备号dev”和“逻辑块号block”计算 hash 值
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
// 根据“设备号dev”和“逻辑块号block” 获得对应的 hash 表的数组项
#define hash(dev,block) hash_table[_hashfn(dev,block)] 

/*
 * 从 “hash 队列”和 “空闲缓冲队列”移走“缓冲块”
 *
 * bh: 特定的缓冲块（缓冲头结构指针）
 *
 * hash 队列：双向链表结构，空闲缓冲队列：双向循环链表结构
 */
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
        // 从 hash 队列移除掉 bh 缓冲块
        if (bh->b_next)
                bh->b_next->b_prev = bh->b_prev; 
        if (bh->b_prev)
                bh->b_prev->b_next = bh->b_next; 
        // hash(bh->b_dev,bh->b_blocknr) 用来计算这个hash队列头指针，用来和bh比较，来确定 bh 是否是头一个元素
        if (hash(bh->b_dev,bh->b_blocknr) == bh) 
                hash(bh->b_dev,bh->b_blocknr) = bh->b_next; // 如果 bh 是该 hash 队列的头一个块，则让 hash 表的对应项指向本队列的下一个缓冲区  
/* remove from free list */
        
        // 从空闲队列中移除 bh 缓冲块
        if (!(bh->b_prev_free) || !(bh->b_next_free))
                panic("Free block list corrupted");
        bh->b_prev_free->b_next_free = bh->b_next_free; 
        bh->b_next_free->b_prev_free = bh->b_prev_free;
        // 如果空闲链表头指向本缓冲块，则让他指向下一个缓冲块
        if (free_list == bh)
                free_list = bh->b_next_free;
}

/*
 * 将缓冲块插入“空闲链表”尾部，同时放入 hash 队列中
 *
 * bh: 特定的缓冲块
 */
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
        // 放入空闲链表尾部
        bh->b_next_free = free_list;
        bh->b_prev_free = free_list->b_prev_free;
        free_list->b_prev_free->b_next_free = bh;
        free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
        // 如果该缓冲块对应一个设备则将其插入到hash队列中
        bh->b_prev = NULL;
        bh->b_next = NULL;
        if (!bh->b_dev)
                return;
        /*
         * 请注意：当 hash 表某散列项第1次插入项时， hash宏计算的值肯定为 NULL（见 buffer_init 函数最后）
         * 也就是这里的 bh-> b_next 的值会是 NULL 
         */
        bh->b_next = hash(bh->b_dev,bh->b_blocknr); // 缓冲块的”后一个hash队列项”指向“hash队列的头”
        hash(bh->b_dev,bh->b_blocknr) = bh; // “hash队列的头”指向“缓冲块 bh”
        if(bh->b_next) // 必须判断这个散列项是否第一次插入项！！！
                bh->b_next->b_prev = bh; // 缓冲块的”后一个hash队列项”的“前一个 hash 队列项”指向“缓冲块 bh”
}

/*
 * 利用 hash 表在高速缓冲中寻找给定设备和指定逻辑块号的缓冲区块
 *
 * dev: 设备号
 * block: 逻辑块号
 * 如果找到：返回对应缓冲区块的指针，找不到：返回 NULL 
 */
static struct buffer_head * find_buffer(int dev, int block)
{		
        struct buffer_head * tmp;

        // 根据设备号和逻辑块号计算hash值，遍历对应hash值的“散列项”（双向队列）
        for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
                // 寻找匹配对应设备号和逻辑块号的缓冲块
                if (tmp->b_dev==dev && tmp->b_blocknr==block)
                        return tmp;
        return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
/*
 * 代码为什么会是这样的，原因是竞争条件！！！
 * 由于没有对缓冲块进行加锁（除非在读取他们中的数据），因此在进程睡眠的时候，可能会发生一些问题（例如一个读错误，导致该缓冲块出错）
 * 目前这种情况实际上还没发生，但是代码已经准备好了 
 */

/*
 * 利用 hash 表在高速缓冲区寻找特定的缓冲块，如果找到则等待该缓冲块解锁后再次校验通过后，才返回缓冲块头指针！！！
 * 
 * dev: 设备号
 * block: 逻辑块号
 * 如果找到：返回对应缓冲区块的指针，找不到：返回 NULL  
 */
struct buffer_head * get_hash_table(int dev, int block)
{
        struct buffer_head * bh;

        for (;;) {
                if (!(bh=find_buffer(dev,block)))
                        return NULL; // 找不到，则直接返回 NULL 
                bh->b_count++; // 对该缓冲块的引用计数 + 1 
                wait_on_buffer(bh); // 等待该缓冲块解锁
                if (bh->b_dev == dev && bh->b_blocknr == block) // 再次判断缓冲块是否还是寻找的
                        return bh;
                // 如果在睡眠状态，该缓冲块所属的设备号，逻辑块已经发生改变，则撤消对它的引用计数，重新寻找
                bh->b_count--;
        }
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */

/*
 * 接下来是 getblk 函数。为了防止竞争条件，它并不是非常清晰
 * 绝大部分代码几乎很少被使用（比如 repate: ） 因此它比看起来要高效得多
 *
 * 算法已经被修改过了：希望它能变得更好，并且一个隐藏的很深的错误被修正了
 * 
 */

// 下面宏用于同时判断缓冲块的“修改标志”和“锁定标志”，并且通过左移位运算使得“修改标志”的权重要比“锁定”标志大
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)

/**
 * 取高速缓冲中的指定缓冲块
 *
 * dev: 设备号
 * block: 逻辑块号
 *
 * 返回：相应的高速缓冲头指针
 * 
 */
struct buffer_head * getblk(int dev,int block)
{
        struct buffer_head * tmp, * bh;

repeat:
        // 搜索 hash 表，如果指定块已经在缓冲中，则返回对应的缓冲头指针，退出
        if ((bh = get_hash_table(dev,block)))
                return bh;

        // 扫描空闲双向循环列表，寻找空闲缓冲块区
        tmp = free_list; // 首先，让 tmp 执行空闲列表的表头
        do {
                // 当一个缓冲区引用计数为0时候，并不一定意味者该缓冲块是干净的(b_dirty = 0) 或者 没有锁定的(b_lock = 0)
                // 例如：当一个进程改写过一块内存时，就释放了，于是该 b_count = 0，但 b_dirty != 0
                // 或者 当一个进程执行 breada()预读几个块时，只要 ll_rw_block()命令发出后，它就会递减 b_count，但此时实际硬盘访问可能操作还在进行，因此此时 b_lock = 1，但 b_count = 0  
                if (tmp->b_count) // 如果该缓冲区正被使用（引用计数不等于 0），则继续扫描下一项
                        continue;
                // 如果 bh 指针为空 或者 tmp 所指向的缓冲块的权重标志小于 bh 头标志的权重
                if (!bh || BADNESS(tmp)<BADNESS(bh)) { // 修改的权重大于锁定的权重，例如：tmp只有锁定标志，而bh有修改标志
                        bh = tmp; // bh 指向 tmp 指向的缓冲头
                        if (!BADNESS(tmp)) // tmp 即没有修改标志，也没有锁定标志，则退出循环
                                break;
                }
                // 否则继续循环，看看能否找到一个 BADNESS 最小（最合适）的缓冲块
/* and repeat until we find something good */
        } while ((tmp = tmp->b_next_free) != free_list);

        // 遍历全部空闲列表，仍然无法找到对应的缓冲块（所有的缓冲块的引用计数都 > 0）
        if (!bh) {
                sleep_on(&buffer_wait); // 当前进程进入不可中断的睡眠等待有空闲块可以用，注意：是针对整个空闲队列(buffer_wait)的等待
                // 当有空闲块可以用时，进程会被明确唤醒
                goto repeat; // 唤醒后，从头开始遍历整个空闲列表
        }

        // 执行到这里说明已经找到合适的缓冲块
        wait_on_buffer(bh); // 如果该缓冲块上锁了，先不可中断地睡眠等待该缓冲块解锁。注意：此时是针对该缓冲块(bh->b_wait)的队列等待
        if (bh->b_count) // 如果在唤醒后，该缓冲块又被其他进程占用
                goto repeat; // 只能从开始搜索合适的缓冲块 :-( 
        
        // 如果该缓冲块已被修改
        while (bh->b_dirt) {
                sync_dev(bh->b_dev); // 将数据写盘
                wait_on_buffer(bh); // 再次等待该缓冲区解锁
                if (bh->b_count) // 如果在唤醒后，该缓冲块又被其他进程占用
                        goto repeat; // 只能从开始搜索合适的缓冲块 :-(
        }
        
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
        // 是否乘前面的睡眠时候，该缓冲块已经被其他进程加入到高速缓冲区了
        if (find_buffer(dev,block)) 
                goto repeat; // 再次重复上述查找过程
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */

        // 现在最终找到一块合适的缓冲块了：未使用的(b_count = 0)，没上锁的(b_lock=0)，干净的 (b_dirty = 0) 
        bh->b_count=1; // 引用计数  = 1 
        bh->b_dirt=0; // 修改标志 = 0
        bh->b_uptodate=0; // 有效（更新）标志 = 0
        // 从 hash队列 和 空闲列表移出该缓冲头
        remove_from_queues(bh); 
        bh->b_dev=dev; // 设置该缓冲块的设备号
        bh->b_blocknr=block; // 设置该缓冲块的逻辑块号
        // 根据刚刚设置的设备号和逻辑块号，重新插入空闲链表的头部， hash表对应散列项队列的头部
        insert_into_queues(bh);
        return bh; // 返回该缓冲头指针
}

/**
 * 释放某个高速缓冲块
 *
 * buf: 缓冲头指针
 *
 * 无返回值
 */
void brelse(struct buffer_head * buf)
{
        if (!buf) // 缓冲块为空指针，直接返回
                return;
        wait_on_buffer(buf); // 等待该缓冲块解锁
        // 该缓冲块的引用计数 - 1 
        if (!(buf->b_count--)) // 如果引用计数不为0，则直接异常退出
                panic("Trying to free free buffer");
        // 明确唤醒”等待空闲块“(buffer_wait)的进程队列中的所有进程!!!
        wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */

/**
 * 从设备上读取指定的数据块到高速缓冲区
 *
 * dev: 设备号
 * block: 逻辑块号
 *
 * 返回：存储该缓冲块的头指针，如果该块不存在则返回 NULL 
 */
struct buffer_head * bread(int dev,int block)
{
        struct buffer_head * bh;

        
        if (!(bh=getblk(dev,block))) // 在高速缓冲区中申请一块空闲缓冲块来存储读入的数据
                panic("bread: getblk returned NULL\n"); // 无法申请到空闲的缓冲块，内核出错，停机
        if (bh->b_uptodate) // 如果申请到的缓冲块的数据是有效的（已更请的，可用的），直接返回该缓冲块
                return bh;
        
        ll_rw_block(READ,bh);// 调用底层块设备读写 ll_rw_block() 函数，产生读设备请求
        wait_on_buffer(bh); // 缓冲块上锁（等待数据读入）
        if (bh->b_uptodate) // 睡眠醒来后，如果缓存块数据已经有效，则返回缓冲头指针
                return bh;

        // 读取设备的操作失败，释放该缓冲区，返回 NULL 
        brelse(bh);
        return NULL;
}

// 复制缓冲块，从“from地址”复制一块1024字节的数据到“to地址”
#define COPYBLK(from,to)                                    \
        __asm__("cld\n\t"                                   \
                "rep\n\t"                                   \
                "movsl\n\t"                                 \
                ::"c" (BLOCK_SIZE/4),"S" (from),"D" (to)    \
                )

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */

/**
 * 一次读4个缓冲块数据到指定内存地址处
 *
 * address: 保存页面数据的物理地址
 * dev: 设备号
 * b[4]: 4个设备数据块号的数组
 *
 * 无返回值
 *
 * 该函数仅用 mm/memory.c 的 do_no_page() 函数中
 * 
 */
void bread_page(unsigned long address,int dev,int b[4])
{
        struct buffer_head * bh[4];
        int i;

        // 循环执行4次
        for (i=0 ; i<4 ; i++) {
                if (b[i]) { // 数组中的逻辑块号有效
                        if ((bh[i] = getblk(dev,b[i]))) // 申请空闲高速缓冲块
                                if (!bh[i]->b_uptodate) // 判断申请到的缓冲块内容是否有效
                                        ll_rw_block(READ,bh[i]); // 内容无效发起读设备请求
                } else
                        bh[i] = NULL; // 逻辑块号无效，则不理会
        }

        // 将读取到高速缓冲区的数据复制到指定的页面地址处
        for (i=0 ; i<4 ; i++,address += BLOCK_SIZE) {
                if (bh[i]) {
                        wait_on_buffer(bh[i]); // 复制前先让进程等待读请求做完
                        if (bh[i]->b_uptodate) // 睡眠醒来（读请求做完），再次判断缓冲块的内容是否有效（更新的）
                                COPYBLK((unsigned long) bh[i]->b_data,address); // 拷贝缓冲块数据到指定内存地址
                        brelse(bh[i]); // 拷贝完毕，释放对应的高速缓存区
                }
        }
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */

/**
 * 从指定设备额外读取指定的一些块
 *
 * dev: 设备号
 * first: 逻辑块号1 注意：这是一个变长参数，以一个负数结尾！
 *
 * 成功：返回第一块缓冲头指针，反之：返回 NULL
 *
 * breada 可以像 bread 一样使用，但是会额外预读一些块
 * 
 */
struct buffer_head * breada(int dev,int first, ...)
{
        va_list args;
        struct buffer_head * bh, *tmp;

        va_start(args,first); // 取可变参数表的第一个参数 first 
        if (!(bh=getblk(dev,first))) // 为第一个逻辑块号申请一块空闲缓冲块
                panic("bread: getblk returned NULL\n");
        if (!bh->b_uptodate) // 如果缓冲块的内容不更新
                ll_rw_block(READ,bh); // 发起读请求
        // 遍历可变参数表，依次取一个参数，直到取到负数作为结束（因为逻辑块号不能为负）
        while ((first=va_arg(args,int))>=0) {
                // 注意： 这里也没有强制要求预读块和第一块在链表上有前后关系，以后可以通过hash表快速定位，所以没必要
                tmp=getblk(dev,first); // 申请空闲缓冲块
                if (tmp) {
                        if (!tmp->b_uptodate) // 申请到的空闲缓冲块内容无效
                                ll_rw_block(READA,tmp); // 发起读请求
                        tmp->b_count--; // 暂时释放掉该预读块，因为现在没人使用
                }
        }
        va_end(args); // 结束遍历可变参数表
        wait_on_buffer(bh); // 进程进入睡眠，等待第一块缓冲块的读取完毕
        if (bh->b_uptodate) // 再次判断读入的数据是否有效
                // 注意：因为现在并不需要预读的那些数据块，所以并没有进入睡眠来等待预读请求全部完成以后才返回
                return bh; // 有效则直接返回对应的缓冲头指针
        brelse(bh); // 无效，则释放缓冲块，返回 NULL 
        return NULL;
}

/**
 * 初始化高速缓冲区
 *
 * buffer_end: 具有16MB内存的系统，其值是4MB，对于8MB内存的系统，其值是2MB
 * 无返回值
 *
 * 从 buffer_start 和 buffer_end 处开始同时初始化“缓冲块头结构”和对应的“数据块”
 */
void buffer_init(long buffer_end)
{
        struct buffer_head * h = start_buffer;
        void * b;
        int i;

        // 根据缓冲区的高端位置来确定实际缓冲区的高端位置 b 
        if (buffer_end == 1<<20) // 如果缓冲区高端位置等于1MB
                b = (void *) (640*1024); // 因为从 640KB ~ 1MB 之间的内存要被显存和BIOS占用，所以实际高速缓冲区的高端位置只能是 640KB 
        else
                b = (void *) buffer_end;
        // 从缓冲区高端开始划分 1KB 大小的“缓冲块”，与此同时在缓冲区低端建立描述该数据块的“缓冲块头结构”
        // h 是指向缓冲头结构的指针，而 h + 1 是指向内存地址连续的下一个缓冲头的地址（指向缓冲头结构末端地址）
        // 为了保证有足够长度的内存来存储一个缓冲头结构，b指向的内存块地址 必须大于等于 当前缓冲头结构的末端地址 (h + 1)
        while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
                h->b_dev = 0; // 设备号
                h->b_dirt = 0; // 脏标志
                h->b_count = 0; // 引用计数标志
                h->b_lock = 0; // 锁定标志
                h->b_uptodate = 0; // 是否有效标志
                h->b_wait = NULL; // 等待该缓冲块的队列头指针
                h->b_next = NULL; // 指向下一个相同 hash 值的缓冲头指针
                h->b_prev = NULL; // 指向前一个相同 hash 值的缓冲头指针
                h->b_data = (char *) b; // 指向对应数据块的（1024字节）
                h->b_prev_free = h-1; // 指向空闲缓冲头链表中的前一项
                h->b_next_free = h+1; // 指向空闲缓冲头链表中的下一项
                h++; // h 指向下一块缓冲头
                NR_BUFFERS++; // 缓冲区缓冲块个数 + 1 
                if (b == (void *) 0x100000) // 若 b 递减到 1MB，则跳过 384KB 
                        b = (void *) 0xA0000; // 让 b 执行 640KB (0xA0000)处
        }
        
        // 形成了一个双向环形链表！！！
        h--; // h 指向最后一个有效缓冲头
        free_list = start_buffer; // “空闲缓冲头链表”的“链表头”设为“高速缓冲区”开始处
        free_list->b_prev_free = h; // “空闲缓冲头链表”的“表头“中的“前一个指针”(b_prev_free)指向这个链表尾
        h->b_next_free = free_list; // “空闲缓冲头链表”的“表尾”中的“后一个指针”(b_next_free))指向这个链表头
        // 初始化缓冲头哈希表
        for (i=0;i<NR_HASH;i++)
                hash_table[i]=NULL; // 设置每个哈希数据项对应的双向链表数组为空
}	
