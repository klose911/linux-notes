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

static inline void wait_on_buffer(struct buffer_head * bh)
{
        cli();
        while (bh->b_lock)
                sleep_on(&bh->b_wait);
        sti();
}

int sys_sync(void)
{
        int i;
        struct buffer_head * bh;

        sync_inodes();		/* write out inodes into buffers */
        bh = start_buffer;
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                wait_on_buffer(bh);
                if (bh->b_dirt)
                        ll_rw_block(WRITE,bh);
        }
        return 0;
}

int sync_dev(int dev)
{
        int i;
        struct buffer_head * bh;

        bh = start_buffer;
        for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
                if (bh->b_dev != dev)
                        continue;
                wait_on_buffer(bh);
                if (bh->b_dev == dev && bh->b_dirt)
                        ll_rw_block(WRITE,bh);
        }
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
                        bh->b_uptodate = bh->b_dirt = 0;
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

#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]

static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
        if (bh->b_next)
                bh->b_next->b_prev = bh->b_prev;
        if (bh->b_prev)
                bh->b_prev->b_next = bh->b_next;
        if (hash(bh->b_dev,bh->b_blocknr) == bh)
                hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
        if (!(bh->b_prev_free) || !(bh->b_next_free))
                panic("Free block list corrupted");
        bh->b_prev_free->b_next_free = bh->b_next_free;
        bh->b_next_free->b_prev_free = bh->b_prev_free;
        if (free_list == bh)
                free_list = bh->b_next_free;
}

static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
        bh->b_next_free = free_list;
        bh->b_prev_free = free_list->b_prev_free;
        free_list->b_prev_free->b_next_free = bh;
        free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
        bh->b_prev = NULL;
        bh->b_next = NULL;
        if (!bh->b_dev)
                return;
        bh->b_next = hash(bh->b_dev,bh->b_blocknr);
        hash(bh->b_dev,bh->b_blocknr) = bh;
        bh->b_next->b_prev = bh;
}

static struct buffer_head * find_buffer(int dev, int block)
{		
        struct buffer_head * tmp;

        for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
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
struct buffer_head * get_hash_table(int dev, int block)
{
        struct buffer_head * bh;

        for (;;) {
                if (!(bh=find_buffer(dev,block)))
                        return NULL;
                bh->b_count++;
                wait_on_buffer(bh);
                if (bh->b_dev == dev && bh->b_blocknr == block)
                        return bh;
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
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
struct buffer_head * getblk(int dev,int block)
{
        struct buffer_head * tmp, * bh;

repeat:
        if ((bh = get_hash_table(dev,block)))
                return bh;
        tmp = free_list;
        do {
                if (tmp->b_count)
                        continue;
                if (!bh || BADNESS(tmp)<BADNESS(bh)) {
                        bh = tmp;
                        if (!BADNESS(tmp))
                                break;
                }
/* and repeat until we find something good */
        } while ((tmp = tmp->b_next_free) != free_list);
        if (!bh) {
                sleep_on(&buffer_wait);
                goto repeat;
        }
        wait_on_buffer(bh);
        if (bh->b_count)
                goto repeat;
        while (bh->b_dirt) {
                sync_dev(bh->b_dev);
                wait_on_buffer(bh);
                if (bh->b_count)
                        goto repeat;
        }
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
        if (find_buffer(dev,block))
                goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
        bh->b_count=1;
        bh->b_dirt=0;
        bh->b_uptodate=0;
        remove_from_queues(bh);
        bh->b_dev=dev;
        bh->b_blocknr=block;
        insert_into_queues(bh);
        return bh;
}

void brelse(struct buffer_head * buf)
{
        if (!buf)
                return;
        wait_on_buffer(buf);
        if (!(buf->b_count--))
                panic("Trying to free free buffer");
        wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
struct buffer_head * bread(int dev,int block)
{
        struct buffer_head * bh;

        if (!(bh=getblk(dev,block)))
                panic("bread: getblk returned NULL\n");
        if (bh->b_uptodate)
                return bh;
        ll_rw_block(READ,bh);
        wait_on_buffer(bh);
        if (bh->b_uptodate)
                return bh;
        brelse(bh);
        return NULL;
}

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
void bread_page(unsigned long address,int dev,int b[4])
{
        struct buffer_head * bh[4];
        int i;

        for (i=0 ; i<4 ; i++)
                if (b[i]) {
                        if ((bh[i] = getblk(dev,b[i])))
                                if (!bh[i]->b_uptodate)
                                        ll_rw_block(READ,bh[i]);
                } else
                        bh[i] = NULL;
        for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
                if (bh[i]) {
                        wait_on_buffer(bh[i]);
                        if (bh[i]->b_uptodate)
                                COPYBLK((unsigned long) bh[i]->b_data,address);
                        brelse(bh[i]);
                }
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */
struct buffer_head * breada(int dev,int first, ...)
{
        va_list args;
        struct buffer_head * bh, *tmp;

        va_start(args,first);
        if (!(bh=getblk(dev,first)))
                panic("bread: getblk returned NULL\n");
        if (!bh->b_uptodate)
                ll_rw_block(READ,bh);
        while ((first=va_arg(args,int))>=0) {
                tmp=getblk(dev,first);
                if (tmp) {
                        if (!tmp->b_uptodate)
                                ll_rw_block(READA,bh);
                        tmp->b_count--;
                }
        }
        va_end(args);
        wait_on_buffer(bh);
        if (bh->b_uptodate)
                return bh;
        brelse(bh);
        return (NULL);
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
