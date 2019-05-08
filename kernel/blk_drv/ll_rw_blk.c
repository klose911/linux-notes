/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h> // 错误号头文件
#include <linux/sched.h> // 进程调度头文件
#include <linux/kernel.h> // 内核配置头文件
#include <asm/system.h> // 定义了设置或修改“描述符”/“中断门”等的嵌入式汇编语句

#include "blk.h" // 块设备头文件

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */

/*
 * “块设备请求项”包含所有把nr个扇区加载到内存中的信息
 */
struct request request[NR_REQUEST]; // “块设备请求项”数组，总共有NR_REQUEST=32个请求项

/*
 * used to wait on when there are no free requests
 */

/*
 * 用来保存等待空闲请求项的进程队列
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */

/*
 * blk_dev_struct 结构：块设备结构
 *   do_request-address : 对应主设备号的请求处理函数指针
 *	 next-request : 该设备的下一个请求项
 */
// 块设备数组： 该数组使用主设备号作为下标，实际内容将在各块设备驱动程序初始化时填入
// 例如：硬盘设备驱动程序初始化时（hd.c中的hd_init函数），用于设置blk_dev[3]的内容
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
        { NULL, NULL },		/* no_dev */   // 0 - 无设备
        { NULL, NULL },		/* dev mem */  // 1 - 内存
        { NULL, NULL },		/* dev fd */   // 2 - 软驱 
        { NULL, NULL },		/* dev hd */   // 3 - 硬盘
        { NULL, NULL },		/* dev ttyx */ // 4 - ttyx设备
        { NULL, NULL },		/* dev tty */  // 5 - tty设备
        { NULL, NULL }		/* dev lp */   // 6 - 打印机
};

/*
 * 锁定指定的高速缓冲块
 *
 * bh: 高速缓冲块头指针
 *
 * 无返回
 * 
 * 如果指定的缓冲块已经被其他进程使用，则使当前进程睡眠（不可中断地等待），直到被执行解锁缓冲块的进程明确地唤醒
 * 
 */
static inline void lock_buffer(struct buffer_head * bh)
{
        cli(); // 关闭中断
        while (bh->b_lock) // 缓冲块已被锁定
                sleep_on(&bh->b_wait); //当前进程进入睡眠，直到缓冲区被解锁（加入 bh->b_wait这个等待队列） 
        bh->b_lock=1; // 立刻锁定缓冲块
        sti(); // 打开中断
}

/*
 * 解锁（释放）高速缓冲块
 *
 * bh: 高速缓冲块头指针
 *
 * 无返回
 * 
 */
