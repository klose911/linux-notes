/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>

/**
 * 管道读操作函数
 *
 * inode: 管道对应的i节点
 * buf: 用户空间数据缓冲区指针
 * count: 要读取的字节数
 *
 * 返回：读取的总字节数，0表示失败
 * 
 */
int read_pipe(struct m_inode * inode, char * buf, int count)
{
        int chars, size, read = 0;

        // 如果要读取的字节数 > 0, 执行下列循环
        while (count>0) {
                // 计算管道可用数据的大小
                while (!(size=PIPE_SIZE(*inode))) { // 管道中可用数据的大小 == 0 ： 管道为空
                        wake_up(&inode->i_wait); // 唤醒“写管道”的进程(inode->i_wait)
                        // 如果已经没有写管道的进程：i节点的引用计数 != 2 
                        if (inode->i_count != 2) /* are there any writers? */
                                return read; // 返回读取到的字节数
                        // 这里没有考虑到信号，后面版本对此进行了修改！！！
                        sleep_on(&inode->i_wait); // 让当前进程在管道的i节点的等待队列休眠（不可中断），等待管道被写入字节
                }
                // 运行到这里，说明管道中有字节可读
                chars = PAGE_SIZE-PIPE_TAIL(*inode); // 计算“管道尾指针”到“缓冲区末端”的字节数
                if (chars > count) // 如果 chars > 要读的字节数
                        chars = count; // chars = 要读的字节数
                if (chars > size) // 如果 chars > 管道当前可读的字节数 
                        chars = size; // chars = 管道当前可读的字节数
                count -= chars; // 要读的总字节数 - 本次循环管道可读的字节数
                read += chars; // 读到的总字节数 + 本次循环管道可读的字节数
                size = PIPE_TAIL(*inode); // 令size指向当前管道尾指针处（也就是将要开始读取的地方）
                // 调整i节点的管道尾指针（读取管道用）：i_zone[1] 加上 chars个字节，然后取余
                PIPE_TAIL(*inode) += chars; 
                PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
                // 从管道的 inode->i_size[size]处开始 一个字节一个字节地 复制到“用户缓冲区”，总共拷贝chars个字节
                while (chars-->0)
                        put_fs_byte(((char *)inode->i_size)[size++],buf++); // size, buf 自增
        }
        // 当此次读操作完成后，唤醒写管道的进程
        wake_up(&inode->i_wait);
        return read; // 返回当前已经读取的总字节数
}

/**
 * 管道写操作函数
 *
 * inode: 管道对应的i节点
 * buf: 用户空间数据缓冲区指针
 * count: 要写入的字节数
 *
 * 返回：写入的总字节数，-1 表示失败
 * 
 */
int write_pipe(struct m_inode * inode, char * buf, int count)
{
        int chars, size, written = 0;

// 如果要写入的字节数 > 0, 执行下列循环
        while (count>0) {
                // 计算管道空闲空间
                while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) { // 空闲空间 == 0 : 管道已满
                        wake_up(&inode->i_wait); // 唤醒读取管道的进程
                        // 如果管道i节点的引用计数 != 2 : 没有读取进程
                        if (inode->i_count != 2) { /* no readers */
                                current->signal |= (1<<(SIGPIPE-1)); // 向当前进程发送 SIGPIPE 信号
                                return written?written:-1; // 返回已经写入的字节数，如果没有写入任何字节，返回 -1 表示失败
                        }
                        sleep_on(&inode->i_wait); // 当前进程进入休眠（不可中断），等待“读取管道的进程”从管道读取数据
                }
                // 运行到这里，说明管道中有字节可写
                chars = PAGE_SIZE-PIPE_HEAD(*inode); // 计算管道头指针到管道末端的字节数
                if (chars > count) // 如果 chars > 要写的字节数
                        chars = count; //  chars = 要写的字节数
                if (chars > size) // 如果 chars > 管道当前可写的字节数
                        chars = size; // chars = 管道当前可写的字节数
                count -= chars; // 要写的总字节数 - 本次循环管道可写的字节数
                written += chars; // 写入的总字节数 + 本次循环管道可写的字节数
                size = PIPE_HEAD(*inode); // 令size指向当前管道头指针处（也就是将要开始写入的地方）
                // 调整i节点的管道头指针（写入管道用）：i_zone[0] 加上 chars个字节，然后取余
                PIPE_HEAD(*inode) += chars;
                PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
                // 从“用户缓冲区”一个字节一个字节地复制到管道的 inode->i_size[size]处，总共复制 chars 个字节
                while (chars-->0)
                        ((char *)inode->i_size)[size++]=get_fs_byte(buf++); // size, buf 自增
        }
        // 当此次写操作完成后，唤醒读管道的进程
        wake_up(&inode->i_wait);
        return written; // 返回当前已经写入的总字节数
}

/**
 * 创建一个无名管道（系统调用）
 *
 * filedes: 文件描述符数组，返回创建的一对文件描述符，这对文件描述符指向同一个i节点，filedes[0]用于读管道数据，filedees[1]用于写管道数据
 *
 * 成功：返回 0, 失败：返回 -1 
 *
 * 注意：filedes位于用户进程空间
 *
 */
int sys_pipe(unsigned long * fildes)
{
        struct m_inode * inode;
        struct file * f[2]; // 文件结构数组
        int fd[2]; // 文件描述符数组
        int i,j;

        j=0;
        // 在系统的文件表里寻找两个空闲项
        for(i=0;j<2 && i<NR_FILE;i++)
                if (!file_table[i].f_count) // 引用计数为0：说明这是要找的空闲项
                        (f[j++]=i+file_table)->f_count++; // 把找到的空闲项的引用计数加1
        if (j==1) // 如果只找到一个文件空闲项
                f[0]->f_count=0; // 复位找到的空闲项的引用计数
        if (j<2) // 无法找到两个文件空闲项，直接返回 -1 
                return -1;

        j=0;
        // 在当前进程的文件表里寻找两个空闲项
        for(i=0;j<2 && i<NR_OPEN;i++) {
                if (!current->filp[i]) { // 当前进程的文件表的某一项为空
                        fd[j] = i; // 设置文件描述符：进程文件表的索引
                        current->filp[i] = f[j]; // 设置当前进程的文件表的数据项
                        j++; // j递增
                }
        }
    
        if (j==1) // 如果在当前进程的文件表里只找到一个空闲项
                current->filp[fd[0]]=NULL; // 置空已经设置过的进程文件表的数据项
        if (j<2) { // 当前进程的文件表里无法找到两个空闲项
                f[0]->f_count=f[1]->f_count=0; // 复位从系统文件表中找到的2个文件项的引用计数
                return -1; // 返回 -1 
        }

        // 创建一个管道i节点
        if (!(inode=get_pipe_inode())) { // 创建管道i节点失败
                current->filp[fd[0]] =
                        current->filp[fd[1]] = NULL; // 置空当前进程文件表的相关项
                f[0]->f_count = f[1]->f_count = 0; // 复位系统文件表相关项的引用计数
                return -1; // 返回 -1
        }
        
        f[0]->f_inode = f[1]->f_inode = inode; // f[0],f[1]指向刚才分配的管道i节点
        f[0]->f_pos = f[1]->f_pos = 0; // f[0], f[1]的偏移量都设置为0

        // f[0]文件：用于读， f[1]文件：用于写 
        f[0]->f_mode = 1;		/* read */
        f[1]->f_mode = 2;		/* write */

        // 将文件描述符的值复制到用户空间filedes数组中
        put_fs_long(fd[0],0+fildes);
        put_fs_long(fd[1],1+fildes);
        
        return 0;
}
