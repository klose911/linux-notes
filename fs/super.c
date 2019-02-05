/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
/**
 * 测试addr地址处偏移bitnr位的位值
 *
 * bitnr: 位偏移值
 * addr: 内存物理地址
 *
 */
// 1. 定义了一个寄存器变量，保存在eax中
// 2. bt 指令：对偏移位进行测试
// 3. setb 指令：根据iflags寄存器中的进位标志CF设置al寄存器，如果CF=1，则%al=1，否则%al=0
// %0: eax(__res), %1: 常量0, %2: bitnr, %3 addr 
#define set_bit(bitnr,addr) ({                                          \
                        register int __res ;                            \
                        __asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
                        __res; })


struct super_block super_block[NR_SUPER]; // 超级块数组(NR_SUPER=8)
/* this is initialized in init/main.c */
int ROOT_DEV = 0; // 根文件系统设备号

// 以下三个函数和 inode.c 中类似，只是操作对象从 i节点变成了超级块

/*
 * 锁定超级块
 *
 * sb: 超级块指针
 *
 * 无返回值
 * 
 */
static void lock_super(struct super_block *sb)
{
        cli(); // 关闭中断
        while (sb->s_lock) // 如果超级块已经上锁
                sleep_on(&(sb->s_wait)); // 将当前任务置为不可中断的等待状态，并添加到该超级块的等待队列 s_wait
        sb->s_lock = 1; // 给该超级块加锁（置位锁定标志）
        sti(); // 打开中断
}

/*
 * 解锁超级块
 *
 * sb: 超级块指针
 *
 * 无返回值
 * 
 */
static void free_super(struct super_block * sb)
{
        cli(); // 关中断
        sb->s_lock = 0; // 释放锁（复位锁定标志）
        wake_up(&(sb->s_wait)); // 把当前进程从等待队列移除
        sti(); // 开中断
}

/*
 * 睡眠等待超级块解锁
 *
 * sb: 超级块指针
 *
 * 无返回值
 * 
 */
static void wait_on_super(struct super_block * sb)
{
        cli(); // 关中断
        while (sb->s_lock) // 超级块已经被锁定
                sleep_on(&(sb->s_wait)); // 将当前任务置为不可中断的等待状态，并添加到该超级块的等待队列 s_wait
        sti(); // 开中断
}

/**
 * 读取指定设备的超级块
 *
 * dev: 设备号
 *
 * 成功：返回超级块结构的指针，失败：返回 NULL
 * 
 */
struct super_block * get_super(int dev)
{
        struct super_block * s;

        if (!dev) // 设备号为0，直接返回 NULL 
                return NULL;
        s = super_block; // s 指向 super_block 数组
        // 遍历超级块数组
        while (s < NR_SUPER+super_block)
                if (s->s_dev == dev) { // 找到对应设备号的超级块结构
                        wait_on_super(s); // 等待该超级块解锁（如果该超级块已经被加锁）
                        if (s->s_dev == dev) // 因为在等待期间，该超级块项有可能被其他进程使用和修改，所以需要再次判断一次
                                return s;
                        // 等待结束后，超级块项已经被其他进程修改，再次从头开始遍历超级块数组
                        s = super_block;
                } else
                        s++;
        return NULL; // 找不到，返回NULL 
}

/**
 * 释放（放回）指定设备的超级块
 *
 * dev: 设备号
 *
 * 无返回值
 *
 * 注意：这里仅仅重置了超级块结构的设备域，其他会在相关操作后被重置！！！
 * 
 */
