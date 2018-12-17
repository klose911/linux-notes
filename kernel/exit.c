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

// 子进程终止时通知父进程
// pid : 子进程标识符
// 无返回值
// 如果能找到父进程，则发送 SIGCHLD 信号给父进程
// 如果找不到父进程，则释放自己占用的任务槽和页面
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


/**
 * 程序退出处理函数，被下面的 sys_exit 使用
 *
 * code: 返回状态码
 *
 * 返回值：-1 
 */
// 这段代码无论程序是正确退出或者异常退出都会被调用
int do_exit(long code)
{
        int i;
        // 释放当前进程代码段和数据段所占的内存页面
        // get_limit 从段选择子指定的段描述符中获取对应的段限制长度
        free_page_tables(get_base(current->ldt[1]),get_limit(0x0f)); // current->ldt[1] 进程的代码段基地址, 0x0f:  代码段选择子
        free_page_tables(get_base(current->ldt[2]),get_limit(0x17)); // current->ldt[2] 进程的数据段基地址， 0x17: 数据段选择子

// 遍历进程结构指针数组
        for (i=0 ; i<NR_TASKS ; i++) {
                // 找到当前进程的子进程
                if (task[i] && task[i]->father == current->pid) {
                        task[i]->father = 1; // 子进程的父进程设为 1 (init 进程)
                        // 如果子进程的状态已经是僵死状态，则强制向 init 进程发送 SIGCHLD，来清理僵死进程 
                        if (task[i]->state == TASK_ZOMBIE) 
                                /* assumption task[1] is always init */
                                (void) send_sig(SIGCHLD, task[1], 1);
                }
        }
        // 关闭当前进程打开的所有文件
        for (i=0 ; i<NR_OPEN ; i++) {
                if (current->filp[i])
                        sys_close(i);
        }
        
        // 当前进程的工作目录 pwd, 根目录 root, 以及可执行文件 executable 做 inode 的同步操作，并把这些对应的指针置空
        iput(current->pwd);
        current->pwd=NULL;
        iput(current->root);
        current->root=NULL;
        iput(current->executable);
        current->executable=NULL;

        // 如果当前进程是会话的首进程，而且当前进程打开了终端
        if (current->leader && current->tty >= 0)
                tty_table[current->tty].pgrp = 0; // 清空终端结构数组中对应当前进程项的所属进程组字段 

// 如果当前进程使用了数学协处理器，则清空 last_task_used_math 指针
        if (last_task_used_math == current)
                last_task_used_math = NULL;
        
        // 如果当前进程是否个会话的首进程，则挂断会话的所有进程
        if (current->leader)
                kill_session();

        // 当前进程状态设置为僵尸状态
        current->state = TASK_ZOMBIE; // 一个已经终止，但是其父进程尚未对其进行善后处理(获取终止子进程的有关信息)的进程被称为僵尸进程!!! 
        current->exit_code = code; // 设置当前进程返回码
        tell_father(current->father); // 发送 SIGCHLD 给父进程
        schedule(); // 执行调度函数
        return (-1);	/* just to suppress warnings */
}

/**
 * 系统调用 _exit, 它和ANSI 标准库函数不一样
 * 标准库函数的 exit() 函数还会做一些类似关闭标准I/O流的清理工作，也可以通过 atexit() 来自定义清理工作
 *
 * error_code: 用户程序退出的状态信息，只低8位有效
 */
int sys_exit(int error_code)
{
        // 把 error_code 左移8位是 wait 系列函数的要求
        // 例如：进程处于 TASK_STOP 状态，那么低8位就是 0x7f, 这样WIFSTOPPED等宏就可以得到进程退出状态等信息
        return do_exit((error_code&0xff)<<8);
}