static inline void unlock_buffer(struct buffer_head * bh)
{
        if (!bh->b_lock) // 缓冲块没有被锁定，打印出错信息
                printk("ll_rw_block.c: buffer not locked\n\r");
        bh->b_lock = 0; // 修改锁定标志
        wake_up(&bh->b_wait); // 明确唤醒等待该缓冲块的进程队列
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */

/*
 * add-request 向链表中加入一个请求项。它会关闭中断，这样就能安全地处理请求链表了
 * 
 */

/*
 * 往“块设备项”的”请求链表“中加入一个“请求项”
 *
 * dev: 块设备项
 * req: 请求项
 *
 * 无返回
 *
 * 如果dev中的current_request指针为空，则可以设置current_request指针为req，并且立刻调用对应的设备请求项处理函数指针
 * 否则使用“电梯算法”把req插入到current_request对应的链表中
 * 
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
        struct request * tmp;

        req->next = NULL; // 置空请求项的下一项指针
        cli(); // 关闭中断
        if (req->bh)
                req->bh->b_dirt = 0; // 复位请求项中高速缓冲块的“脏”标志位
        // tmp被赋值为dev的current_request域
        if (!(tmp = dev->current_request)) { // dev中的current_request指针为空
                dev->current_request = req; // 设置current_request为req
                sti(); // 打开中断
                (dev->request_fn)(); // 立刻执行块设备请求函数（对应硬盘就是 do_hd_request）
                return;
        }
        // 遍历对应设备的请求链表数组，根据电梯算法把req项插入到合适的位置
        for ( ; tmp->next ; tmp=tmp->next)
                // （tmp的优先级比req高 或者 tmp的优化级不高于tmp->next） 并且 （req的优先级比tmp->next来的高）
                if ((IN_ORDER(tmp,req) || 
                     !IN_ORDER(tmp,tmp->next)) &&
                    IN_ORDER(req,tmp->next))
                        break; // 找到对应的项
        // 插入到tmp项的后面
        req->next=tmp->next; // req的下一项请求指针指向tmp的下一项 
        tmp->next=req; // tmp的下一项请求指针指向req
        sti();
}

/*
 * 创建请求项，并插入请求项队列中
 *
 * major: 主设备号
 * rw: 指定命令
 * bh: 存放数据的高速缓冲块指针
 *
 * 无返回
 * 
 */
static void make_request(int major,int rw, struct buffer_head * bh)
{
        struct request * req;
        int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */

        /*
         * WRITEA/READA 是一种特殊情况，这并非必须的
         * 如果高速缓冲块已经被锁，就不用管他，否则就当一般的读/写处理
         * 这里的A：表示advance，也就是预写/预读
         */
        if ((rw_ahead = (rw == READA || rw == WRITEA))) {
                if (bh->b_lock) // 如果高速缓冲块已经被锁，直接退出
                        return;
                if (rw == READA)
                        rw = READ;
                else
                        rw = WRITE;
        }
        if (rw!=READ && rw!=WRITE) // 指定命令无效，打印出错信息，死机
                panic("Bad block dev command, must be R/W/RA/WA");
        lock_buffer(bh); // 尝试锁定对应的高速缓冲块，如果无法锁定则睡眠（不可中断地等待）
        // 1. 写命令 并且 高速缓冲块“脏”标志没有置位
        // 2. 读命令 并且 高速缓冲块“内容同步”标志已经置位
        // 这两种情况都不需要任何设备操作
        if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
                unlock_buffer(bh); // 解锁高速缓冲块
                return; // 退出
        }
        // 接下来在请求项数组队列中找到一个空闲项
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
        /*
         * 不能让所有的队列都是写请求项，因为读请求项优先级远高于写
         * 所以为读请求项预留一些空间，最后1/3的部分只保留给读请求
         */
        if (rw == READ)
                req = request+NR_REQUEST; // 读请求项可以搜索整个队列
        else
                req = request+((NR_REQUEST*2)/3); // 写请求项只能从最开始到2/3队列之间搜索
/* find an empty request */
        while (--req >= request) // 从后往前遍历
                if (req->dev<0) // 对应项的dev域 < 0 : 表示这项是空闲可用的
                        break; // 找到空闲项，终止遍历
/* if none found, sleep on new requests: check for rw_ahead */
        if (req < request) { // 遍历完毕，依旧无法找到空闲项 
                if (rw_ahead) { // 如果是“预”读/写请求
                        unlock_buffer(bh); // 释放高速缓冲块，直接返回
                        return;
                }
                sleep_on(&wait_for_request); // 当前进程加入等待空闲项的等待队列
                goto repeat; // 被唤醒后重新开始搜索空闲请求项
        }
/* fill up the request-info, and add it to the queue */

        // 执行到这里表示已经在请求项队列中找到一项空闲的请求项
        // 初始化对应的空闲请求项
        req->dev = bh->b_dev; // 设备号
        req->cmd = rw; // 读写命令
        req->errors=0; // 操作错误数初始化为0
        req->sector = bh->b_blocknr<<1; // 起始扇区，块号转换成扇区号（1块对应1024字节，1扇区对应512字节，因此1块=2扇区）
        req->nr_sectors = 2; // 本请求项需要读写的扇区数 = 2 （1块）
        req->buffer = bh->b_data; // “请求项”的“缓冲区”指针指向“高速缓冲块”头指针中的“数据区”
        req->waiting = NULL; // 等待本次操作执行完成的进程队列初始化为空
        req->bh = bh; // 本次操作的高速缓冲块头指针
        req->next = NULL; // 下一项请求指针初始化为空
        add_request(major+blk_dev,req); // 将“请求项”插入到对应“块设备项”的“请求项链表“中
}

/**
 * 底层数据块读写接口(low level read/write block)
 * 该函数是块设备驱动程序与内核其他部分的接口函数，通常在fs/buffer.c中被调用
 * 主要功能：创建请求项，并插入对应的设备的请求项链表中
 *
 * rw: 读写命令，READ/WRITE/READA/WRITEA
 * bh: 高速缓冲块头指针
 *
 * 无返回
 *
 * 实际读写动作是调用对应设备的request_fn函数指针来完成：　
 * 对于硬盘设备：该函数是do_hd_request, 对于内存盘：该函数是do_rd_request, 对于软盘：该函数是do_fd_request
 *
 * 另外在调用本函数前，调用者必须把读写块设备相关的信息在高速缓冲块头指针中准备号，如设备号，块号
 * 
 */
void ll_rw_block(int rw, struct buffer_head * bh)
{
        unsigned int major;

        //  判断设备号和设备号中的请求处理函数指针是否有效
        // （主设备号 > NR_BLK_DEV） 或者　（对应设备的请求处理函数指针为空）　
        if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
            !(blk_dev[major].request_fn)) {
                printk("Trying to read nonexistent block-device\n\r"); // 打印出错信息，直接返回
                return;
        }
        make_request(major,rw,bh); // 创建对应的块设备请求项
}

/**
 * 块设备系统初始化: 由初始化程序'init/main.c'调用
 * 
 */
void blk_dev_init(void)
{
        int i;

        // 初始化“块设备请求项”数组的“请求项”，总共32项
        for (i=0 ; i<NR_REQUEST ; i++) {
                request[i].dev = -1; // -1 表示这个请求项空闲
                request[i].next = NULL; // NULL 表示没有下一个请求
        }
}
