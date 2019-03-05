/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);
extern int read_pipe(struct m_inode * inode, char * buf, int count);
extern int write_pipe(struct m_inode * inode, char * buf, int count);
extern int block_read(int dev, off_t * pos, char * buf, int count);
extern int block_write(int dev, off_t * pos, char * buf, int count);
extern int file_read(struct m_inode * inode, struct file * filp,
                     char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,
                      char * buf, int count);

/**
 * 重定位文件读写指针
 *
 * fd: 文件描述符
 * offset: 新的文件读写偏移值
 * origin: 偏移的起始位置，SEEK_SET - 0（文件头），SEEK_CUR - 1（当前读写位置），SEEK_END - 2（文件尾）
 *
 * 成功：返回从定位后的文件读写指针值，失败：返回错误码
 * 
 */
int sys_lseek(unsigned int fd,off_t offset, int origin)
{
        struct file * file;
        int tmp;


        // 判断参数有效性
        // 1. 文件描述符的值 >= 进程打开的文件个数
        // 2. 文件描述符对应的文件File为空
        // 3. 对应的文件的i节点为空
        // 4. i节点的设备无法被重定位：只有软盘，硬驱，内存支持
        if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode) 
            || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
                return -EBADF; // 返回错误码：-EBADF
        if (file->f_inode->i_pipe) // 如果文件的i节点是管道
                return -ESPIPE; // 返回错误码：-ESPIPE

        // 根据设置的定位标志，重新定位文件的读写指针
        switch (origin) {
		case 0: // 从文件头
                if (offset<0) // 偏移值 < 0
                        return -EINVAL; // 返回错误码：-EINVAL
                file->f_pos=offset; // 直接设置“文件”结构的偏移值
                break;
		case 1: // 从当前位置
                if (file->f_pos+offset<0) // 当前读写指针的偏移 + 偏移值 < 0 
                        return -EINVAL; // 返回错误码：-EINVAL 
                file->f_pos += offset; // 累加偏移值
                break;
		case 2: // 从文件尾
                if ((tmp=file->f_inode->i_size+offset) < 0) // 文件大小 + 偏移值 < 0 
                        return -EINVAL; // 返回错误码：-EINVAL 
                file->f_pos = tmp;
                break;
		default: // 定位标志无效
                return -EINVAL; // 返回错误码：-EINVAL
        }
        return file->f_pos; // 返回重定位后的文件读写指针偏移值
}

/**
 * 读文件系统调用
 *
 * fd: 文件描述符
 * buf: 用户空间缓冲区指针
 * count: 读取的字节数
 *
 * 成功：返回读取的总字节数，失败返回：错误号
 * 
 */
int sys_read(unsigned int fd,char * buf,int count)
{
        struct file * file;
        struct m_inode * inode;

        // 判断参数的有效性
        // 1. 文件描述符的值 >= 进程打开的文件个数
        // 2. 读取的字节数 < 0
        // 3. 文件描述符对应的文件File为空
        if (fd >= NR_OPEN || count<0  || !(file=current->filp[fd]))
                return -EINVAL; // 返回错误码：-EINVAL 
        if (!count) // 读取的字节数 == 0
                return 0; // 返回 0 
        verify_area(buf,count); // 用户空间缓冲的内存验证
        inode = file->f_inode; // 获取文件对应的i节点指针

        // 根据i节点的属性，调用不同的读取实现函数
        if (inode->i_pipe) // i节点是管道
                return (file->f_mode & 1)?read_pipe(inode,buf,count):-EIO; // 如果管道i节点是“读模式”，则调用read_pipe，否则返回错误码：EIO 
        if (S_ISCHR(inode->i_mode)) // 字符文件
                return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos); // 调用字符设备读接口，其中 inode->i_zone[0] 作为 dev（设备号）参数传递
        if (S_ISBLK(inode->i_mode)) // 块设备
                return block_read(inode->i_zone[0],&file->f_pos,buf,count);// 调用块设备读函数，其中 inode->i_zone[0] 作为 dev（设备号）参数传递
        if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) { // 目录文件 或者 普通文件
                if (count+file->f_pos > inode->i_size) // 如果“读取的区域的最远处”超越“文件大小”
                        count = inode->i_size - file->f_pos; // 调整读取的字节数
                if (count<=0) // 如果 count <= 0
                        return 0; // 返回 0 
                return file_read(inode,file,buf,count); // 调用文件读取函数
        }
        printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode); // 打印i节点的文件类型和属性（调试用）
        return -EINVAL; // 不明属性的文件，返回错误码：-EINVAL 
}

/**
 * 写文件系统调用
 *
 * fd: 文件描述符
 * buf: 用户空间缓冲区指针
 * count: 写入的字节数
 *
 * 成功：返回写入的总字节数，失败返回：错误号
 * 
 */
int sys_write(unsigned int fd,char * buf,int count)
{
        struct file * file;
        struct m_inode * inode;

        // 判断参数的有效性
        // 1. 文件描述符的值 >= 进程打开的文件个数
        // 2. 写入的字节数 < 0
        // 3. 文件描述符对应的文件File为空
        if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
                return -EINVAL;
        if (!count) // 写入的字节数 == 0 
                return 0; // 直接返回 0 
        inode=file->f_inode; // 获取文件i节点

        // 根据i节点的属性，调用不同的写入实现函数
        if (inode->i_pipe) // 管道文件
                return (file->f_mode & 2) ? write_pipe(inode,buf,count):-EIO; // 如果管道i节点是“写模式”，则调用write_pipe，否则返回错误码：EIO 
        if (S_ISCHR(inode->i_mode)) // 字符设备
                return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos); // 调用字符设备写接口，其中 inode->i_zone[0] 作为 dev（设备号）参数传递
        if (S_ISBLK(inode->i_mode)) // 块设备
                return block_write(inode->i_zone[0],&file->f_pos,buf,count); // 调用块设备写函数，其中 inode->i_zone[0] 作为 dev（设备号）参数传递
        if (S_ISREG(inode->i_mode)) // 普通文件
                return file_write(inode,file,buf,count); // 调用文件写入函数
        // 注意： 不能直接读写目录文件，必须通过link, rmdir等！！！
        printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode); // 打印i节点的文件类型和属性（调试用）
        return -EINVAL; // 不明属性的文件，返回错误码：-EINVAL
}
