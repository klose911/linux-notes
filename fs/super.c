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

static struct super_block * read_super(int dev)
{
        struct super_block * s;
        struct buffer_head * bh;
        int i,block;

        if (!dev)
                return NULL;
        check_disk_change(dev);
        if ((s = get_super(dev)))
                return s;
        for (s = 0+super_block ;; s++) {
                if (s >= NR_SUPER+super_block)
                        return NULL;
                if (!s->s_dev)
                        break;
        }
        s->s_dev = dev;
        s->s_isup = NULL;
        s->s_imount = NULL;
        s->s_time = 0;
        s->s_rd_only = 0;
        s->s_dirt = 0;
        lock_super(s);
        if (!(bh = bread(dev,1))) {
                s->s_dev=0;
                free_super(s);
                return NULL;
        }
        *((struct d_super_block *) s) =
                *((struct d_super_block *) bh->b_data);
        brelse(bh);
        if (s->s_magic != SUPER_MAGIC) {
                s->s_dev = 0;
                free_super(s);
                return NULL;
        }
        for (i=0;i<I_MAP_SLOTS;i++)
                s->s_imap[i] = NULL;
        for (i=0;i<Z_MAP_SLOTS;i++)
                s->s_zmap[i] = NULL;
        block=2;
        for (i=0 ; i < s->s_imap_blocks ; i++)
                if ((s->s_imap[i]=bread(dev,block)))
                        block++;
                else
                        break;
        for (i=0 ; i < s->s_zmap_blocks ; i++)
                if ((s->s_zmap[i]=bread(dev,block)))
                        block++;
                else
                        break;
        if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
                for(i=0;i<I_MAP_SLOTS;i++)
                        brelse(s->s_imap[i]);
                for(i=0;i<Z_MAP_SLOTS;i++)
                        brelse(s->s_zmap[i]);
                s->s_dev=0;
                free_super(s);
                return NULL;
        }
        s->s_imap[0]->b_data[0] |= 1;
        s->s_zmap[0]->b_data[0] |= 1;
        free_super(s);
        return s;
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

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
        struct m_inode * dev_i, * dir_i;
        struct super_block * sb;
        int dev;

        if (!(dev_i=namei(dev_name)))
                return -ENOENT;
        dev = dev_i->i_zone[0];
        if (!S_ISBLK(dev_i->i_mode)) {
                iput(dev_i);
                return -EPERM;
        }
        iput(dev_i);
        if (!(dir_i=namei(dir_name)))
                return -ENOENT;
        if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
                iput(dir_i);
                return -EBUSY;
        }
        if (!S_ISDIR(dir_i->i_mode)) {
                iput(dir_i);
                return -EPERM;
        }
        if (!(sb=read_super(dev))) {
                iput(dir_i);
                return -EBUSY;
        }
        if (sb->s_imount) {
                iput(dir_i);
                return -EBUSY;
        }
        if (dir_i->i_mount) {
                iput(dir_i);
                return -EPERM;
        }
        sb->s_imount=dir_i;
        dir_i->i_mount=1;
        dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
        return 0;			/* we do that in umount */
}

void mount_root(void)
{
        int i,free;
        struct super_block * p;
        struct m_inode * mi;

        if (32 != sizeof (struct d_inode))
                panic("bad i-node size");
        for(i=0;i<NR_FILE;i++)
                file_table[i].f_count=0;
        if (MAJOR(ROOT_DEV) == 2) {
                printk("Insert root floppy and press ENTER");
                wait_for_keypress();
        }
        for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
                p->s_dev = 0;
                p->s_lock = 0;
                p->s_wait = NULL;
        }
        if (!(p=read_super(ROOT_DEV)))
                panic("Unable to mount root");
        if (!(mi=iget(ROOT_DEV,ROOT_INO)))
                panic("Unable to read root i-node");
        mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
        p->s_isup = p->s_imount = mi;
        current->pwd = mi;
        current->root = mi;
        free=0;
        i=p->s_nzones;
        while (-- i >= 0)
                if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
                        free++;
        printk("%d/%d free blocks\n\r",free,p->s_nzones);
        free=0;
        i=p->s_ninodes+1;
        while (-- i >= 0)
                if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
                        free++;
        printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