/**
 * 系统调用 waitpid: 挂起当前进程，直到pid指定的子进程退出（终止）或者收到本进程终止的信号，或者调用一个信号句柄
 *
 * pid: 进程号
 * stat_addr: 保存子进程退出状态信息的指针
 * options: waitpid的选项
 *
 * 返回值：
 * 如果 pid > 0 : 等待进程号等于pid的子进程
 * 如果 pid == 0 : 等待当前进程组号等于当前进程组号的任何子进程
 * 如果 pid == -1: 等待任何子进程
 * 如果 pid < -1: 等待进程组号等于 pid 绝对值的任何子进程
 *
 * 如果 stat_addr 不为空，则保存子进程退出状态信息
 *
 * 如果 options == WUNTRACED: 如果子进程已经停止，则马上返回
 * 如果 options == WNOHANG: 如果没有子进程处于退出或终止，则马上返回（非阻塞的wait调用）
 */
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
        int flag, code; // flag 用于表示后面选出的子进程状态
        struct task_struct ** p;

        // 验证将要用来保存子进程退出状态信息的空间是否足够
        verify_area(stat_addr,4);
repeat:
        flag=0; // 复位 flag
        // 扫描进程结构数组
        for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
                if (!*p || *p == current) // 进程槽为空，或进程结构指向当前进程
                        continue;
                if ((*p)->father != current->pid) // 不是当前进程的子进程
                        continue;
                
                //执行到这里已经找到一个当前进程的子进程
                if (pid>0) { // pid>0: 等待进程号等于pid的子进程
                        if ((*p)->pid != pid) // 不是要等待的那个子进程
                                continue;
                } else if (!pid) { // pid == 0: 等待当前进程组号等于当前进程组号的任何子进程
                        if ((*p)->pgrp != current->pgrp) // “子进程的进程组号”不等于“当前进程的进程组号” 
                                continue;
                } else if (pid != -1) { // pid < -1: 等待进程组号等于 pid 绝对值的任何子进程
                        if ((*p)->pgrp != -pid) // “子进程的进程组号”不等于“pid 的绝对值”
                                continue;
                } // pid == -1 : 等待任何子进程
                
                switch ((*p)->state) {
                case TASK_STOPPED: // 子进程处于停止状态
                        // 如果 WUNTRACED 位没有被置位
                        if (!(options & WUNTRACED))
                                continue; // 继续扫描处理其他子进程
                        put_fs_long(0x7f,stat_addr);//退出状态码设置为 0x7f，写回退出状态信息
                        return (*p)->pid; // 返回子进程的 pid 
                case TASK_ZOMBIE: // 子进程处于僵死状态
                        // 把子进程的用户态运行时间 utime, 内核态运行时间 stime 累加到当前进程的 cutime 和 cstime 上
                        current->cutime += (*p)->utime; 
                        current->cstime += (*p)->stime;
                        flag = (*p)->pid; // flag 置为子进程 pid 
                        code = (*p)->exit_code; // 取出子进程的退出状态码
                        release(*p); // 释放子进程的任务槽和任务结构占用的内存空间
                        put_fs_long(code,stat_addr); //回写退出状态信息到 stat_addr
                        return flag; // 返回子进程id，退出
                default: // 子进程处于运行态，或睡眠态
                        flag=1; // 置flag标志位，继续扫描下一个进程
                        continue;
                }
        }
        //进程数组扫描完毕，flag == 1: 找到一个子进程，但它仍然处于运行或睡眠态 
        if (flag) { 
                if (options & WNOHANG) // 如果 WNOHANG 被置位
                        return 0; // 直接返回 0 
                current->state=TASK_INTERRUPTIBLE; // 当前进程状态置为可信号中断的等待状态
                // 注意：这里应该先设置当前进程允许收到 SIGCHLD 信号！！！ 
                schedule(); // 执行调度程序
                if (!(current->signal &= ~(1<<(SIGCHLD-1)))) //收到一个 SIGCHLD 信号，则 SIGCHLD 位被复位为0
                        goto repeat; // 重新执行 repeat 段代码
                else // 注意：收到的信号不是 SIGCHLD，应该“重新启动本waitpid系统调用”，而不是草率地以 -EINTR 返回
                        return -EINTR; 
        }
        // 扫描完整个进程数组，flag 仍为 0 : 没有找到对应的子进程
        return -ECHILD; //  -ECHLD 错误码返回（子进程不存在）
}


