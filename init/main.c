/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

//定义 __LIBRARY__ 是为了包括定义在 unistd.h 中的内嵌汇编代码等信息
#define __LIBRARY__
#include <unistd.h> // 标准符号常数与类型文件，声明了各种系统调用等函数，如果声明了 __LIBRARY__ 则还包含系统调用号和内嵌汇编代码
#include <time.h> // 时间类型头文件，包含了 tm 结构，和一些有关时间的函数

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */

/* 我们需要下面这些内联语句，因为从内核空间创建进程0时候无法使用写时复制(copy on write) !!!
 * 直到执行一个 execve 调用。这对栈调用来说会有问题，处理方式是在 fork() 被调用后，main 程序就不能再
 * 使用栈，这意味着 fork 也必须使用内联函数，否则在 fork() 退出后就必须使用堆栈了
 *
 * 实际上只有 pause 和 fork 需要内联，使得 main() 函数不会弄乱堆栈，但我们还是定义了其他一些内联函数
 */

// Linux 在内核空间创建进程时候不使用 copy on write 。main() 在移动到用户模式 （任务0）后执行内联的 fork 和 pause，
// 因此可以保证不使用任务0的用户栈。在执行 move_to_user_mode()之后，main() 就以任务0的身份运行了。而任务0
// 是所有创建子进程的父进程。当它创建第一个子进程时(init进程)，由于任务1代码还是属于内核空间，因此没有写时复制技术。
// 此时任务0的用户栈就是任务1的用户栈，即他们共用一个栈空间，因此希望 main() 在运行任务0时候不要有任何对栈的操作。
// 而当再次执行 fork() 并执行过 execve() 后，被加载的程序已经不属于内核空间，因此可以使用写时复制了。

// _syscall0 以嵌入汇编的形式调用系统中断 80， 最后的0表示没有参数
static inline _syscall0(int,fork) // int fork() ：创建子进程
        static inline _syscall0(int,pause) // int pause() : 暂停进程的执行，直到收到一个信号
        static inline _syscall1(int,setup,void *,BIOS) // int setup(void* BIOS) ：系统设置，仅在这个文件使用
        static inline _syscall0(int,sync) // int sync() ：同步文件系统

#include <linux/tty.h> // tty 头文件，定义了 tty_io，串行通信方面的参数，常数
#include <linux/sched.h> // 调度程序头文件，定义了 task_struct，任务0的数据，描述符参数设置等
#include <linux/head.h> // head 头文件，定义了段描述符的简单结构，和几个选择子的常量等
#include <asm/system.h> // 汇编系统头文件，以宏的形式定义了有关设置或修改描述符/中断门等的嵌入汇编代码
#include <asm/io.h> // 汇编io头文件，以宏的形式定义了对 io 端口的嵌入汇编代码

#include <stddef.h> // 标准头文件，定义了 NULL, offsetof(TYPE, MEMBER)等
#include <stdarg.h> // 标准参数头文件，定义了可变参数列表
#include <unistd.h>
#include <fcntl.h> // 文件控制头文件，定义了文件及其描述符的操作控制常数符号和操作
#include <sys/types.h> // 类型头文件，linux系统的基本数据类型

#include <linux/fs.h> // 文件系统头文件，定义文件表结构 (file, buffer_head, m_inode) 和 extern int ROOT_DEV 等

        static char printbuf[1024]; // 内核显示信息的缓存

extern int vsprintf(); // 格式化输出到一个字符串
extern void init(void); // 初始化
extern void blk_dev_init(void); // 块控制设备初始化
extern void chr_dev_init(void); // 字符设备初始化
extern void hd_init(void); // 硬盘设备初始化
extern void floppy_init(void); // 软盘设备初始化
extern void mem_init(long start, long end); // 内存初始化
extern long rd_init(long mem_start, int length); //虚拟内存盘初始化
extern long kernel_mktime(struct tm * tm); // 计算计算机开机启动时间
extern long startup_time; // 开机启动时间，以 ms 为单位

/*
 * This is set up by the setup-routine at boot-time
 */
/*
 * 以下这三行是由 setup.s 在引导期间设置的
 */

// 下面三行强制转换线性地址为物理地址，并获取地址里的内容，因为内核代码段被映射到物理地址 0x00000处，因此实际上这些线性地址正好也对应于物理地址
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

// 这段代码是读取 COMS 时钟信息
// outb_p 和 inb_p 是定义在 include/asm/io.h 中的端口输入输出宏
// 0x70 是写地址端口，0x80|addr 是要读取的 CMOS 的内容
// 0x71 是读数据端口
#define CMOS_READ(addr) ({                      \
                        outb_p(0x80|addr,0x70); \
                        inb_p(0x71);            \
                })
                
