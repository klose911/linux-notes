/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h> 
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

//内存中全局“i节点表”(NR_INODE = 32) 
struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

/*
 * 等待指定的i节点可用
 *
 * inode: 内存中i节点指针
 *
 * 无返回值
 *
 * 注意：在休眠前要手动禁止／打开中断！！！
 *
 */
static inline void wait_on_inode(struct m_inode * inode)
{
        cli();
        // 如果i节点已经被锁定，则将当前进程设置为“不可中断的等待状态”，并添加到该节点的i_wait队列上
        // 休眠状态将维持到i节点被解锁，并明确地唤醒本进程（调用wake_up函数）
        while (inode->i_lock)
                sleep_on(&inode->i_wait);
        sti();
}

/*
 * 对某个i节点上锁
 *
 * inode: 内存中i节点指针
 *
 * 无返回值
 *
 */
static inline void lock_inode(struct m_inode * inode)
{
        cli();
        while (inode->i_lock)
                sleep_on(&inode->i_wait);
        inode->i_lock=1; // 设置锁定标志
        sti();
}

/*
 * 对指定的i节点解锁
 *
 * inode: 内存中i节点指针
 *
 * 无返回值
 *
 * 注意: 此时不关心中断是否响应
 *
 */
static inline void unlock_inode(struct m_inode * inode)
{
        inode->i_lock=0; // 清空i节点的锁定标志
        wake_up(&inode->i_wait); //明确唤醒等待在该i节点的i_wait队列上的所有进程
}

/**
 * 释放指定设备dev在内存i节点表中所有的i节点
 *
 * dev: 设备号
 *
 * 无返回值
 *
 * 这里的释放指的是清空内存i节点表中的对应设备的i节点
 *
 */
void invalidate_inodes(int dev)
{
        int i;
        struct m_inode * inode;

        inode = inode_table;
        // 遍历整个内存节点表
        for(i=0 ; i<NR_INODE ; i++,inode++) {
                wait_on_inode(inode); // 等待i节点解锁
                if (inode->i_dev == dev) { // 校验i节点的设备号是不是特定设备
                        if (inode->i_count) // 如果i节点还被其他进程引用,则显示出错警告
                                printk("inode in use on removed disk\n\r");
                        inode->i_dev = inode->i_dirt = 0; // i节点的设备号,修改标志皆设为 0 
                }
        }
}

/**
 * 同步内存中所有修改过的i节点
 *
 * 无参数
 *
 * 无返回值
 *
 * 注意:实际上是写回高速缓存区，而buffer.c会在适当时机把数据写回设备
 *
 */
void sync_inodes(void)
{
        int i;
        struct m_inode * inode;

        inode = inode_table;
        // 遍历内存中的i节点表
        for(i=0 ; i<NR_INODE ; i++,inode++) {
                wait_on_inode(inode); // 等待i节点解锁
                if (inode->i_dirt && !inode->i_pipe) // "i节点已经被修改"而且"i节点不是管道节点"
                        write_inode(inode); // i节点写入高速缓冲区
        }
}

/*
 * 文件数据块映射到盘块(block map)
 *
 * inode: 文件的i节点指针
 * block: 文件的数据块号
 * create: 创建标志(0：不置位，1:置位)，如果该标志置位，则在设备上如果对应的逻辑块不存在，则申请新的磁盘块
 *
 * 返回：在设备上对应的盘块号（逻辑块号）
 * 
 */
