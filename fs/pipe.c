/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>

int read_pipe(struct m_inode * inode, char * buf, int count)
{
        int chars, size, read = 0;

        while (count>0) {
                while (!(size=PIPE_SIZE(*inode))) {
                        wake_up(&inode->i_wait);
                        if (inode->i_count != 2) /* are there any writers? */
                                return read;
                        sleep_on(&inode->i_wait);
                }
                chars = PAGE_SIZE-PIPE_TAIL(*inode);
                if (chars > count)
                        chars = count;
                if (chars > size)
                        chars = size;
                count -= chars;
                read += chars;
                size = PIPE_TAIL(*inode);
                PIPE_TAIL(*inode) += chars;
                PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
                while (chars-->0)
                        put_fs_byte(((char *)inode->i_size)[size++],buf++);
        }
        wake_up(&inode->i_wait);
        return read;
}
	
int write_pipe(struct m_inode * inode, char * buf, int count)
{
        int chars, size, written = 0;

        while (count>0) {
                while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
                        wake_up(&inode->i_wait);
                        if (inode->i_count != 2) { /* no readers */
                                current->signal |= (1<<(SIGPIPE-1));
                                return written?written:-1;
                        }
                        sleep_on(&inode->i_wait);
                }
                chars = PAGE_SIZE-PIPE_HEAD(*inode);
                if (chars > count)
                        chars = count;
                if (chars > size)
                        chars = size;
                count -= chars;
                written += chars;
                size = PIPE_HEAD(*inode);
                PIPE_HEAD(*inode) += chars;
                PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
                while (chars-->0)
                        ((char *)inode->i_size)[size++]=get_fs_byte(buf++);
        }
        wake_up(&inode->i_wait);
        return written;
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
