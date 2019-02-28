/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/**
 * 普通文件读函数
 *
 * inode: i节点
 * filp: 文件结构指针
 * buf: 用户空间缓冲区指针
 * count: 要读取的字节数
 *
 * 返回：实际读取的字节数，或出错号(< 0)
 *
 * 由i节点可以知道设备号，由filp可以直到文件中当前指针的位置，以此来读取文件的数据
 * 
 */
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
        int left,chars,nr;
        struct buffer_head * bh;

        // 判断参数的有效性
        if ((left=count)<=0) // 如果要读的字节数 <= 0, 直接返回0
                return 0;
        // 如果要读的字节数 > 0, 则执行下面循环
        while (left) {
                // 计算包含文件当前指针位置的数据块在设备上对应的逻辑块号
                if ((nr = bmap(inode,(filp->f_pos)/BLOCK_SIZE))) { // 计算出的逻辑块号不为 0
                        // 从设备读取数据块到高速缓冲区
                        if (!(bh=bread(inode->i_dev,nr))) // 从设备读取数据块失败
                                break; // 中止循环
                } else // 逻辑块号 == 0: 指定的数据块不存在
                        bh = NULL; // 置高速缓冲区指针为空
                nr = filp->f_pos % BLOCK_SIZE; // 计算文件读写指针在数据块中的偏移值
                // 1. 如果 BLOCK_SIZE-nr >= left  : 表示已经是需要读取到高速缓冲区中的最后一块数据块
                // 2. 如果 BLOCK_SIZE-nr < left  : 表示还需要在另外读取新的数据块到高速缓冲区
                chars = MIN( BLOCK_SIZE-nr , left ); // 计算当前高速缓冲块可读的字节数
                filp->f_pos += chars; // 文件读写指针向后移动 chars个字节（这次循环读取的字节数）
                left -= chars; // 要读的总字节数减少 chars个字节
                if (bh) { // bh 不为空
                        char * p = nr + bh->b_data; // p指向高速缓冲块数据区起始处后的nr个字节（开始读取数据的位置）
                        // 从“高速缓冲区” “一个一个字节地“ 拷贝到“用户缓冲区”，总共复制 chars个字节
                        while (chars-->0) 
                                put_fs_byte(*(p++),buf++); // p指针，buf指针，每次往后偏移一个字节
                        brelse(bh); // 释放高速缓冲块
                } else { // 要读的数据块不存在：直接往用户缓冲区填入chars个0值字节
                        // 这里的处理导致会有”文件空洞“的现象（实际文件占用的数据块 < 文件大小）
                        while (chars-->0)
                                put_fs_byte(0,buf++);
                }
        }
        //执行到这里已经读取完毕或者出错退出循环
        inode->i_atime = CURRENT_TIME; // 修改该i节点的访问时间为当前时间（UNIX格式）
        return (count-left)?(count-left):-ERROR; // 返回读取的字节数：如果读取的字节数为0，则返回 -ERROR
}

/**
 * 普通文件写函数
 *
 * inode: i节点
 * filp: 文件结构指针
 * buf: 用户空间缓冲区指针
 * count: 要写入的字节数
 *
 * 返回：实际写入的字节数，或出错号(< 0)
 * 
 */
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
        off_t pos;
        int block,c;
        struct buffer_head * bh;
        char * p;
        int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
        // 首先确定文件写入的位置
        if (filp->f_flags & O_APPEND) // 如果O_APPEND标志置位，每次都是从文件的最末尾开始添加数据
                pos = inode->i_size; // 文件读写指针移动到文件的最末尾
        else // 否则从文件的当前位置开始写入
                pos = filp->f_pos;
        // 如果已写入字节数（初始为0）小于需要写入的字节数(count), 执行下面循环： 
        while (i<count) {
                // 获取文件读写指针在设备上的逻辑块号 block
                // 注意：这里与读取时候的区别，bmap假定对应的逻辑块必须已经存在，而create_block如果逻辑块号不存在，则创建一块
                if (!(block = create_block(inode,pos/BLOCK_SIZE))) // 获取相应的逻辑块号失败
                        break; // 终止循环
                // 从设备读入相应的逻辑块到高速缓冲区
                if (!(bh=bread(inode->i_dev,block))) // 读取逻辑块到高速缓冲区失败
                        break; // 终止循环
                c = pos % BLOCK_SIZE; // 计算文件读写指针在缓冲块中的偏移 c 
                p = c + bh->b_data; // p指向高速缓冲块起始处后c个字节的位置
                bh->b_dirt = 1; // 置位高速缓冲块的修改标志
                c = BLOCK_SIZE-c; // 计算当前高速缓冲块可以写入的字节数
                if (c > count-i) // 如果当前高速缓冲块可写入的字节数 > 还需要写入的总字节数 
                        c = count-i; // c = 还需要写入的总字节数
                pos += c; // 文件读写指针向后移动 c个字节（本次循环写入的字节数）
                if (pos > inode->i_size) { // 如果此时pos的位置已经超过文件的长度
                        inode->i_size = pos; // 修改文件大小
                        inode->i_dirt = 1; // 置位i节点的修改标志
                }
                i += c; // 累加已经写入的总字节数
                // 从”用户缓冲区“一个个字节地拷贝到”高速缓冲区“中，总共拷贝 c 个字节
                while (c-->0)
                        *(p++) = get_fs_byte(buf++); // p, buf 各自每次向后移动一个字节
                brelse(bh); // 释放高速缓冲块
        }
        // 执行到这里已经写入完毕 或者 出错退出循环
        inode->i_mtime = CURRENT_TIME; // 更改文件修改时间为当前时间
        if (!(filp->f_flags & O_APPEND)) { // 如果 O_APPEND 标志没有置位
                filp->f_pos = pos; // 设置文件的读写指针
                inode->i_ctime = CURRENT_TIME; // 设置i节点的修改时间为当前时间
        }
        return (i?i:-1); // 返回总共写入的字节数：如果总写入的字节数 == 0 ，则返回 -1 表示出错
}