static int _bmap(struct m_inode * inode,int block,int create)
{
        struct buffer_head * bh;
        int i;

        if (block<0) // 校验数据块号的有效性
                panic("_bmap: block<0"); // 小于0, 停机
        if (block >= 7+512+512*512) // 超出文件系统表示范围，停机
                panic("_bmap: block>big");
        // 直接使用直接块
        if (block<7) {
                // 需要创建设备上的盘块 && i节点中对应逻辑块（区段）字段为0
                if (create && !inode->i_zone[block]) {
                        if ((inode->i_zone[block]=new_block(inode->i_dev))) {
                                inode->i_ctime=CURRENT_TIME; // 设置“i节点修改时间”
                                inode->i_dirt=1; // 设置i节点的修改标志为“真”
                        }
                }
                return inode->i_zone[block]; // 返回已经申请的逻辑块号
        }
        // 6 < block < 7+512：说明使用的是一次间接块号 
        block -= 7;
        if (block<512) { 
                if (create && !inode->i_zone[7]) {
                        // 如果创建标志置位，则申请一块新的磁盘块来存放一次间接块
                        if ((inode->i_zone[7]=new_block(inode->i_dev))) {
                                inode->i_dirt=1;
                                inode->i_ctime=CURRENT_TIME;
                        }
                }                
                if (!inode->i_zone[7]) // 存放一次间接块的申请失败 或者 创建标志不置位但是i_zone[7]为0
                        return 0; // 返回0：表示失败
                if (!(bh = bread(inode->i_dev,inode->i_zone[7]))) //尝试从磁盘读入”一次间接块“对应的”逻辑块“到”高速缓冲区“
                        return 0; // 返回0：表示一次间接块的读取失败
                i = ((unsigned short *) (bh->b_data))[block]; // 文件数据块在一次间接块（高速缓存映射）中对应的数据
                if (create && !i) // 创建标志置位，并且原来位置上数据为空
                        // 创建一块新的逻辑块
                        if ((i=new_block(inode->i_dev))) {
                                ((unsigned short *) (bh->b_data))[block]=i;
                                bh->b_dirt=1; // 一次间接块的修改标志置位
                        }
                brelse(bh); // 释放一次间接块对应的缓冲区
                return i;
        }
        // 处理二次间接块
        block -= 512;
        if (create && !inode->i_zone[8])
                // 设置i节点中的字段值 inode->i_zone[8]：二次间接块中的一级块
                if ((inode->i_zone[8]=new_block(inode->i_dev))) {
                        inode->i_dirt=1; //设置i节点的值为已经修改
                        inode->i_ctime=CURRENT_TIME;
                }
        if (!inode->i_zone[8])
                return 0;
        // 读取二次间接块对应的一级块到高速缓存区
        if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
                return 0;
        i = ((unsigned short *)bh->b_data)[block>>9]; // 二次间接块的”二级块“在”一级块“上对应位置：block/512
        if (create && !i)
                // 申请二次间接块对应的二级块
                if ((i=new_block(inode->i_dev))) {
                        ((unsigned short *) (bh->b_data))[block>>9]=i;
                        bh->b_dirt=1; // 设置二次间接块的一级块已经被修改
                }
        brelse(bh); // 释放二次间接块的一级块对应的高速缓存区
        if (!i)
                return 0;
        // 读取二次间接块对应的二级块到高速缓存区
        if (!(bh=bread(inode->i_dev,i)))
                return 0;
        i = ((unsigned short *)bh->b_data)[block&511]; // 计算数据块在二次间接块的二级块中对应的位置：(block&511) 
        if (create && !i)
                // 在设备上创建一块新的盘块号给数据块
                if ((i=new_block(inode->i_dev))) {
                        ((unsigned short *) (bh->b_data))[block&511]=i;
                        bh->b_dirt=1; // 二次间接块的二级块对应的i节点设置修改标志
                }
        brelse(bh); // 高速缓存区中释放二次间接块中的二级块
        return i;
}

/**
 * 取i节点在设备上对应的逻辑块号
 *
 * inode: i节点指针
 * block: 文件数据块号
 *
 * 成功：对应设备上的逻辑块号， 失败：0
 * 
 */
int bmap(struct m_inode * inode,int block)
{
        return _bmap(inode,block,0);
}

/**
 * 取i节点在设备上对应的逻辑块号，如果设备上对应的逻辑块不存在，则创建一块！
 *
 * inode: i节点指针
 * block: 文件数据块号
 *
 * 成功：对应设备上的逻辑块号， 失败：0
 * 
 */
