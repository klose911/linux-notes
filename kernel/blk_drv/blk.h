#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7 // 块设备类型数量
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */

/*
 * NR_REQUEST 是请求队列中的项数
 * 注意：写请求仅使用这些项中低2/3部分，读操作优先处理
 *
 * 32 看起来是一个比较合理的数字，足够从电梯算法获取利益，当缓冲区在请求队列中锁住时也不是很大的数字
 * 64 显得太大了，当大量的写/同步操作进行时容易引起长时间的暂停
 * 
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */

/**
 * 请求项的结构
 *
 * 注意：在分页请求中使用同样的request结构：在分页处理中 'bh'是 NULL ，'waiting'则用于等待”读/写“的完成
 * 
 */
struct request {
        int dev; // 发送请求的设备号， -1 表示该项没有被使用
        int cmd; // READ 或 WRITE命令 
        int errors; // 操作时产生的错误次数
        unsigned long sector; // 操作的起始扇区号（1块=2扇区）
        unsigned long nr_sectors; // 读/写扇区数
        char * buffer; // 数据缓冲区
        struct task_struct * waiting; // 等待请求完成的进程队列
        struct buffer_head * bh; // 高速缓冲区头指针
        struct request * next; // 指向下一个请求项，NULL表示当前是最后一项
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */

/**
 * 电梯算法的排序宏：ll_rw_blk.c中的add_request函数使用
 *
 * s1, s2 都是 requests指针，根据其中的信息(cmd, dev, sector) 来判断处理两个请求的先后顺序（访问块设备的请求项执行顺序）
 * 优先级：cmd, dev, sector, 值越小，优先级越高
 * 
 * 注意：读总是在写前面，这是很自然的，因为读对时间的要求比写严格的多
 *
 */
#define IN_ORDER(s1,s2)                                                 \
        ((s1)->cmd<(s2)->cmd || ((s1)->cmd==(s2)->cmd &&                \
                                 ((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
                                                            (s1)->sector < (s2)->sector))))
/**
 * 块设备项的结构
 * 
 */
struct blk_dev_struct {
        void (*request_fn)(void); // 请求处理函数指针，硬盘是 do_hd_request，内存盘是 do_rd_request，软盘是 do_floppy_request 
        struct request * current_request; // 当前处理的请求项指针
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV]; // 全局块设备表，每种块设备各占用一项，共7项
extern struct request request[NR_REQUEST]; // 全局请求队列数组，总共32项
extern struct task_struct * wait_for_request; // 等待空闲请求项的进程队列头指针

// 在块设备驱动程序(如hd.c)中包含此头文件，必须先定义驱动程序处理的主设备号
// 下面的代码会根据主设备号给出正确的宏定义
#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1) // 内存盘
/* ram disk */
// 虚拟盘无须开启和关闭
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request // 请求处理函数
#define DEVICE_NR(device) ((device) & 7) // 设备号 0~7
#define DEVICE_ON(device) // 开启设备
#define DEVICE_OFF(device) // 关闭设备

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3) // 硬盘 
/* harddisk */
// 硬盘也无须开启和关闭
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd // 硬盘中断处理函数
#define DEVICE_REQUEST do_hd_request // 硬盘请求处理函数 
#define DEVICE_NR(device) (MINOR(device)/5) // 设备号
#define DEVICE_ON(device) // 开启设备
#define DEVICE_OFF(device) // 关闭设备

#elif
/* unknown blk device */
// 否则在编译预处理阶段显示出错信息：”未知块设备“
#error "unknown blk device" 

#endif

#define CURRENT (blk_dev[MAJOR_NR].current_request) // 某个设备的当前请求项指针
#define CURRENT_DEV DEVICE_NR(CURRENT->dev) // 当前请求项指针 CURRENT 中的dev域

// 如果申明了”设备中断处理“符号常数，则把它申明为一个不带参数，且无返回的函数指针，并默认为NULL 
#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif

/*
 * 声明”设备请求“符号常数(DEVICE_REQUEST) 是一个不带参数，且无返回的静态函数指针 
 */
static void (DEVICE_REQUEST)(void);

/*
 * 解锁高速缓冲块
 *
 * bh: 高速缓冲块头指针
 * 
 */
static inline void unlock_buffer(struct buffer_head * bh)
{
        if (!bh->b_lock) // 高速缓冲块未锁定
                printk(DEVICE_NAME ": free buffer being unlocked\n");
        bh->b_lock=0; // 复位“高速缓冲块”的“锁定”标志
        wake_up(&bh->b_wait); // 唤醒等待高速缓冲块的进程队列
}

/*
 * 结束当前块请求
 *
 * uptodate: 更新标志，如果为 0， 则打印出错信息
 * 
 */
static inline void end_request(int uptodate)
{
        DEVICE_OFF(CURRENT->dev); // 关闭当前请求对应的设备
        if (CURRENT->bh) { // 当前请求的"高速缓冲块头指针"不为NULL
                CURRENT->bh->b_uptodate = uptodate; // 置位“高速缓冲块头指针”的“更新”标志
                unlock_buffer(CURRENT->bh); // 解锁高速缓冲块
        }
        if (!uptodate) { // 打印错误信息
                printk(DEVICE_NAME " I/O error\n\r");
                printk("dev %04x, block %d\n\r",CURRENT->dev,
                       CURRENT->bh->b_blocknr);
        }
        wake_up(&CURRENT->waiting); // 唤醒等待“该读写请求项”的进程
        wake_up(&wait_for_request); // 唤醒等待“获取空闲请求项”的进程
        CURRENT->dev = -1; // 释放该读写请求项：dev = -1 表示该请求项“空闲” 
        CURRENT = CURRENT->next; // 当前请求项指向下一个
}

// 初始化请求项宏：用于对当前请求项进行一些有效性的判断
// 1. 当前请求项为空：直接返回
// 2. 当前请求项的设备号 ！= 驱动程序定义的设备号：报错，死机
// 3. 当前请求项对应的高速缓冲块没有被锁定：报错，死机
#define INIT_REQUEST                                                \
        repeat:                                                     \
        if (!CURRENT)                                               \
                return;                                             \
        if (MAJOR(CURRENT->dev) != MAJOR_NR)                        \
                panic(DEVICE_NAME ": request list destroyed");      \
        if (CURRENT->bh) {                                          \
                if (!CURRENT->bh->b_lock)                           \
                        panic(DEVICE_NAME ": block not locked");    \
        }

#endif

#endif