// 将 BCD 码转换为十六进制数值
// BCD 码是用半个字节 （4位） 表示一个10进制数，因此一个字节表示2个10进制数
// (val) >> 4 取 BCD 表示的10进制的十位数，所以需要再乘以10
// 最后两者相加就是一个字节的 BCD 码表示的十六进制数
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

//
static void time_init(void)
{
        struct tm time;

        do {
                time.tm_sec = CMOS_READ(0);
                time.tm_min = CMOS_READ(2);
                time.tm_hour = CMOS_READ(4);
                time.tm_mday = CMOS_READ(7);
                time.tm_mon = CMOS_READ(8);
                time.tm_year = CMOS_READ(9);
        } while (time.tm_sec != CMOS_READ(0));
        BCD_TO_BIN(time.tm_sec);
        BCD_TO_BIN(time.tm_min);
        BCD_TO_BIN(time.tm_hour);
        BCD_TO_BIN(time.tm_mday);
        BCD_TO_BIN(time.tm_mon);
        BCD_TO_BIN(time.tm_year);
        time.tm_mon--;
        startup_time = kernel_mktime(&time);
}

// 下面定义一些局部变量
static long memory_end = 0; // 机器具有的物理内存容量（字节数）
static long buffer_memory_end = 0; // 高速缓存末端地址
static long main_memory_start = 0; // 主内存的开始处，将用于分页开始的位置

struct drive_info { char dummy[32]; } drive_info; // 用于存放硬盘参数表信息

// 内核初始化主程序，初始化结束后以任务0（调度程序）在用户态运行
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
        /* 此时中断仍旧被禁止，做完必要的初始化后开启 */
        // 
        ROOT_DEV = ORIG_ROOT_DEV; // ROOT_DEV 定义在 fs/super.c 
        drive_info = DRIVE_INFO; // 复制内存 0x90080 处的硬盘信息

// 根据机器物理内存容量设置高速缓存区和主内存区的位置和范围
// 高速缓存区末端地址 -> buffer_memory_end
// 机器内存容量 -> memory_end
// 主内存开始地址 -> main_memory_start 
        memory_end = (1<<20) + (EXT_MEM_K<<10); // 内存大小 = 1M + (扩展内存K) * 1024 字节 
        memory_end &= 0xfffff000; // 忽略不到 4KB (1页) 的内存
        if (memory_end > 16*1024*1024)
                memory_end = 16*1024*1024; // 如果内存大于 16MB，则按照 16MB 计
        if (memory_end > 12*1024*1024) 
                buffer_memory_end = 4*1024*1024; // 如果内存 大于 12 MB，高速缓存区的结束地址是在 4MB
        else if (memory_end > 6*1024*1024)
                buffer_memory_end = 2*1024*1024; // 如果内存 大于 6MB，高速缓存区的末端 = 2MB 
        else
                buffer_memory_end = 1*1024*1024; // 其他情况，高速缓存区的末端 = 1MB 
        main_memory_start = buffer_memory_end; // 主内存的开始 = 高速缓存区的末端 
#ifdef RAMDISK
        main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
        // 以下是内核进行的初始化工作
        mem_init(main_memory_start,memory_end); // 主内存初始化
        trap_init(); // 陷阱门 （硬件中断） 初始化
        blk_dev_init(); // 块设备初始化
        chr_dev_init(); // 字符设备初始化
        tty_init(); // tty 初始化
        time_init(); // 启动时间初始化
        sched_init(); // 调度程序初始化，加载任务0 的 tr, ldtr 
        buffer_init(buffer_memory_end); // 缓存区管理初始化，内存链表等
        hd_init(); // 硬盘初始化
        floppy_init(); // 软盘初始化
        sti(); // 开启中断
        // 通过在栈中设置的参数，利用中断返回指令 iret 启动任务0
        move_to_user_mode(); // 跳跃到用户模式
        // fork 一个新的进程1 (init进程)
        if (!fork()) {		/* we count on this going ok */ 
                init(); // 在进程1 里面执行 init() 
        }
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
        // 这里的代码以进程0执行 !!! 
        /* 注意：对于任何其他的进程，'pause()'意味着必须等待收到一个信号才会返回就绪状态，
         * 但任务0是唯一的例外情况，因为任务0在任何空闲时间都会被激活，所以对任务0来说
         * 'pause()'意味着我们返回来看是否有其他任务可以运行，如果没有我们就回到这里，
         * 一直循环执行'pause()'
         * 
         */
        
        // pause 系统调用会把任务0转换成可中断状态，再执行调度函数，
        // 但是调度函数只要发现没有其他可执行任务，就会切换到任务0，而不依赖于任务0的状态
        for(;;) pause();
}