int create_block(struct m_inode * inode, int block)
{
        return _bmap(inode,block,1);
}

/**
 * 放置一个“i节点”（回写入设备）
 * 
 * inode: i节点指针
 *
 * 无返回值
 *
 * 该函数用于把i节点的引用计数减1：
 * 如果是管道i节点，则唤醒等待的进程，如果是块设备文件，则刷新设备
 * 如果该节点的链接数为0，则释放该节点占用的所有磁盘逻辑块，并释放该i节点
 * 
 */
void iput(struct m_inode * inode)
{
        if (!inode) // i节点为NULL，直接返回
                return;
        wait_on_inode(inode); // 等待i节点解锁
        if (!inode->i_count) // 如果该节点的引用计数为0，则表明该节点已经是空闲的 
                panic("iput: trying to free free inode"); // 内核报错，退出

        // 处理“管道文件”节点
        if (inode->i_pipe) {
                wake_up(&inode->i_wait); // 唤醒等待该节点的进程
                if (--inode->i_count) // 如果还有引用，则直接返回
                        return;
                // 释放i节点对应的内存页面
                free_page(inode->i_size); // 对于管道节点， i_size 保存了对应的内存地址 
                inode->i_count=0; // 引用计数为0
                inode->i_dirt=0; // 复位修改标志 
                inode->i_pipe=0; // 复位管道标志
                return;
        }

        // 如果i节点对应的设备号为0：如管道操作的i节点
        if (!inode->i_dev) {
                inode->i_count--; // 引用计数减1，退出
                return;
        }
        // 处理块设备文件对应的i节点（注意：不是普通文件，目录，类似于 /dev/fd 这种）
        if (S_ISBLK(inode->i_mode)) {
                // 刷新该设备
                sync_dev(inode->i_zone[0]); // 此时i_zone[0]中保存的是设备号
                wait_on_inode(inode); // 等待该节点解锁
        }
        // 处理普通文件，目录等
repeat:
        if (inode->i_count>1) { // 如果该节点的引用次数大于1
                inode->i_count--; // 引用次数递减1
                return; // 因为还有其他进程在引用该节点，所以不能释放，直接退出
        }
        // 该节点的引用次数为1（前面已经判断过引用次数是否为0！）
        if (!inode->i_nlinks) { // 如果该节点的链接次数等于0，说明对应的文件已经被删除
                truncate(inode); // 释放该节点所对应的所有逻辑块
                free_inode(inode); // 释放该节点
                return;
        }
        // 该节点的“修改标志”为“真”
        if (inode->i_dirt) {
                // 回写该节点
                write_inode(inode);	/* we can sleep - so do again */
                wait_on_inode(inode); // 睡眠等待回写完成
                goto repeat; // 睡眠过程中，其他的进程可能还会修改该节点，所以重复进行上述判断
        }
        inode->i_count--; // i节点的引用计数减1
        return;
}

/**
 * 从i节点表(inode_table)中获得一个空闲的i节点项
 *
 * 无参数
 *
 * 返回：一个空闲的i节点项
 */
struct m_inode * get_empty_inode(void)
{
        struct m_inode * inode;
        static struct m_inode * last_inode = inode_table;
        int i;

