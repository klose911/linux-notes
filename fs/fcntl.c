/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h> */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <fcntl.h> // 文件控制头文件
#include <sys/stat.h>

extern int sys_close(int fd);

/*
 * 复制文件描述符
 *
 * fd: 被复制的文件描述符
 * arg: 指定新文件描述符的最小值
 *
 * 成功：返回新文件描述符，失败：返回出错码
 * 
 */
static int dupfd(unsigned int fd, unsigned int arg)
{
        // 校验被复制的文件描述符的是否有效
        if (fd >= NR_OPEN || !current->filp[fd])
                return -EBADF; // 返回错误码 EBADF
        // 校验新文件描述符的最小值的有效性
        if (arg >= NR_OPEN) 
                return -EINVAL; // 返回错误码 EINVAL
        // 遍历进程的打开文件表 filp 
        while (arg < NR_OPEN)
                if (current->filp[arg]) // 如果arg所处的文件表项已经被使用
                        arg++; // 尝试下一个
                else
                        break; // 找到一个空闲的文件表项
        if (arg >= NR_OPEN) // 如果此时 arg >= 一个文件允许打开的最大文件数
                return -EMFILE; // 返回错误码 EMFILE 
        current->close_on_exec &= ~(1<<arg); // 复位'close_on_exec'位图中'arg'对应的位
        // 1. arg对应的文件结构指针(current->filp[arg]) 赋值为：fd对应的文件结构指针(current->filp[fd])
        // 2. 文件对应的引用计数 + 1 
        (current->filp[arg] = current->filp[fd])->f_count++;
        return arg; // 返回新的文件描述符
}

/**
 * 复制文件描述符2（系统调用）
 *
 * oldfd: 旧的文件描述符
 * newfd: 新的文件描述符
 *
 * 成功：返回新的文件描述符，失败：返回错误码
 *
 * 如果新的文件描述符已经被打开的文件所使用，则先关闭文件
 * 
 */
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
        //注意：这个函数不是原子操作！！！
        sys_close(newfd); // 如果newfd已经打开，则先关闭
        return dupfd(oldfd,newfd); 
}

/**
 * 复制文件描述符（系统调用）
 *
 * fileds: 欲复制的文件描述符
 *
 * 成功：返回新的文件描述符，失败：返回错误码
 * 
 */
int sys_dup(unsigned int fildes)
{
        return dupfd(fildes,0);
}

/**
 * 文件控制（系统调用）
 *
 * fd：文件描述符
 * cmd：功能
 * args：可选参数
 *
 * 成功：返回值依赖于cmd，失败：返回对应的错误码
 * 
 */
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
        struct file * filp;

        // 校验文件描述符参数是否有效
        if (fd >= NR_OPEN || !(filp = current->filp[fd]))
                return -EBADF; // 返回错误码 EBADF
        // 根据不同的cmd进行处理
        switch (cmd) {
		case F_DUPFD: // 复制文件描述符
                return dupfd(fd,arg);
		case F_GETFD: // 获取“文件描述符”的“执行时关闭”(close_on_exec)标志
                return (current->close_on_exec>>fd)&1;
		case F_SETFD: // 修改“文件描述符”的“执行时关闭”(close_on_exec)标志
                if (arg&1) // 置位
                        current->close_on_exec |= (1<<fd);
                else // 复位
                        current->close_on_exec &= ~(1<<fd); // 复位
                return 0; // 返回0：表示成功
		case F_GETFL: // 获取文件描述符的“属性和访问模式”信息
                return filp->f_flags; 
		case F_SETFL: // 根据arg设置O_APPEND(添加)和O_NONBLOCK（非阻塞）标志
                filp->f_flags &= ~(O_APPEND | O_NONBLOCK); 
                filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
                return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW: // 文件锁功能未实现
                return -1; // 直接返回 -1
		default: // 未知命令
                return -1; // 直接返回 -1 
        }
}
