/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

/**
 * ioctl 文件实现了输入/输出系统调用ioctl. 这个函数可以看作是各个具体驱动程序ioctl的接口函数
 * 接口将调用文件描述符对应的设备文件的驱动程序中的IO控制函数，主要调用终端tty字符设备的tty_ioctl函数，对终端的IO进行控制
 * 
 */
/* #include <string.h>*/
#include <errno.h>
#include <sys/stat.h>

#include <linux/sched.h>

extern int tty_ioctl(int dev, int cmd, int arg); // chr_drv/tty_ioctl.c

// 定义输入输出控制(ioctl)的函数指针
// 函数的参数：int dev, int cmd, int arg, 函数的返回值 int 
typedef int (*ioctl_ptr)(int dev,int cmd,int arg);

#define NRDEVS ((sizeof (ioctl_table))/(sizeof (ioctl_ptr))) // 设备数目的宏

// ioctl函数指针表
static ioctl_ptr ioctl_table[]={
        NULL,		// 没有设备
        NULL,		// 内存
        NULL,		// 软盘
        NULL,		// 硬盘
        tty_ioctl,	// 串行终端
        tty_ioctl,	// 控制终端
        NULL,		// 打印机
        NULL};		// 有名管道
	

/**
 * 输入输出控制（系统调用）
 *
 * fd：文件描述符
 * cmd：功能
 * args：可选参数
 *
 * 成功：返回值依赖于cmd，失败：返回对应的错误码
 * 
 */
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
        struct file * filp;
        int dev,mode;

        // 校验文件描述符参数是否有效
        if (fd >= NR_OPEN || !(filp = current->filp[fd]))
                return -EBADF; // 返回错误码 EBADF
        mode=filp->f_inode->i_mode; // 获取对应文件的类型和属性
        if (!S_ISCHR(mode) && !S_ISBLK(mode)) // 文件不是块设备也不是字符设备
                return -EINVAL; // 返回错误码 EINVAL 
        dev = filp->f_inode->i_zone[0]; // 获取文件对应的物理设备号
        if (MAJOR(dev) >= NRDEVS) // 主设备号大于支持的设备种类
                return -ENODEV; // 返回错误码 ENODEV
        if (!ioctl_table[MAJOR(dev)]) // ioctl函数指针表中对应项为空
                return -ENOTTY; // 返回错误码 ENOTTY
        return ioctl_table[MAJOR(dev)](dev,cmd,arg); // 调用对应的驱动程序中的输入/输出控制函数（终端设备对应的是 'tty_ioctl'）
}