        do {
                inode = NULL;
                // 从最后一项开始，遍历i节点表
                for (i = NR_INODE; i ; i--) {
                        if (++last_inode >= inode_table + NR_INODE) // 如果 last_node 已经指向“i节点表”最后一项之后
                                last_inode = inode_table; // last_node 重新指向“i节点表”
                        if (!last_inode->i_count) { // 如果 last_node 的引用计数为0：说明已经找到一个空闲的i节点
                                inode = last_inode; // inode 指向该项
                                if (!inode->i_dirt && !inode->i_lock) // inode 的修改标志和锁定标志皆没有置位
                                        break; // 已经找到需要的空节点，退出循环
                        }
                }
                // 无法找到一个空的i节点，则打印所有的i节点，并停机
                if (!inode) {
                        for (i=0 ; i<NR_INODE ; i++)
                                printk("%04x: %6d\t",inode_table[i].i_dev,
                                       inode_table[i].i_num);
                        panic("No free inodes in mem");
                }
                wait_on_inode(inode); // 等待该节点解锁（如果又被上锁的话）
                while (inode->i_dirt) { 
                        write_inode(inode); // 如果修改标志置位，则把节点回写到高速缓冲区中
                        wait_on_inode(inode); // 再次等待该节点解锁，因为休眠过程中仍可能再次被修改，所以还需重新循环校验“修改标志”
                }
        } while (inode->i_count); // 再次校验该节点是否空闲， 如果又被其他进程占用，则再次开始循环寻找一个空闲的节点
        // 总算找到一个真正空闲的i节点：引用次数为0, 没有修改，没有上锁
        memset(inode,0,sizeof(*inode)); // 重新设置i节点中的数据
        inode->i_count = 1; // i节点引用计数为1
        return inode; 
}

/**
 * 获得一个管道节点
 *
 * 无参数
 *
 * 成功则返回申请到的管道文件的i节点指针，失败返回 NULL
 * 
 */
struct m_inode * get_pipe_inode(void)
{
        struct m_inode * inode;

        // 尝试从内存的i节点表申请一个空闲i节点
        if (!(inode = get_empty_inode()))
                return NULL; // 申请失败，则返回NULL
        // 尝试为这个i节点分配一页内存页，分配到的内存页物理地址放入到i节点的i_size域下
        if (!(inode->i_size=get_free_page())) {
                // 分配失败，则设置i节点的引用计数为0，返回NULL
                inode->i_count = 0;
                return NULL; 
        }
        // i节点的引用计数设为2：读进程和写进程
        inode->i_count = 2;	/* sum of readers/writers */ 
        PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0; // 管道头指针(i_zone[0])和管道尾指针(i_zone[1])复位
        inode->i_pipe = 1; // 设置管道标志
        return inode;
}

/**
 * 获取一个i节点
 *
 * dev: 设备号
 * nr: i节点号
 *
 * 成功返回对应的i节点指针，失败返回
 * 
 */
struct m_inode * iget(int dev,int nr)
{
        struct m_inode * inode, * empty;
        
        if (!dev) // 设备号为0：内核报错，退出
                panic("iget with dev==0");
        empty = get_empty_inode(); // 获得一个空闲的i节点指针
        inode = inode_table; // inode 指向内核i节点表
        // 遍历“内核i节点表”
        while (inode < NR_INODE+inode_table) {
                // “内核i节点表”中的i节点与“要获得的i节点”不匹配
                if (inode->i_dev != dev || inode->i_num != nr) {
                        inode++;
                        continue; // 遍历下一个
                }
                wait_on_inode(inode); // 等待i节点解锁
                // 再次校验是否匹配
                if (inode->i_dev != dev || inode->i_num != nr) {
                        inode = inode_table; // 重新指向内核i节点表开头
                        continue; // 从新开始遍历
                }
                // 到这里表示找到相应的i节点
                inode->i_count++; // i节点的引用计数加1
                // 检查i节点是否是另一个文件系统的挂载点
                if (inode->i_mount) {
                        // 当前的i节点是另一个文件系统的挂载点
                        int i;

                        // 遍历超级块数组，寻找”被安装文件系统“的”根节点“
                        for (i = 0 ; i<NR_SUPER ; i++)
                                if (super_block[i].s_imount==inode) // s_imount ： 挂载点目录在原文件系统中的“i节点” （类似于/usr 在 / 这个文件系统中的i节点）
                                        break;
                        // 超级块中无法找到对应的i节点
                        if (i >= NR_SUPER) {
                                printk("Mounted inode hasn't got sb\n"); // 显示出错信息
                                if (empty)
                                        iput(empty); // 放回申请的空闲节点
                                return inode; // 返回该i节点指针
                        }
                        // 执行到这里：已经找到安装到当前i节点的文件系统的超级块
                        iput(inode); // 将该i节点写盘放回
                        dev = super_block[i].s_dev; // 设备号为超级块中对应的设备号
                        nr = ROOT_INO; // i节点号为文件系统的根节点号(1)
                        inode = inode_table; // inode 重新指向内存节点表，再次循环查找对应的i节点
                        continue;
                }
                // 执行到这里：表示已经在内存i节点表中找到对应的i节点
                if (empty) 
                        iput(empty); // 重新放回临时申请的i节点
                return inode; // 返回已经寻找到的i节点
        }
        // 执行到这里：在内存i节点表中无法找到对应的i节点
        if (!empty) // 无法申请一个空闲i节点作为临时i节点
                return (NULL);
        inode=empty; // inode 指向申请到的临时i节点
        inode->i_dev = dev; // 设备号 = dev 
        inode->i_num = nr; // i节点号 = nr 
        read_inode(inode); // 从设备中读取该i节点信息到高速缓存区中
        return inode;
}

