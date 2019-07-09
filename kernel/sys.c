/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

/**
 * 很多系统调用的实现函数
 * 
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>

/**
 * 返回日期和时间：未实现
 * 
 */
int sys_ftime()
{
        return -ENOSYS;
}

/**
 * 
 */
int sys_break()
{
        return -ENOSYS;
}

/**
 * 进程跟踪：未实现
 */
int sys_ptrace()
{
        return -ENOSYS;
}

int sys_stty()
{
        return -ENOSYS;
}

/**
 * 改变并打印终端行设置：未实现
 */
int sys_gtty()
{
        return -ENOSYS;
}

/**
 * 修改文件名：未实现
 */
int sys_rename()
{
        return -ENOSYS;
}

int sys_prof()
{
        return -ENOSYS;
}

/**
 * 设置当前任务的实际或者有效组ID
 *
 * rgid: 实际组ID
 * egid: 有效组ID
 *
 * 成功：返回0, 失败：返回错误号
 * 
 * 
 * 如果当前进程有管理员权限，就能任意设置当前进程的组ID和有效组ID
 * 如果当前进程没有管理员权限，那么当前进程的实际组ID不变，有效组ID可以被设置为当前进程的”实际组ID“或当前进程的”保存组ID“（这个来自于可执行文件）
 * 注意：这是BSD形式的实现，没有考虑保存的gid(saved gig)
 * 这使得一个使用setgid的程序可以完全放弃其特权，对一个程序进行安全审计的时候，这是一种好的处理方式
 * 
 */
int sys_setregid(int rgid, int egid)
{
        if (rgid>0) { // 实际组ID有效
                if ((current->gid == rgid) || 
                    suser()) // 当前进程的组ID == 实际组ID参数 或 超级管理员权限
                        current->gid = rgid; // 当前进程”实际组ID“设置为”实际组ID参数“
                else
                        return(-EPERM); // 返回 -EPERM （权限错误）
        }
        if (egid>0) { // 有效组ID有效
                if ((current->gid == egid) || // 当前进程组ID == 有效组ID参数 
                    (current->egid == egid) || // 当前进程有效组ID == 有效组ID参数 
                    (current->sgid == egid) || // 当前进程的保存组ID == 有效组ID参数
                    suser()) // 超级管理员
                        current->egid = egid; // ”当前进程的有效组ID“设置为”有效组ID参数“
                else
                        return(-EPERM); // 返回 -EPERM （权限错误）
        }
        return 0;
}

/**
 * 设置当前进程的组ID
 * 
 * gid: 要设置的组ID
 *
 * 成功：返回0, 失败：返回错误号
 *
 * 如果当前进程有管理员权限，设置当前进程的实际组ID和有效组ID为gid参数
 * 如果当前进程没有管理员权限，那么当前进程的实际组ID不变，有效组ID可以被设置为当前进程的”实际组ID“或当前进程的”保存组ID“（这个来自于可执行文件）
 *
 * 注意：这和有保留gid的(saved gid)SYSV的实现方式相同
 *
 */
int sys_setgid(int gid)
{
        return(sys_setregid(gid, gid));
}

/**
 * 打开或关闭内核记账：未实现
 */
int sys_acct()
{
        return -ENOSYS;
}

/**
 * 映射任意物理内存地址：未实现
 */
int sys_phys()
{
        return -ENOSYS;
}

/**
 * 内核锁：未实现
 */
int sys_lock()
{
        return -ENOSYS;
}

int sys_mpx()
{
        return -ENOSYS;
}

/**
 * 进程资源限制：未实现
 */
int sys_ulimit()
{
        return -ENOSYS;
}

/**
 * 返回当前UNIX时间（秒）
 *
 * tloc: 用户进程的内存指针，如果不为NULL，则结果也放置在那里
 *
 * 返回：当前UNIX时间
 * 
 */
