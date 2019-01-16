/*
 * This file has definitions for some important file table
 * structures etc.
 */

/**
 * 文件系统头文件
 */
#ifndef _FS_H
#define _FS_H

#include <sys/types.h> //类型头文件 

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

/*
 * 系统所包含的设备，下面是“主设备号”
 * 0 - 没有使用
 * 1 - /dev/mem 内存设备
 * 2 - /dev/fd 软盘
 * 3 - /dev/hd 硬盘
 * 4 - /dev/ttyx tty串行终端设备
 * 5 - /dev/tty tty终端设备
 * 6 - /dev/lp 打印设备
 * 7 - 匿名管道
 */
#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3) // 判断设备是否可以寻找定位（随机访问），只有内存，软盘，硬盘支持


#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

/**
 * 高速缓存区初始化函数
 */
void buffer_init(long buffer_end);

// 设备号用一个字表示，高字节是主设备号，低字节是次设备号（0x32: 表示的是第二块硬盘）
#define MAJOR(a) (((unsigned)(a))>>8) // 取高字节，主设备号
#define MINOR(a) ((a)&0xff) // 取低字节，次设备号

#define NAME_LEN 14 // 名字长度值
#define ROOT_INO 1 // 根 i节点

#define I_MAP_SLOTS 8 // “i节点位图”槽数
#define Z_MAP_SLOTS 8 // “逻辑块位图”槽数
#define SUPER_MAGIC 0x137F // “超级块”魔数

#define NR_OPEN 20 // 进程最多打开的文件数
#define NR_INODE 32 // 系统最多使用的i节点数
#define NR_FILE 64 // 系统最多同时打开的文件个数（文件数组项数）
#define NR_SUPER 8 // 系统所含最多的超级块个数（超级块数组项数），这意味着系统最多支持挂载8个分区
#define NR_HASH 307 // 缓冲区 Hash 表数组项数值
#define NR_BUFFERS nr_buffers // 系统所含缓冲块个数（初始化后不再改变）
#define BLOCK_SIZE 1024 // 逻辑块长度（字节值 1024B = 1KB）  
#define BLOCK_SIZE_BITS 10 // 数据块长度所占的比特位数 (2 ^ 10 = 1024) 
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode))) // 每个逻辑块可存储的 i节点 个数 (1024 / 32 = 32 个)
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry))) // 每个逻辑块能保存的目录项数 (1024 / 16 = 64个)

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))
#define INC_PIPE(head)                                  \
        __asm__("incl %0\n\tandl $4095,%0"::"m" (head))

typedef char buffer_block[BLOCK_SIZE]; // 块缓冲区类型定义（声明 'buffer_block buf' 等价于声明 'char[BLOCK_SIZE] buf'）

/**
 * 缓冲块头数据结构（极为重要！！！）
 * 在程序中常用 bh 来表示 buffer_head类型的缩写 
 */
struct buffer_head {
        // 指向内存中一个数据块（1024字节）
        char * b_data;			/* pointer to data block (1024 bytes) */
        // 数据块号码
        unsigned long b_blocknr;	/* block number */
        // 数据源的设备号
        unsigned short b_dev;		/* device (0 = free) */
        // 最新标志：
        unsigned char b_uptodate;
        // 修改标志：0- 未修改，1- 已修改
        unsigned char b_dirt;		/* 0-clean,1-dirty */
        // 使用的用户数
        unsigned char b_count;		/* users using this block */
        // 是否被锁定：0- 未锁定，1- 已锁定
        unsigned char b_lock;		/* 0 - ok, 1 -locked */
        struct task_struct * b_wait; // 指向等待该缓存区解锁的任务（进程）
        // 下面四个指针用于缓冲区管理
        struct buffer_head * b_prev; // hash队列上前一块
        struct buffer_head * b_next; // hash队列上下一块
        struct buffer_head * b_prev_free; // 空闲表上前一个
        struct buffer_head * b_next_free; // 空闲表上下一块
};

/**
 * 磁盘上 i节点（索引节点）的数据结构 
 */