//使用 vsprintf 把格式化的字符串放入打印缓存 printbuf ，然后用 write 输出到 1: stdout
static int printf(const char *fmt, ...)
{
        va_list args;
        int i;

        va_start(args, fmt);
        write(1,printbuf,i=vsprintf(printbuf, fmt, args));
        va_end(args);
        return i;
}

// 读取并执行 /etc/rc 文件时所使用的命令行和环境参数
static char * argv_rc[] = { "/bin/sh", NULL }; 
static char * envp_rc[] = { "HOME=/", NULL };

// 运行登录 shell 时候所使用的命令行和环境参数
// argv[0] 中 '-' 特殊字符用来告诉 shell 这个是登录shell, 这和在 shell 环境执行 sh 的执行逻辑不一样
static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

// init 函数运行在 任务0 创建的子进程（任务1）中。
// 首先对第一个要执行的程序（shell）的环境进行初始化
// 然后以登录shell的方式加载该程序并执行它 
void init(void)
{
        int pid,i;

        // setup 是一个系统调用，读取硬盘参数表，分区表，虚拟盘（如果存在），安装根文件系统设备
        setup((void *) &drive_info); // 位于文件 /kernel/blk_drv/hd.c:int sys_setup(void * BIOS)
        // 以读写方式打开设备/dev/tty0，对应于终端控制台
        // 因为是第一次打开文件操作，所以文件描述符肯定是0,对应于 stdin 标准输入
        // "(void)"前缀：表示强制函数无须返回值
        (void) open("/dev/tty0",O_RDWR,0); // stdin 标准输入 
        (void) dup(0); // 复制 文件描述符0 到 文件描述符1：stdout 标准输出
        (void) dup(0); // 复制 文件描述符0 到 文件描述符2：stderr 标准错误输出
        printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
               NR_BUFFERS*BLOCK_SIZE); // 打印高速缓存区大小
        printf("Free mem: %d bytes\n\r",memory_end-main_memory_start); // 打印主内存大小

        // 下面 fork 调用用来创建子进程2, 子进程2的返回值是 0 ,而父进程则返回2，
        if (!(pid=fork())) { // 这里是子进程2在执行
                close(0); // 立刻关闭复制得来的文件描述符0
                if (open("/etc/rc",O_RDONLY,0)) //  重定位到文件描述符0 -> /etc/rc 文件
                        _exit(1); // 如果打开 /etc/rc 失败，则以出错码 -1 退出
                execve("/bin/sh",argv_rc,envp_rc); // 执行 /etc/rc sh脚本
                _exit(2); // 如果执行失败则返回错误码 -2 
        }
        if (pid>0) // 进程1 继承执行
                while (pid != wait(&i)) // 等待直到子进程2执行完毕，如果 wait 返回值不等于“子进程号”，则继续等待
                        /* nothing */;

        // 如果执行到子进程2结束，/etc/rc脚本已经执行成功
        while (1) {
                // 创建一个新的子进程，如果创建失败，则重试
                if ((pid=fork())<0) {
                        printf("Fork failed in init\r\n");
                        continue;
                }
                // 下面是新创建的子进程：
                if (!pid) {
                        close(0);close(1);close(2); // 关闭fork复制的文件描述符
                        setsid(); // 新创建一个会话，并设置进程组号
                        (void) open("/dev/tty0",O_RDWR,0); // 重新定位 文件描述符0 -> 终端控制台
                        (void) dup(0); // 复制 文件描述符0 到 文件描述符1：stdout 标准输出
                        (void) dup(0); // 复制 文件描述符0 到 文件描述符2：stderr 标准错误输出
                        _exit(execve("/bin/sh",argv,envp)); // 执行登录shell ..... 最终返回退出的状态码
                }

                // 进程1 继承执行
                while (1)
                        if (pid == wait(&i)) // 等待直到 登录shell的子进程 终止
                                break;
                printf("\n\rchild %d died with code %04x\n\r",pid,i);
                sync(); // 高速缓存区和文件系统同步
        }
        // 退出 init 子进程，实际上这是关机时的操作
        _exit(0);	/* NOTE! _exit, not exit() */
        /* 
         * _exit 和 exit 的区别：
         * _exit : 系统调用
         * exit : 标准库函数，在执行 _exit 之前会进行一系列的清理工作，比如执行各终止处理程序，关闭标准IO等 
         */
}
