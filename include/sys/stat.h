#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

struct stat {
        dev_t	st_dev; // 普通文件的设备号
        ino_t	st_ino; // 文件的“i节点”号
        umode_t	st_mode; // 文件类型和属性
        nlink_t	st_nlink; //文件的硬连接数
        uid_t	st_uid; // 文件的用户ID 
        gid_t	st_gid; // 文件的组ID
        dev_t	st_rdev; // 设备号（特殊的字符文件或块文件）
        off_t	st_size; // 文件大小（字节数）（如果文件是常规文件的话）
        time_t	st_atime; // 上次（最后）访问时间
        time_t	st_mtime; // 最后修改时间
        time_t	st_ctime; // 最后节点修改时间
};

//下面是为st_mode字段所用的值定义的符号名称，这些值均用八进制表示
// st_mode 由三部分组成：文件类型 | 文件属性 | 文件访问权限

// 文件类型
#define S_IFMT  00170000 // 文件类型位屏蔽码
#define S_IFREG  0100000 // 常规文件
#define S_IFBLK  0060000 // 块文件，如 /dev/fd0 等
#define S_IFDIR  0040000 // 目录文件
#define S_IFCHR  0020000 // 字符设备文件
#define S_IFIFO  0010000 // 管道特殊文件

//文件属性位
// S_ISUID 如果被设置，那么当执行该文件时，“进程的有效用户ID”被设置为“该文件的宿主用户ID”　
// S_ISGID 处理和 S_ISUID 类似
#define S_ISUID  0004000 // 执行时设置用户ID(set-user-ID) 
#define S_ISGID  0002000 // 执行时设置组ID(set-group-ID) 
#define S_ISVTX  0001000 // 对于目录，受限删除标志

#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG) // 测试是否是“常规”文件
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR) // 测试是否是“目录”文件
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR) // 测试是否是“字符设备”文件
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK) // 测试是否是“块设备”文件
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO) // 测试是否是“管道”特殊文件

// 文件访问权限
#define S_IRWXU 00700 // 宿主有　读，写，执行／搜索　权限
#define S_IRUSR 00400 // 宿主有　读 权限
#define S_IWUSR 00200 // 宿主有　写 权限
#define S_IXUSR 00100 // 宿主有　执行／搜索　权限

#define S_IRWXG 00070 // 组成员有　读，写，执行／搜索　权限
#define S_IRGRP 00040 // 组成员有　读 权限
#define S_IWGRP 00020 // 组成员有　写 权
#define S_IXGRP 00010 // 组成员有　执行／搜索　权限

#define S_IRWXO 00007 // 其他人有　读，写，执行／搜索　权限
#define S_IROTH 00004 // 其他人有　读 权限
#define S_IWOTH 00002 // 其他人有　写 权
#define S_IXOTH 00001 // 其他人有　执行／搜索　权限
        
extern int chmod(const char *_path, mode_t mode);
extern int fstat(int fildes, struct stat *stat_buf);
extern int mkdir(const char *_path, mode_t mode);
extern int mkfifo(const char *_path, mode_t mode);
extern int stat(const char *filename, struct stat *stat_buf);
extern mode_t umask(mode_t mask);

#endif
