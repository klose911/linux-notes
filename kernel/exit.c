/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h> // 错误头文件
#include <signal.h> // 信号头文件 
#include <sys/wait.h> // 等待进程终止头文件，定义了系统调用 wait() 和 waitpid() 以及相关常数符号

#include <linux/sched.h> // 进程调度头文件
#include <linux/kernel.h> // 内核头文件，定义了常用函数的原型
#include <linux/tty.h> // 终端头文件，定义了有关 tty_io 有关的参数
#include <asm/segment.h> // 段操作头文件

// 系统调用：把进程置为睡眠状态，直到进程收到信号（在 kernel/sched.c 中实现）
int sys_pause(void);
// 系统调用：关闭指定文件（在 fs/open.c 中实现）
int sys_close(int fd);

/**
 * 释放指定进程占用的任务槽及其数据结构占用的内存页面，该函数在后面的 sys_kill 和 sys_waitpid 中被调用
 *
 * p: 任务数据结构指针
 *
 * 无返回值
 *
 */
void release(struct task_struct * p)
{
        int i;

        // 如果任务结构指针 p 为空，直接退出
        if (!p)
                return;

        //扫描任务指针数组 task[] 以寻找任务 *p ：
        for (i=1 ; i<NR_TASKS ; i++)
                if (task[i]==p) {
                        task[i]=NULL; // 清空该任务槽
                        free_page((long)p); // 释放该任务数据结构所占的内存页面
                        schedule(); // 执行调度函数并在返回后立即退出
                        return;
                }
        panic("trying to release non-existent task"); // 没找到对应的任务：内核panic退出
}

// 向指定任务 p 发送特定信号 sig, 权限为 priv
// sig: 信号数字
// p: 任务结构指针
// priv: 强制发送信号标志
// 权限足够则发送信号并且返回0，否则返回错误号 -EINVAL 或 -EPERM 
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
        if (!p || sig<1 || sig>32) // 结构任务指针 p 为空，或者信号数字 sig 非法
                return -EINVAL; // 返回错误号 -EINVAL
        // 以下三种情况可以发送信号：
        // 1. 带有强制发送标志
        // 2. 当前进程的有效用户ID == 进程 p 的有效用户ID
        // 3. 当前进程的有效用户是root（suser() 等级 current->euid == 0）
        if (priv || (current->euid==p->euid) || suser())
                p->signal |= (1<<(sig-1)); // p 进程结构中的 signal域里 sig 对应位置 1 
        else
                return -EPERM; // 没有权限发送，返回错误号 -EPERM 
        return 0;
}

