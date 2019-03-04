/*
 *  linux/fs/char_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#include <asm/segment.h>
#include <asm/io.h>

extern int tty_read(unsigned minor,char * buf,int count); // 终端驱动提供的读函数
extern int tty_write(unsigned minor,char * buf,int count); // 终端驱动提供的写函数

// 函数指针原型定义
// 参数：rw - 读/写， minor - 次设备号，buf - 用户空间缓存，count - 读写字节数，pos - 读写操作当前指针，对于终端设备无用
// 返回：int - 读写的字节数，失败则返回出错码
typedef int (*crw_ptr)(int rw,unsigned minor,char * buf,int count,off_t * pos);

/*
 * 串口终端操作函数
 *
 * rw: 读写命令
 * minor: 终端子设备号
 * buf: 用户空间缓存区
 * count: 读写字节数
 * pos: 读写操作当前指针，对于终端设备无用
 *
 * 成功返回读写的字节数，失败则返回出错码
 * 
 */
static int rw_ttyx(int rw,unsigned minor,char * buf,int count,off_t * pos)
{
        // 调用终端驱动操作函数
        return ((rw==READ)?tty_read(minor,buf,count):
                tty_write(minor,buf,count));
}

/*
 * 控制台终端操作函数：实现和 rw_ttyx() 类似，只是增加了“当前进程”是否有“控制终端”的检查
 *
 * rw: 读写命令
 * minor: 终端子设备号
 * buf: 用户空间缓存区
 * count: 读写字节数
 * pos: 读写操作当前指针，对于终端设备无用
 *
 * 成功返回读写的字节数，失败则返回出错码
 * 
 */
static int rw_tty(int rw,unsigned minor,char * buf,int count, off_t * pos)
{
        if (current->tty<0) // 如果当前进程未使用控制终端，直接返回 -EPERM
                return -EPERM;
        return rw_ttyx(rw,current->tty,buf,count,pos);
}

// 内存映射文件读写：未实现
static int rw_ram(int rw,char * buf, int count, off_t *pos)
{
        return -EIO;
}

// 物理内存数据读写：未实现
static int rw_mem(int rw,char * buf, int count, off_t * pos)
{
        return -EIO;
}

// 虚拟内存数据读写：未实现
static int rw_kmem(int rw,char * buf, int count, off_t * pos)
{
        return -EIO;
}

/*
 * 端口读写操作
 *
 * rw: 读写命令
 * buf: 用户空间缓存区
 * count: 读写字节数
 * pos: 端口地址
 *
 * 成功返回读写的字节数，失败则返回出错码
 * 
 */
static int rw_port(int rw,char * buf, int count, off_t * pos)
{
        int i=*pos;

        // 读写的字节数 > 0 并且 端口地址小于64K时，执行下列循环
        while (count-->0 && i<65536) {
                if (rw==READ) // 读命令
                        put_fs_byte(inb(i),buf++); // 从端口i读入一个字节，并拷贝到buf处，buf自增1 
                else
                        outb(get_fs_byte(buf++),i); // 从buf处拷贝一个字节到端口i, buf自增1
                i++; // 前移1个端口 ？？
        }
        i -= *pos; // 计算读/写总字节数，作为返回
        *pos += i; // 调整现在读写的端口值
        return i;
}

/*
 * 内存读写接口
 *
 * rw: 读写命令
 * minor: 内存子设备号
 * buf: 用户空间缓存区
 * count: 读写字节数
 * pos: 读写操作当前指针
 *
 * 成功返回读写的字节数，失败则返回出错码
 * 
 */
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos)
{
        switch(minor) {
		case 0:
                return rw_ram(rw,buf,count,pos); // 内存盘
		case 1:
                return rw_mem(rw,buf,count,pos); // 物理内存
		case 2:
                return rw_kmem(rw,buf,count,pos); // 虚拟内存
		case 3:
                return (rw==READ)? 0 : count;	// /dev/null 
		case 4:
                return rw_port(rw,buf,count,pos); // 端口读写
		default:
                return -EIO; // 出错返回
        }
}

// 系统中设备种类数目
#define NRDEVS ((sizeof (crw_table))/(sizeof (crw_ptr)))

// 字符设备读写函数指针表
static crw_ptr crw_table[]={
        NULL,		/* nodev: 无设备 */
        rw_memory,	/* /dev/mem: 内存设备等 */
        NULL,		/* /dev/fd: 软驱 */
        NULL,		/* /dev/hd: 硬盘 */
        rw_ttyx,	/* /dev/ttyx: 串行终端 */
        rw_tty,		/* /dev/tty: 控制终端 */
        NULL,		/* /dev/lp: 打印机 */
        NULL /* unnamed pipes: 无名管道 */
};		

/*
 * 字符设备读写接口
 *
 * rw: 读写命令
 * dev: 字符设备号
 * buf: 用户空间缓存区
 * count: 读写字节数
 * pos: 读写操作当前指针
 *
 * 成功返回读写的字节数，失败则返回出错码
 * 
 */
int rw_char(int rw,int dev, char * buf, int count, off_t * pos)
{
        crw_ptr call_addr; // 字符设备读写指针

        if (MAJOR(dev)>=NRDEVS) // 如果主设备号超出系统设备种类
                return -ENODEV; // 返回 -ENODEV
        // 获得对应设备的读写函数指针
        if (!(call_addr=crw_table[MAJOR(dev)])) // 查询到的读写设备指针为空
                return -ENODEV; // 返回 -ENODEV
        return call_addr(rw,MINOR(dev),buf,count,pos); // 调用相关设备的读写指针函数
}