void put_super(int dev)
{
        struct super_block * sb;
        /* struct m_inode * inode;*/
        int i;
        
        if (dev == ROOT_DEV) { // 根节点设备的超级块无法释放，打印出错信息，退出函数
                printk("root diskette changed: prepare for armageddon\n\r");
                return;
        }
        if (!(sb = get_super(dev))) // 无法读取该设备的超级块信息，直接退出
                return;
        //在卸载文件系统时会先把s_imount设置成NULL
        if (sb->s_imount) { // 如果该超级块指向的该文件系统所安装到的i节点还没有被处理过，显示警告信息，退出函数
                printk("Mounted disk changed - tssk, tssk\n\r");
                return;
        }
        lock_super(sb); // 对超级块加锁
        sb->s_dev = 0; // 超级块的设备号设置为0

        // 释放设备在内核中占用的资源
        // 注意：如果对应的缓冲块在内核中被修改，需要同步操作才能把数据写回到设备中去
        for(i=0;i<I_MAP_SLOTS;i++) 
                brelse(sb->s_imap[i]); // 释放该设备i节点位图在内存中占用的高速缓冲块
        for(i=0;i<Z_MAP_SLOTS;i++) 
                brelse(sb->s_zmap[i]); // 释放该设备逻辑位图在内存中占用的高速缓冲块
        
        free_super(sb); // 对超级块解锁
        return;
}

/*
 * 读取指定设备的超级块
 *
 * dev: 设备号
 *
 * 成功：返回超级块结构指针，失败：返回NULL
 *
 * 如果指定设备的超级块已经在超级块数组中，则返回相应超级块项的指针
 * 反之，从设备dev上读取超级块到高速缓冲区，并复制到超级块数组，返回超级块指针
 * 
 */
static struct super_block * read_super(int dev)
{
        struct super_block * s;
        struct buffer_head * bh;
        int i,block;

        if (!dev) // 如果设备号为0，直接返回NULL 
                return NULL;
        // 检查该设备是否更换过盘片（针对软盘），如果更换过，则高速缓冲区有关该设备的所有缓冲块均失效（释放原来加载的文件系统）
        check_disk_change(dev);
        
        if ((s = get_super(dev))) // 从超级块数组寻找对应设备的超级块项
                return s; // 如果寻找到，则直接返回该超级块项指针
        // 遍历超级块数组，寻找一个 s_dev 为空的超级块项（已经释放，空的）
        for (s = super_block ;; s++) {
                if (s >= NR_SUPER+super_block) // 所有超级块项皆被占用，返回NULL 
                        return NULL;
                if (!s->s_dev)
                        break;
        }
        // 初始换超级块
        s->s_dev = dev;
        s->s_isup = NULL;
        s->s_imount = NULL;
        s->s_time = 0;
        s->s_rd_only = 0;
        s->s_dirt = 0;
        lock_super(s); // 锁定该超级块
        // 从设备的上读取超级块信息到 bh 指向的缓冲块中
        //超级块位于块设备的第2个逻辑块，也就是第1号块中（第一个逻辑块是引导块）
        if (!(bh = bread(dev,1))) { // 读取超级块到高速缓冲中失败
                s->s_dev=0; // 重置设备号
                free_super(s); // 释放该超级块
                return NULL; // 返回 NULL 
        }
        // 将设备上读取的超级块信息从高速缓冲区复制到超级块数组相应项的结构中
        *((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);
        brelse(bh); // 释放该超级块在高速缓冲区中的缓冲块

        // 检查这个超级块信息的有效性（文件系统魔数：minix1.0文件系统中是 0x137f）
        if (s->s_magic != SUPER_MAGIC) { // 超级块校验魔数失败
                s->s_dev = 0; // 重置设备号
                free_super(s); // 释放超级块
                return NULL; // 返回 NULL 
        }

        // 初始化设备的“i节点位图”和“逻辑块位图”内存空间
        for (i=0;i<I_MAP_SLOTS;i++)
                s->s_imap[i] = NULL;
        for (i=0;i<Z_MAP_SLOTS;i++)
                s->s_zmap[i] = NULL;
        
        block=2;
        // 从设备上读取“i节点位图”信息（从第2个逻辑块开始，总共s_imap_blocks块）
        for (i=0 ; i < s->s_imap_blocks ; i++)
                if ((s->s_imap[i]=bread(dev,block)))
                        block++;
                else
                        break;
        // 从设备上读取“逻辑块位图”信息（紧跟着“i节点位图”，总共s_zmap_blocks块）
        for (i=0 ; i < s->s_zmap_blocks ; i++)
                if ((s->s_zmap[i]=bread(dev,block)))
                        block++;
                else
                        break;
        // 如果读出的块数 != 2 + i节点位图应占用的块数 + 逻辑块位图应占用的块数，则说明文件系统有问题
        if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
                for(i=0;i<I_MAP_SLOTS;i++)
                        brelse(s->s_imap[i]); // 释放所有已经读到的“i节点位图”占用的高速缓冲块
                for(i=0;i<Z_MAP_SLOTS;i++)
                        brelse(s->s_zmap[i]); // 释放所有已经读到的“逻辑块位图”占用的高速缓冲块
                s->s_dev=0; // 重置设备号
                free_super(s); // 解锁超级块
                return NULL; // 返回 NULL 
        }