// 挂断当前会话(session) 
static void kill_session(void)
{
        struct task_struct **p = NR_TASKS + task; // p 指向任务结构数组的最后一项

        // 从末尾开始扫描整个任务结构数组
        while (--p > &FIRST_TASK) {
                // 找到所有进程，其“会话号”就是当前进程的“会话号”
                if (*p && (*p)->session == current->session)
                        (*p)->signal |= 1<<(SIGHUP-1); // 当前会话中的进程的信号位图里的 SIGHUP 位置 1，默认动作是终止该进程
        }
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */

/**
 * 系统调用 sys_kill ：向进程号为 pid 的进程发送信号 sig
 *
 * pid: 进程号
 * sig: 信号数字
 *
 * 成功返回0，失败返回相应错误
 */
int sys_kill(int pid,int sig)
{
        struct task_struct **p = NR_TASKS + task; //  p 指向任务结构数组的最后一项
        int err, retval = 0;

        // pid == 0：强制发送给当前进程组里所有的进程信号
        if (!pid) {        
                while (--p > &FIRST_TASK) {
                        // 所有进程的进程组号 == 当前进程标识(current->pid)
                        if (*p && (*p)->pgrp == current->pid) {
                                // 强制向当前进程组里所有进程发送信号 sig 
                                if ((err=send_sig(sig,*p,1))) {        
                                        retval = err;
                                }
                        }
                        
                }
        } else if (pid > 0) { // pid > 0：向特定 pid 的进程发送信号
                while (--p > &FIRST_TASK) {
                        // 寻找进程号为 pid 的进程
                        if (*p && (*p)->pid == pid) {
                                // 注意：这里是非强制发送，需要校验权限
                                if ((err=send_sig(sig,*p,0))) {
                                        retval = err;
                                }
                        }
                }
        } else if (pid == -1) { // pid == -1：给除了第一个进程（调度进程）的所有进程发送信号
                while (--p > &FIRST_TASK) {
                        // 这里依旧需要权限校验
                        if ((err = send_sig(sig,*p,0))) {
                                retval = err;
                        }
                } 
        } else { // pid < -1：向“进程组号”为 -pid 的所有进程发送信号
                while (--p > &FIRST_TASK) {
                        if (*p && (*p)->pgrp == -pid) {
                                // 这里还是需要校验权限
                                if ((err = send_sig(sig,*p,0))) {
                                        retval = err;
                                }
                        }
                }
        }
        
        return retval;
}

static void tell_father(int pid)
{
        int i;

        if (pid)
                for (i=0;i<NR_TASKS;i++) {
                        if (!task[i])
                                continue;
                        if (task[i]->pid != pid)
                                continue;
                        task[i]->signal |= (1<<(SIGCHLD-1));
                        return;
                }
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
        printk("BAD BAD - no father found\n\r");
        release(current);
}

int do_exit(long code)
{
        int i;
        free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
        free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
        for (i=0 ; i<NR_TASKS ; i++) {
                if (task[i] && task[i]->father == current->pid) {
                        task[i]->father = 1;
                        if (task[i]->state == TASK_ZOMBIE)
                                /* assumption task[1] is always init */
                                (void) send_sig(SIGCHLD, task[1], 1);
                }
        }
        for (i=0 ; i<NR_OPEN ; i++) {
                if (current->filp[i])
                        sys_close(i);
        }
        iput(current->pwd);
        current->pwd=NULL;
        iput(current->root);
        current->root=NULL;
        iput(current->executable);
        current->executable=NULL;
        if (current->leader && current->tty >= 0)
                tty_table[current->tty].pgrp = 0;
        if (last_task_used_math == current)
                last_task_used_math = NULL;
        if (current->leader)
                kill_session();
        current->state = TASK_ZOMBIE;
        current->exit_code = code;
        tell_father(current->father);
        schedule();
        return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
        return do_exit((error_code&0xff)<<8);
}

int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
        int flag, code;
        struct task_struct ** p;

        verify_area(stat_addr,4);
repeat:
        flag=0;
        for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
                if (!*p || *p == current)
                        continue;
                if ((*p)->father != current->pid)
                        continue;
                if (pid>0) {
                        if ((*p)->pid != pid)
                                continue;
                } else if (!pid) {
                        if ((*p)->pgrp != current->pgrp)
                                continue;
                } else if (pid != -1) {
                        if ((*p)->pgrp != -pid)
                                continue;
                }
                switch ((*p)->state) {
                case TASK_STOPPED:
                        if (!(options & WUNTRACED))
                                continue;
                        put_fs_long(0x7f,stat_addr);
                        return (*p)->pid;
                case TASK_ZOMBIE:
                        current->cutime += (*p)->utime;
                        current->cstime += (*p)->stime;
                        flag = (*p)->pid;
                        code = (*p)->exit_code;
                        release(*p);
                        put_fs_long(code,stat_addr);
                        return flag;
                default:
                        flag=1;
                        continue;
                }
        }
        if (flag) {
                if (options & WNOHANG)
                        return 0;
                current->state=TASK_INTERRUPTIBLE;
                schedule();
                if (!(current->signal &= ~(1<<(SIGCHLD-1))))
                        goto repeat;
                else
                        return -EINTR;
        }
        return -ECHILD;
}


