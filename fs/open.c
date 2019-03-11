/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h> */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/**
 * 获取文件系统信息
 *
 * dev: 设备号
 * ubuf: ustat结构指针，用于返回已安装(mounted)的文件系统信息
 *
 * 成功：返回 0
 * 
 */
int sys_ustat(int dev, struct ustat * ubuf)
{
        return -ENOSYS; // 当前版本还没实现
}

/**
 * 设置文件访问和修改时间
 *
 * filename: 文件名
 * times: utimbuf结构指针，包含访问和修改时间
 *
 * 成功：返回 0，失败：返回出错码
 *
 * 如果times不为NULL，则取其中的信息来设置文件的访问和修改时间
 * 如果times为NULL，则取系统当前时间来设置文件的访问和修改时间
 * 
 */
int sys_utime(char * filename, struct utimbuf * times)
{
        struct m_inode * inode;
        long actime,modtime;

        // 根据路径名获取对应的i节点
        if (!(inode=namei(filename))) // 无法获取对应的文件i节点
                return -ENOENT; // 返回 -ENOENT

        // 判断 times参数是否为 NULL
        if (times) { // 不为 NULL
                // 获取文件的访问和修改时间
                // 注意：times位于进程的用户空间中，所以必须调用 get_fs_long !!!
                actime = get_fs_long((unsigned long *) &times->actime);
                modtime = get_fs_long((unsigned long *) &times->modtime);
        } else // 为 NULL
                actime = modtime = CURRENT_TIME; // 设置为当前时间
        
        inode->i_atime = actime; // 设置i节点的访问时间
        inode->i_mtime = modtime; // 设置i节点的修改时间
        inode->i_dirt = 1; // 设置i节点的修改标志
        iput(inode); // 释放 i节点
        return 0; // 返回 0作为成功标志
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */

/**
 * 校验文件的访问权限
 *
 * filename: 文件名
 * mode: 要校验的访问属性(4: R_OK, 2: W_OK, 1: X_OK, 0: F_OK) 
 *
 * 如果允许访问：返回 0， 否则：返回出错码
 *
 * 注意：这里还没有决定根据进程的”有效用户ID“或者”真实用户ID“来判断，只是根据当前进程的用户ID 
 * （POSIX标准建议使用真实用户ID，BSD使用真实用户ID）
 * 
 */
int sys_access(const char * filename,int mode)
{
        struct m_inode * inode;
        int res, i_mode;

        mode &= 0007; // 访问属性只有低3位有效，这里用来清除无用的高位

        // 根据路径名获取对应的i节点
        if (!(inode=namei(filename))) // 无法获取对应的文件i节点
                return -EACCES; // 返回 -EACCES 
        i_mode = res = inode->i_mode & 0777; // 获得i节点中的文件访问权限：取i节点属性位的低9位
        iput(inode); // 放回i节点

        // "UUUGGGOOO": UUU代表文件宿主访问属性, GGG代表文件组访问属性，OOO代表文件的其他用户访问属性 
        if (current->uid == inode->i_uid) // 如果当前进程的用户id == i节点的宿主用户id
                res >>= 6; // 取文件“宿主访问属性”：res右移6位，使得UUU变成最低三位
        else if (current->gid == inode->i_gid) // 如果当前进程的组id == i节点的组id
                res >>= 3; // 取文件组“访问属性”：res右移3位，使得GGG变成最低三位

        // 其他用户无须移位
        if ((res & 0007 & mode) == mode) // 取最后三位 和 mode 参数做比较
                return 0; // 有权限，返回 0 
        /*
         * XXX we are doing this test last because we really should be
         * swapping the effective with the real user id (temporarily),
         * and then calling suser() routine.  If we do call the
         * suser() routine, it needs to be called last. 
         */

        // 当前进程是超级用户 并且 （访问属性的“执行位”是0 或者 文件可以被任何人执行，修改，搜索） 
        if ((!current->uid) &&
            (!(mode & 1) || (i_mode & 0111))) 
                return 0; // 返回 0 
        return -EACCES; // 无访问权限：返回 -EACCES
}

/**
 * 改变当前进程的工作目录
 *
 * filename: 目录路径名
 *
 * 成功：返回 0，失败：返回出错码
 * 
 */
int sys_chdir(const char * filename)
{
        struct m_inode * inode;

        // 根据路径名获取对应的i节点
        if (!(inode = namei(filename))) // 无法获取对应目录的i节点
                return -ENOENT; // 返回 -ENOENT
        if (!S_ISDIR(inode->i_mode)) { // 对应i节点不是目录
                iput(inode); // 放回i节点
                return -ENOTDIR; // 返回 -ENOTDIR 
        }
        iput(current->pwd); // 放回当前进程的pwd域对应的i节点
        current->pwd = inode; // 设置当前进程的pwd域为获取的i节点
        return (0); // 返回 0 
}

/**
 * 改变当前进程的根目录
 *
 * filename: 目录路径名
 *
 * 成功：返回 0，失败：返回出错码
 * 
 */
int sys_chroot(const char * filename)
{
        struct m_inode * inode;

        // 根据路径名获取对应的i节点 
        if (!(inode=namei(filename))) // 无法获取对应的文件i节点 
                return -ENOENT; // 返回 -ENOENT
        if (!S_ISDIR(inode->i_mode)) { // 对应i节点不是目录
                iput(inode); // 放回i节点
                return -ENOTDIR; // 返回 -ENOTDIR
        }
        iput(current->root); // 放回当前进程的root域对应的i节点
        current->root = inode; // 设置当前进程的root域为获取的i节点
        return (0); // 返回 0
}

/**
 * 修改文件权限
 *
 * filename: 目录路径名
 * mode: 新的文件访问属性(类似与755, 644 ...)
 *
 * 成功：返回 0，失败：返回出错码
 * 
 */
int sys_chmod(const char * filename,int mode)
{
        struct m_inode * inode;

        // 根据路径名获取对应的i节点 
        if (!(inode=namei(filename))) // 无法获取对应的文件i节点
                return -ENOENT; // 返回 -ENOENT
        // "当前进程的有效用户id != i节点的宿主用户id" 并且 "当前进程的有效用户不是root" 
        if ((current->euid != inode->i_uid) && !suser()) {
                iput(inode); // 放回i节点
                return -EACCES; // 无修改权限：返回 -EACCES
        }
        // 设置i节点i_mode属性低12位
        inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
        inode->i_dirt = 1; // 设置i节点的修改标志
        iput(inode); // 放回i节点
        return 0; // 返回 0
}

/**
 * 修改文件的宿主，组标识符
 *
 * filename: 目录路径名
 * uid: 用户id
 * gid: 组id
 *
 * 成功：返回 0，失败：返回出错码
 *
 * 注意：此版本只有超级用户可以修改文件的宿主，组标识符，可能太严苛
 * 
 */
int sys_chown(const char * filename,int uid,int gid)
{
        struct m_inode * inode;

        // 根据路径名获取对应的i节点
        if (!(inode=namei(filename))) // 无法获取对应的文件i节点
                return -ENOENT; // 返回 -ENOENT
        if (!suser()) { // ”当前进程的有效用户ID“不是root
                iput(inode); // 放回i节点
                return -EACCES; // 无修改权限：返回 -EACCES
        }
        inode->i_uid=uid; // 设置i节点的宿主
        inode->i_gid=gid; // 设置i节点的组
        inode->i_dirt=1; // 设置i节点的修改标志
        iput(inode); // 放回i节点
        return 0; // 返回 0
}

int sys_open(const char * filename,int flag,int mode)
{
        struct m_inode * inode;
        struct file * f;
        int i,fd;

        mode &= 0777 & ~current->umask;
        for(fd=0 ; fd<NR_OPEN ; fd++)
                if (!current->filp[fd])
                        break;
        if (fd>=NR_OPEN)
                return -EINVAL;
        current->close_on_exec &= ~(1<<fd);
        f=0+file_table;
        for (i=0 ; i<NR_FILE ; i++,f++)
                if (!f->f_count) break;
        if (i>=NR_FILE)
                return -EINVAL;
        (current->filp[fd]=f)->f_count++;
        if ((i=open_namei(filename,flag,mode,&inode))<0) {
                current->filp[fd]=NULL;
                f->f_count=0;
                return i;
        }
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
        if (S_ISCHR(inode->i_mode)) {
                if (MAJOR(inode->i_zone[0])==4) {
                        if (current->leader && current->tty<0) {
                                current->tty = MINOR(inode->i_zone[0]);
                                tty_table[current->tty].pgrp = current->pgrp;
                        }
                } else if (MAJOR(inode->i_zone[0])==5)
                        if (current->tty<0) {
                                iput(inode);
                                current->filp[fd]=NULL;
                                f->f_count=0;
                                return -EPERM;
                        }
        }
/* Likewise with block-devices: check for floppy_change */
        if (S_ISBLK(inode->i_mode))
                check_disk_change(inode->i_zone[0]);
        f->f_mode = inode->i_mode;
        f->f_flags = flag;
        f->f_count = 1;
        f->f_inode = inode;
        f->f_pos = 0;
        return (fd);
}

int sys_creat(const char * pathname, int mode)
{
        return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

int sys_close(unsigned int fd)
{	
        struct file * filp;

        if (fd >= NR_OPEN)
                return -EINVAL;
        current->close_on_exec &= ~(1<<fd);
        if (!(filp = current->filp[fd]))
                return -EINVAL;
        current->filp[fd] = NULL;
        if (filp->f_count == 0)
                panic("Close: file count is 0");
        if (--filp->f_count)
                return (0);
        iput(filp->f_inode);
        return (0);
}