/*
 * 读取指定i节点信息
 *
 * inode: i节点指针
 *
 * 无返回值
 * 
 */
static void read_inode(struct m_inode * inode)
{
        struct super_block * sb;
        struct buffer_head * bh;
        int block;

        lock_inode(inode); // 锁定i节点
        // 读取i节点对应设备的超级块信息
        if (!(sb=get_super(inode->i_dev))) // 读取超级块信息失败，内核报错，退出
                panic("trying to read inode without dev");
        block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
                (inode->i_num-1)/INODES_PER_BLOCK; // 计算设备上存储该i节点对应的逻辑块号
        // 读取设备上存储该i节点的逻辑块到高速缓冲区
        if (!(bh=bread(inode->i_dev,block))) // 读取设备逻辑块失败，内核报错，退出
                panic("unable to read i-node block");
        // 读取缓冲区中该i节点信息到 inode 指针指向的地址处
        *(struct d_inode *)inode =
                ((struct d_inode *)bh->b_data)
                [(inode->i_num-1)%INODES_PER_BLOCK];
        brelse(bh); // 释放缓冲块
        unlock_inode(inode); // 解锁i节点 
}

/*
 * 将i节点信息写入缓冲区
 *
 * inode: i节点指针
 *
 * 无返回值
 * 
 */
static void write_inode(struct m_inode * inode)
{
        struct super_block * sb;
        struct buffer_head * bh;
        int block;

        lock_inode(inode); // 锁定该节点
        // 如果该节点没有修改，或者该节点不属于任何设备
        if (!inode->i_dirt || !inode->i_dev) {
                unlock_inode(inode); // 解锁该节点，直接退出
                return;
        }
        if (!(sb=get_super(inode->i_dev))) // 获得该节点对应的超级块
                panic("trying to write inode without device");
        // 该节点所在的设备的逻辑块号 = 2 (启动块 + 超级块) + i节点位图占用块数 + 逻辑块位图占用的块数 + (i节点号 - 1) / 每块含有的i节点数 
        block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
                (inode->i_num-1)/INODES_PER_BLOCK; // 计算该节点所在设备的逻辑块号
        // 读取“该节点对应的逻辑快”到“高速缓冲区”中
        if (!(bh=bread(inode->i_dev,block))) // 无法读取对应的逻辑块到高速缓冲区 
                panic("unable to read i-node block");
        // 将该节点信息复制到该逻辑块对应的该i节点的项位置处
        ((struct d_inode *)bh->b_data)
                [(inode->i_num-1)%INODES_PER_BLOCK] =
                *(struct d_inode *)inode;
        bh->b_dirt=1; // “高速缓冲区”的“修改标志”置位
        inode->i_dirt=0; // “i节点”的“修改标志”复位
        brelse(bh); // 释放高速缓冲区对应的缓冲块
        unlock_inode(inode); // i节点解锁
}