struct d_inode {
        unsigned short i_mode; // 文件类型和属性（rwx位）
        unsigned short i_uid; // 用户 uid 
        unsigned long i_size; // 文件大小
        unsigned long i_time; // 修改时间（unix时间：秒）
        unsigned char i_gid; // 组 id 
        unsigned char i_nlinks; // 硬链接数（多少个目录项指向该节点，默认每个目录至少用2个）
        unsigned short i_zone[9]; // zone 是区段的意思，这里就是逻辑块数组，其中 i_zone[7]用于一级间接块，i_zone[8]用于二级间接块，其他是直接块
};

// 内存中的 i节点结构，前7项与 d_inode完全一样
struct m_inode {
        unsigned short i_mode;
        unsigned short i_uid;
        unsigned long i_size;
        unsigned long i_mtime;
        unsigned char i_gid;
        unsigned char i_nlinks;
        unsigned short i_zone[9];
/* these are in memory also */
        struct task_struct * i_wait; // 等待该i节点的进程
        unsigned long i_atime; // i节点自身修改时间
        unsigned long i_ctime; // i节点自身创建时间
        unsigned short i_dev; // i节点所在的设备号
        unsigned short i_num; // i节点号
        unsigned short i_count; // i节点被使用的次数（0表示该i节点处于空闲）
        unsigned char i_lock; // 锁定标志
        unsigned char i_dirt; // 已修改标志
        unsigned char i_pipe; // 管道标志
        unsigned char i_mount; // 挂载标志
        unsigned char i_seek; // 支持随机访问标志
        unsigned char i_update; // 更新标志
};

/**
 * 文件数据结构：“文件句柄”和“i节点”建立关系
 */
struct file {
        unsigned short f_mode; // 文件操作模式(RW位)
        unsigned short f_flags; // 文件打开和控制标志
        unsigned short f_count; // 文件引用计数器
        struct m_inode * f_inode; // 指向对应的“i节点”
        off_t f_pos; // 文件位置（读写偏移值）
};

/**
 * 内存中“磁盘超级块”的数据结构
 */
struct super_block {
        unsigned short s_ninodes; // i节点个数
        unsigned short s_nzones; // 逻辑块个数
        unsigned short s_imap_blocks; // i节点位图所占数据块个数
        unsigned short s_zmap_blocks; // 逻辑块位图所占数据块个数
        unsigned short s_firstdatazone; // 第一个开始写数据的逻辑块号
        unsigned short s_log_zone_size; // log(数据块长度/逻辑块长度)，以2为底，这里是0：数据块长度=逻辑块长度=1024B=1KB 
        unsigned long s_max_size; // 文件最大长度（单位字节）
        unsigned short s_magic; // 超级块魔数：0x137f
/* These are only in memory */
        struct buffer_head * s_imap[8]; // “i节点位图”缓冲块指针数组（占用8块，对应64M设备存储）
        struct buffer_head * s_zmap[8]; // ”逻辑块位图“缓冲块指针数组（占用8块）
        unsigned short s_dev; // 超级块所在的设备号
        struct m_inode * s_isup; // 被安装的文件系统根目录的“i节点”
        struct m_inode * s_imount; // 被安装到的“i节点”
        unsigned long s_time; // 修改时间
        struct task_struct * s_wait; // 等待该超级块的进程
        unsigned char s_lock; // 是否被锁定
        unsigned char s_rd_only; // 是否只读
        unsigned char s_dirt; // 是否被修改
};

/**
 * 磁盘中的文件系统超级块数据结构：和上面的 super_block 前8项完全一致
 */
struct d_super_block {
        unsigned short s_ninodes;
        unsigned short s_nzones;
        unsigned short s_imap_blocks;
        unsigned short s_zmap_blocks;
        unsigned short s_firstdatazone;
        unsigned short s_log_zone_size;
        unsigned long s_max_size;
        unsigned short s_magic;
};

/**
 * 文件目录项的数据结构
 */
struct dir_entry {
        unsigned short inode; // i节点号
        char name[NAME_LEN]; // 文件名
};

// 一些全局变量
extern struct m_inode inode_table[NR_INODE]; // 内存i节点数组（32项）
extern struct file file_table[NR_FILE]; // 文件表数组（64项）
extern struct super_block super_block[NR_SUPER]; // 超级块数组（8项）
extern struct buffer_head * start_buffer; // 缓冲区起始位置
extern int nr_buffers; // 缓冲块个数

// 软盘操作函数原型
extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);

//文件系统操作用的函数操作原型
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
                      struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

// 安装根文件系统
extern void mount_root(void);

#endif
