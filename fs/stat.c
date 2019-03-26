/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h> // 文件状态头文件，含有文件状态结构stat和相关常量

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/*
 * 复制文件状态信息
 *
 * inode: 文件i节点指针
 * statbuf: 用户数据空间中 stat结构指针
 *
 * 无返回值
 * 
 */
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
        struct stat tmp;
        int i;

        // 验证（或分配）存放数据的内存空间（位于用户数据段中）
        verify_area(statbuf,sizeof (* statbuf));
        // 复制i节点的信息到临时stat结构tmp变量上（位于内核栈中）
        tmp.st_dev = inode->i_dev; // 文件所在的设备号
        tmp.st_ino = inode->i_num; // 文件i节点号
        tmp.st_mode = inode->i_mode; // 文件的属性和权限
        tmp.st_nlink = inode->i_nlinks; // 文件的硬链接数
        tmp.st_uid = inode->i_uid; // 文件的宿主id 
        tmp.st_gid = inode->i_gid; // 文件的组id 
        tmp.st_rdev = inode->i_zone[0]; // 文件的真实设备（块设备，字符设备有效）
        tmp.st_size = inode->i_size; // 文件大小（文件是常规文件或目录） 
        tmp.st_atime = inode->i_atime; // 文件的最后访问时间
        tmp.st_mtime = inode->i_mtime; // 文件的最后修改时间
        tmp.st_ctime = inode->i_ctime; // 文件i节点的最后修改时间
        // 从内核栈上一个字节一个字节地拷贝到用户数据段的statbuf结构中
        for (i=0 ; i<sizeof (tmp) ; i++)
                put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}

/**
 * 根据文件路径名获取文件状态（系统调用）
 *
 * filename: 文件路径名
 * statbuf: 用户数据空间中 stat结构指针
 *
 * 成功：返回 0，失败：返回错误号
 * 
 */
int sys_stat(char * filename, struct stat * statbuf)
{
        struct m_inode * inode;

        // 根据文件路径名获取相应的i节点
        if (!(inode=namei(filename))) // 获取i节点失败
                return -ENOENT; // 返回错误号 -ENOENT 
        cp_stat(inode,statbuf); // 拷贝文件状态信息到用户数据空间
        iput(inode); // 放回i节点
        return 0; // 返回0：表示成功
}

/**
 * 根据文件描述符获取文件状态（系统调用）
 *
 * fd: 文件描述符
 * statbuf: 用户数据空间中 stat结构指针
 *
 * 成功：返回 0，失败：返回错误号
 * 
 */
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
        struct file * f;
        struct m_inode * inode;

        // 校验文件描述符的有效性
        if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
                return -EBADF; // 返回错误号 EBADF 
        cp_stat(inode,statbuf); // 拷贝文件状态信息到用户数据空间
        return 0; // 返回0：表示成功
}
