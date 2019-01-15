/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h> // 内核调度头文件

#include <sys/stat.h> // 文件状态头文件，含有文件或文件系统状态结构 stat{} 和常量

/*
 * 释放所有一级间接块
 *
 * dev: 设备号
 * block: 一次间接块号(i_zone[7])
 *
 * 无返回值
 * 
 */
static void free_ind(int dev,int block)
{
        struct buffer_head * bh;
        unsigned short * p;
        int i;

        if (!block) // 校验逻辑块号是否为0
                return; // 如果为0，则直接返回

        // 把设备上对应的一次间接块读入到高速缓冲区
        if ((bh=bread(dev,block))) {
                p = (unsigned short *) bh->b_data; // 指向该缓冲头的数据区
                // 按照２字节遍历这个缓冲区（每个i节点是２个字节）
                for (i=0;i<512;i++,p++) {
                        if (*p) { 
                                free_block(dev,*p); // 释放指定的设备上的逻辑块（这些都是数据块）
                        }
                }
                brelse(bh); // 释放间接块占用的高速缓冲区中的缓冲块
        }
        free_block(dev,block); // 释放设备上的一次间接块
}


/*
 * 释放所有二次间接块
 *
 * dev: 设备号
 * block: 二次间接块号(i_zone[8])
 *
 * 无返回值
 * 
 */
static void free_dind(int dev,int block)
{
        struct buffer_head * bh;
        unsigned short * p;
        int i;

        if (!block)
                return;
        if ((bh=bread(dev,block))) {
                p = (unsigned short *) bh->b_data;
                for (i=0;i<512;i++,p++) {
                        if (*p) {
                                // 代码和 free_ind 只有这里不同，这里是再次调用 free_ind来释放对应的二次间接块的数据块
                                free_ind(dev,*p);
                        }
                }
                brelse(bh);
        }
        free_block(dev,block);
}

/**
 * 截断文件数据函数
 *
 * inode: 文件对应的内存中的i节点指针
 *
 * 无返回值
 *
 * 将节点对应的文件长度截为０，并释放占用的设备空间
 *
 */
void truncate(struct m_inode * inode)
{
        int i;

        // 只有常规文件或目录文件，才可以被截断为0
        if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
                return; // 非常规文件和目录文件，直接返回

        // 遍历这个 i节点中的直接块号数组：i_zone[0] ~ i_zone[6]
        for (i=0;i<7;i++) {
                if (inode->i_zone[i]) {
                        free_block(inode->i_dev,inode->i_zone[i]); // 释放对应的数据块
                        inode->i_zone[i]=0; // 数据块号字段设置为 0 
                }
        }
        free_ind(inode->i_dev,inode->i_zone[7]); // 释放所有的一次间接块：i_zone[7]
        free_dind(inode->i_dev,inode->i_zone[8]); // 释放所有的二次间接块：i_zone[8]
        inode->i_zone[7] = inode->i_zone[8] = 0; // 一次间接块号，二次间接块号都设置为 0

        inode->i_size = 0; // i节点的字节大小设置为 0  
        inode->i_dirt = 1; // i节点的修改标志设置为 真
        inode->i_mtime = inode->i_ctime = CURRENT_TIME; // i节点的修改时间 = i节点的创建时间　设置为当前 unix 时间（秒数）
}