        // 按照约定：“i节点位图”和“逻辑块位图”中的最低位总是设为1（对应的i节点和逻辑块是无法被使用的，根目录的i节点/引导块）
        s->s_imap[0]->b_data[0] |= 1;
        s->s_zmap[0]->b_data[0] |= 1;
        free_super(s); // 解锁超级块
        return s; // 返回超级块指针
}

int sys_umount(char * dev_name)
{
        struct m_inode * inode;
        struct super_block * sb;
        int dev;

        if (!(inode=namei(dev_name)))
                return -ENOENT;
        dev = inode->i_zone[0];
        if (!S_ISBLK(inode->i_mode)) {
                iput(inode);
                return -ENOTBLK;
        }
        iput(inode);
        if (dev==ROOT_DEV)
                return -EBUSY;
        if (!(sb=get_super(dev)) || !(sb->s_imount))
                return -ENOENT;
        if (!sb->s_imount->i_mount)
                printk("Mounted inode has i_mount=0\n");
        for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
                if (inode->i_dev==dev && inode->i_count)
                        return -EBUSY;
        sb->s_imount->i_mount=0;
        iput(sb->s_imount);
        sb->s_imount = NULL;
        iput(sb->s_isup);
        sb->s_isup = NULL;
        put_super(dev);
        sync_dev(dev);
        return 0;
}

/**
 * 加载文件系统（系统调用）
 *
 * dev_name: 要加载的设备文件名
 * dir_name: 安装到的目录名，并且对应的i节点没有被其他进程锁占用
 * rw_flag: 被安装系统的可读写标志
 *
 * 成功：返回0，失败：返回错误号
 * 
 */
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
        struct m_inode * dev_i, * dir_i;
        struct super_block * sb;
        int dev;

        // 根据设备文件名找到对应的i节点（为了获取“设备号”：对于块设备文件，设备号在其i节点的i_zone[0]中）
        if (!(dev_i=namei(dev_name))) // 无法取得设备对应的i节点
                return -ENOENT; // 返回错误号：-ENONET 
        dev = dev_i->i_zone[0]; // 获取设备号
        if (!S_ISBLK(dev_i->i_mode)) { // 如果对应的设备号不是块设备
                iput(dev_i); // 回写设备文件对应的i节点
                return -EPERM; // 返回错误号：-EPERM
        }
        iput(dev_i); // 回写设备文件对应的i节点
        if (!(dir_i=namei(dir_name))) // 获取安装目录对应的i节点
                return -ENOENT;
        // 如果该i节点的引用计数不为1（还有别的进程也在引用） 或者 i节点号 == 根节点的i节点号
        if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
                iput(dir_i); // 放回安装目录对应的i节点
                return -EBUSY; // 返回错误号： -EBUSY 
        }
        // 如果该i节点对应的不是目录
        if (!S_ISDIR(dir_i->i_mode)) {
                iput(dir_i); // 放回安装目录对应的i节点
                return -EPERM; // 返回错误号：-EPERM
        }
        // 读取设备对应的超级块
        if (!(sb=read_super(dev))) { // 读取超级块失败
                iput(dir_i); // 放回安装目录对应的i节点
                return -EBUSY; // 返回错误号： -EBUSY
        }
        // 如果将要安装的文件系统已经挂载到其他目录(s_imount > 0) 
        if (sb->s_imount) {
                iput(dir_i); // 放回安装目录对应的i节点
                return -EBUSY; // 返回错误号： -EBUSY
        }
        // 如果挂载目录已经安装了其他文件系统(i_mount > 0) 
        if (dir_i->i_mount) { 
                iput(dir_i); // 放回安装目录对应的i节点
                return -EPERM; // 返回错误号：-EPERM 
        }
        sb->s_imount=dir_i; // 设置超级块的“s_imount域”为“挂载点目录”对应的“i节点号”
        dir_i->i_mount=1; // 挂载点目录的i_mount域设置为1（文件系统的根节点为1）
        dir_i->i_dirt=1; // 挂载点目录的修改标志为真 /* NOTE! we don't iput(dir_i) */
        // 注意：这里并没有放回挂载点目录的i节点，将在 unmount 的时候放回
        return 0;			/* we do that in umount */
}

