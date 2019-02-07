#ifndef _FCNTL_H
#define _FCNTL_H

/**
 * 文件控制头文件：定义了fcntl函数，文件创建或打开中用到的一些选项
 */
#include <sys/types.h>

/* open/fcntl - NOCTTY, NDELAY isn't implemented yet */
#define O_ACCMODE	00003 // 文件访问模式屏蔽码
// open和fcntl使用的文件访问模式：同时只能使用其中之一
#define O_RDONLY	   00 // 只读
#define O_WRONLY	   01 // 只写
#define O_RDWR		   02 // 读写
// 下面是文件创建或操作标志，用于open，可于上面的访问模式用'|'方式一起使用
#define O_CREAT		00100	/* not fcntl */ // 如果不存在，则创建
#define O_EXCL		00200	/* not fcntl */ // 独占使用
#define O_NOCTTY	00400	/* not fcntl */ // 不分配控制中断
#define O_TRUNC		01000	/* not fcntl */ // 若文件已经存在，并且是写操作，则长度截断为0
#define O_APPEND	02000 // 以添加方式打开，文件指针置为文件末尾
#define O_NONBLOCK	04000	/* not fcntl */ // 以非阻塞方式打开和操作文件
#define O_NDELAY	O_NONBLOCK

/* Defines for fcntl-commands. Note that currently
 * locking isn't supported, and other things aren't really
 * tested.
 */
/*
 * 文件描述符操作函数fcntl中的命令(cmd)
 */
#define F_DUPFD		0	/* dup */ // 拷贝文件句柄为最小数值的句柄
#define F_GETFD		1	/* get f_flags */ // 获取文件描述符
#define F_SETFD		2	/* set f_flags */ // 设置文件描述符
#define F_GETFL		3	/* more flags (cloexec) */ // 取文件状态标志和访问模式
#define F_SETFL		4 // 设置文件状态标志和访问模式
#define F_GETLK		5	/* not implemented */ // 返回锁定文件的flock结构
#define F_SETLK		6 // 设置[F_RDLCK或F_WRLCK]或清除(F_UNLCK)锁定
#define F_SETLKW	7 // 等待设置或清除锁定

/* for F_[GET|SET]FL */
/*
 * 执行 exec 时所需要关闭的文件描述符（句柄）： close on exec
 * 用于F_GETFL和F_SETFL
 * 
 */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* Ok, these are locking features, and aren't implemented at any
 * level. POSIX wants them.
 */
// 文件锁类型
#define F_RDLCK		0 // 读锁
#define F_WRLCK		1 // 写锁
#define F_UNLCK		2 // 解锁

/* Once again - not implemented, but ... */
// 文件锁结构
struct flock {
        short l_type; // 锁类型：F_RDLCK, F_WRLCK 或 F_UNLCK 
        short l_whence; // 文件指针偏移：SEEK_SET, SEEK_CUR 或 SEEK_END
        off_t l_start; // 阻塞锁定的开始处：相对偏移（字节数）
        off_t l_len; // 阻塞锁定的长度：字节数，如果是0则到文件末尾
        pid_t l_pid; // 加锁的进程id
};

extern int creat(const char * filename,mode_t mode);
extern int fcntl(int fildes,int cmd, ...);
extern int open(const char * filename, int flags, ...);

#endif