int sys_time(long * tloc)
{
        int i;

        i = CURRENT_TIME; // 获得当前UNIX时间
        if (tloc) {
                verify_area(tloc,4); // 验证tloc指向的内存页是否有足够的空间，如果不够，则分配新的内存页
                put_fs_long(i,(unsigned long *)tloc); // 复制当前UNIX时间到用户进程的内存中
        }
        return i; // 返回当前UNIX时间
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.
 */

/**
 * 设置当前任务的实际或者有效用户ID
 *
 * ruid: 实际用户ID
 * euid: 有效用户ID
 *
 * 成功：返回0, 失败：返回错误号
 * 
 * 
 * 如果当前进程有管理员权限，就能任意设置当前进程的实际用户ID和有效用户ID
 * 如果当前进程没有管理员权限，那么当前进程的实际用户ID不变或被设置成当前进程的“有效用户ID“，有效用户ID不变或被设置为进程原来的实际用户ID（启动用户ID）
 * 
 * 注意：对于没有管理员权限的进程，这个调用起得作用是让进程的实际用户ID和进程的有效用户ID进行互换
 * 
 */
int sys_setreuid(int ruid, int euid)
{
        int old_ruid = current->uid; // 临时保存当前进程的有效用户ID（启动进程的用户ID）
	
        if (ruid>0) { // ruid参数有效
                if ((current->euid==ruid) || // 当前进程的有效用户ID == ruid参数
                    (old_ruid == ruid) || // 启动进程的用户ID == ruid参数
                    suser()) // 超级管理员
                        current->uid = ruid; // 设置当前进程的实际用户ID为ruid参数
                else
                        return(-EPERM); // 返回 -EPERM （权限错误） 
        }
        if (euid>0) { // euid参数有效
                if ((old_ruid == euid) || // 启动进程的用户ID == euid参数
                    (current->euid == euid) || // 当前进程的有效用户ID == euid参数
                    suser()) // 超级管理员 
                        current->euid = euid; // 设置当前进程的有效用户ID为euid参数
                else {
                        current->uid = old_ruid; // 恢复当前进程的实际用户ID为启动进程的用户ID
                        return(-EPERM); // 返回 -EPERM （权限错误）
                }
        }
        return 0; // 返回0：表示执行成功
}


/**
 * 设置当前进程的用户ID
 * 
 * uid: 要设置的用户ID
 *
 * 成功：返回0, 失败：返回错误号
 *
 * 如果当前进程有管理员权限，设置当前进程的实际用户ID和有效用户ID为uid参数
 * 如果当前进程没有管理员权限，只有uid为当前进程的实际用户ID或有效用户ID的时候，设置当前进程的实际用户ID和有效用户ID为uid参数
 *
 */
int sys_setuid(int uid)
{
        // 注意：这个实现有问题，后来版本修正了
        return(sys_setreuid(uid, uid));
}

int sys_stime(long * tptr)
{
        if (!suser())
                return -EPERM;
        startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
        return 0;
}

int sys_times(struct tms * tbuf)
{
        if (tbuf) {
                verify_area(tbuf,sizeof *tbuf);
                put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
                put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
                put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
                put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
        }
        return jiffies;
}

int sys_brk(unsigned long end_data_seg)
{
        if (end_data_seg >= current->end_code &&
            end_data_seg < current->start_stack - 16384)
                current->brk = end_data_seg;
        return current->brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 */
int sys_setpgid(int pid, int pgid)
{
        int i;

        if (!pid)
                pid = current->pid;
        if (!pgid)
                pgid = current->pid;
        for (i=0 ; i<NR_TASKS ; i++)
                if (task[i] && task[i]->pid==pid) {
                        if (task[i]->leader)
                                return -EPERM;
                        if (task[i]->session != current->session)
                                return -EPERM;
                        task[i]->pgrp = pgid;
                        return 0;
                }
        return -ESRCH;
}

int sys_getpgrp(void)
{
        return current->pgrp;
}

int sys_setsid(void)
{
        if (current->leader && !suser())
                return -EPERM;
        current->leader = 1;
        current->session = current->pgrp = current->pid;
        current->tty = -1;
        return current->pgrp;
}

int sys_uname(struct utsname * name)
{
        static struct utsname thisname = {
                "linux .0","nodename","release ","version ","machine "
        };
        int i;

        if (!name) return -ERROR;
        verify_area(name,sizeof *name);
        for(i=0;i<sizeof *name;i++)
                put_fs_byte(((char *) &thisname)[i],i+(char *) name);
        return 0;
}

int sys_umask(int mask)
{
        int old = current->umask;

        current->umask = mask & 0777;
        return (old);
}