/**
 * 挂载根文件系统
 *
 * 无参数
 *
 * 无返回值
 *
 * 系统开机进行初始化设置时(sys_setup)时调用：
 *   1. 初始化文件表数组 file_table[] 和超级块数组
 *   2. 读取根文件系统的超级块，取得文件系统的根i节点
 *   3. 统计显示根文件系统上的可用资源（空闲块数和空闲i节点数）
 * 
 */
void mount_root(void)
{
        int i,free;
        struct super_block * p;
        struct m_inode * mi;

        if (32 != sizeof (struct d_inode)) // 校验i节点结构大小
                panic("bad i-node size"); // 打印错误信息，死机
        // 初始化内核的文件表数组，总共64项
        for(i=0;i<NR_FILE;i++)
                file_table[i].f_count=0; // 文件结构的引用计数设置为0（表示空闲）
        if (MAJOR(ROOT_DEV) == 2) { // 如果根文件系统所在的设备是软盘
                printk("Insert root floppy and press ENTER"); // 提示超入软盘，并按回车
                wait_for_keypress(); // 等待回车
        }
        // 初始化超级块数组
        // 注意：循环结束时，超级块指针p指向超级块数组的最后一项
        for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
                // 所有的超级块项的结构字段皆初始化为0（表示空闲）
                p->s_dev = 0;
                p->s_lock = 0;
                p->s_wait = NULL;
        }
        
        // 读取根文件系统对应设备的超级块
        if (!(p=read_super(ROOT_DEV))) // 读取超级块出错
                panic("Unable to mount root"); // 打印错误信息，死机
        // 读取根文件系统设备的根i节点（1号i节点）
        if (!(mi=iget(ROOT_DEV,ROOT_INO))) // 读取i节点失败
                panic("Unable to read root i-node"); // 打印错误信息，死机

        // 根 i节点的引用次数 + 3 : 下面三行各自引用了1次
        mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
        // 根文件目录对应的超级块的“被挂载的文件系统i节点”(s_isup)和“挂载点的i节点”都设置为“mi对应的i节点”
        p->s_isup = p->s_imount = mi;
        // 注意：此时当前进程是1号进程(init进程) 
        current->pwd = mi; // 当前进程的工作目录为“mi对应的i节点” 
        current->root = mi; // 当前进程的根目录为“mi对应的i节点”

        // 对根文件系统上的资源进行统计工作
        free=0;
        // 根据“逻辑块位图”打印空闲的逻辑块数量
        i=p->s_nzones;
        while (-- i >= 0)
                if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
                        free++;
        printk("%d/%d free blocks\n\r",free,p->s_nzones);

        free=0;
        // 根据“i节点位图”打印空闲的i节点数量
        i=p->s_ninodes+1;
        while (-- i >= 0)
                if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
                        free++;
        printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
