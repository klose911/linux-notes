/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl.
 */

/**
 * 终端读写接口
 * 
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#define ALRMMASK (1<<(SIGALRM-1)) // Alarm信号在信号位图中对应的位屏蔽位
#define KILLMASK (1<<(SIGKILL-1)) // Kill信号在信号位图中对应的位屏蔽位
#define INTMASK (1<<(SIGINT-1)) // Interrupt信号在信号位图中对应的位屏蔽位
#define QUITMASK (1<<(SIGQUIT-1)) // Quit信号在信号位图中对应的位屏蔽位
#define TSTPMASK (1<<(SIGTSTP-1)) // TTY Stop信号在信号位图中对应的位屏蔽位

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>

// 获取terminos结构中三个模式标志集之一，或者用于判断一个标志集是否有置位标志
#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f) // 本地模式标志
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f) // 输入模式标志
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f) // 输出模式标志

#define L_CANON(tty)	_L_FLAG((tty),ICANON) // 是否开启规范模式
#define L_ISIG(tty)	_L_FLAG((tty),ISIG) // 是否相应信号
#define L_ECHO(tty)	_L_FLAG((tty),ECHO) // 是否回显字符
#define L_ECHOE(tty)	_L_FLAG((tty),ECHOE) // 规范模式时候回显是否需要擦除
#define L_ECHOK(tty)	_L_FLAG((tty),ECHOK) // 规范模式时候回显是否擦除行
#define L_ECHOCTL(tty)	_L_FLAG((tty),ECHOCTL) // 回显是否显示控制字符
#define L_ECHOKE(tty)	_L_FLAG((tty),ECHOKE) // 是否显示被KILL擦除行的字符

#define I_UCLC(tty)	_I_FLAG((tty),IUCLC) // 是否把输入的大写字符转换成小写
#define I_NLCR(tty)	_I_FLAG((tty),INLCR) // 是否把输入的换行符NL转换成回车符CR 
#define I_CRNL(tty)	_I_FLAG((tty),ICRNL) // 是否把输入的回车符CR转换成换行符NL
#define I_NOCR(tty)	_I_FLAG((tty),IGNCR) // 是否忽略输入的回车符CR

#define O_POST(tty)	_O_FLAG((tty),OPOST) // 是否执行输出处理
#define O_NLCR(tty)	_O_FLAG((tty),ONLCR) // 是否把换行符NL转换成回车符CR输出
#define O_CRNL(tty)	_O_FLAG((tty),OCRNL) // 是否把回车符CR转换成换行符NL输出
#define O_NLRET(tty)	_O_FLAG((tty),ONLRET) // 换行符NL是否执行回车的功能
#define O_LCUC(tty)	_O_FLAG((tty),OLCUC) // 输出时是否把小写字符转换成大写字符


/**
 * 终端结构表数组
 *
 * 这里总共有3个数据项，分别代表了console控制台终端，rs1串行口1终端，rs2串行口2终端
 * 
 */
struct tty_struct tty_table[] = {
        // 控制台终端
        {
                // 终端属性
                {ICRNL,		// 输入时把回车符CR转换成换行符NL 
                 OPOST|ONLCR,	 // 把换行符NL作为回车符CR输出，并且执行输出处理
                 0, // 控制模式为NULL（没有波特率等传输信息）
                 ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, // 本地模式标志: 相应信号，规范模式，回显字符，回显字符中显示控制字符，回显模式显示被擦除行的字符
                 0, // 线路模式为NULL 
                 INIT_C_CC}, // 标准控制字符数组
                0, // 初始进程组为0
                0,	// 初始停止标志为0
                con_write, // 写控制终端函数指针为con_write(console.c中)
                {0,0,0,0,""},	//控制台终端读缓冲队列（含有0个字符，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）
                {0,0,0,0,""},	//控制台终端写缓冲队列（含有0个字符，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）	
                {0,0,0,0,""}	//控制台终端辅助缓冲队列（含有0个字符，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）	
        },
        // 串行口1终端
        {
                // 终端属性
                {0, // 输入不做转换
                 0,  // 输出不做转换
                 B2400 | CS8, // 控制模式：波特率2400，每个字符8位（1个字节）
                 0, // 本地模式为NULL
                 0, // 线路模式为NULL 
                 INIT_C_CC}, // 标准控制字符数组
                0, // 初始进程组为0
                0, // 初始停止标志为0
                rs_write, // 串行中断写函数指针rs_write(serial.c中)
                {0x3f8,0,0,0,""},	// 串行口1终端读缓冲队列（串行口1寄存器端口基地址0x3f8，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）
                {0x3f8,0,0,0,""}, // 串行口1终端写缓冲队列（串行口1寄存器端口基地址0x3f8，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）
                {0,0,0,0,""} // 串行口1终端辅助缓冲队列（含有0个字符，头指针偏移为0, 尾指针偏移为0, 等待进程队列为NULL）
        },
        // 串行口2终端：除了串行口2的端口基地址(0x2f8)不同，其余和上面串行口1类似
        {
                {0, /* no translation */
                 0,  /* no translation */
                 B2400 | CS8,
                 0,
                 0,
                 INIT_C_CC},
                0,
                0,
                rs_write,
                {0x2f8,0,0,0,""},		/* rs 2 */
                {0x2f8,0,0,0,""},
                {0,0,0,0,""}
        }
};

/*
 * these are the tables used by the machine code handlers.
 * you can implement pseudo-tty's or something by changing
 * them. Currently not done.
 */

/**
 * 
 * 汇编程序(rs_io.s)中使用的终端读写缓冲队列结构指针地址表
 *
 * 通过修改这张被可以实现伪终端。现在还没有完成
 * 
 */
struct tty_queue * table_list[]={
        &tty_table[0].read_q, &tty_table[0].write_q, // 控制台终端的读写队列指针
        &tty_table[1].read_q, &tty_table[1].write_q, // 串行口1终端的读写队列指针
        &tty_table[2].read_q, &tty_table[2].write_q  // 串行口2终端的读写队列指针
};

/**
 * tty终端初始化函数：在main.c/main()中被调用
 * 
 */
void tty_init(void)
{
        rs_init(); // 初始化串行终端
        con_init(); // 初始化控制台终端
}

/**
 * 向终端所属进程组中的所有进程发送某个信号
 *
 * tty: 终端结构指针
 * mask: 某个信号在信号位图中对应的位屏蔽位（该信号位为1，其余位都为0）
 *
 * 无返回
 * 
 */
void tty_intr(struct tty_struct * tty, int mask)
{
        int i;

        if (tty->pgrp <= 0) // 终端的进程组号非大于0（无进程组），直接返回
                return;
        for (i=0;i<NR_TASKS;i++)
                if (task[i] && task[i]->pgrp==tty->pgrp) // 找到进程组号等于该终端进程组号的所有进程
                        task[i]->signal |= mask; // 置位“该进程”中“进程位图”的相应位（实际上就是发送某个信号给进程）
}

/*
 * 如果缓冲队列为空则让进程进入可中断的睡眠状态：进程在取队列缓冲区中的字符前需要调用此函数保证队列不为空
 *
 * queue: 缓冲队列指针
 *
 * 无返回
 * 
 */
static void sleep_if_empty(struct tty_queue * queue)
{
        cli(); // 关闭中断
        while (!current->signal && EMPTY(*queue)) // 如果当前进程没有信号需要处理，并且指定的缓冲队列空 
                interruptible_sleep_on(&queue->proc_list); // 让进程进入可中断的睡眠状态，并且让队列的进程等待指针设置为当前进程
        sti(); // 开启中断
}

/*
 * 如果缓冲队列满则让进程进入可中断的睡眠状态：进程在往队列缓冲区中的存入字符前需要调用此函数保证队列保证有空间
 *
 * queue: 缓冲队列指针
 *
 * 无返回
 *
 */
static void sleep_if_full(struct tty_queue * queue)
{
        if (!FULL(*queue)) // 队列未满，直接返回
                return; 
        cli(); // 关闭中断
        while (!current->signal && LEFT(*queue)<128) // 如果当前进程没有信号需要处理，比且剩余空间 < 128字节
                interruptible_sleep_on(&queue->proc_list); // 让进程进入可中断的睡眠状态，并且让队列的进程等待指针设置为当前进程
        sti(); // 开启中断
}

/**
 * 等待按键
 * 
 */
void wait_for_keypress(void)
{
        sleep_if_empty(&tty_table[0].secondary); // 如果辅助缓冲队列为空，则让进程进入可中断的休眠状态
}

/**
 * 复制成规范模式的字符序列
 *
 * tty: 对应终端指针
 *
 * 无返回
 * 
 * 根据termios设置的各种标志，将指定tty终端读队列缓冲区的字符复制转换成规范（熟）模式字符序列，并放入辅助队列的缓冲区中
 * 注意：如果开启回显模式，不仅写入辅助队列的规范模式的，而是同时也会写入到该终端的写队列，再通过调用tty_write()函数显示在控制台或由串行口发送出去
 * 
 */
void copy_to_cooked(struct tty_struct * tty)
{
        signed char c;

        while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) { // 读队列不空 并且 辅助队列不满
                GETCH(tty->read_q,c); // 从读队列读取字符到变量c
                if (c==13) // 该字符是回车字符(CR:13)
                        if (I_CRNL(tty)) // 把输入的回车符CR转换成换行符NL 
                                c=10; // 转换成换行符(NL:10)
                        else if (I_NOCR(tty)) // 忽略输入的回车符CR
                                continue; // 读取下一个字符
                        else ;
                else if (c==10 && I_NLCR(tty)) // 换行字符(NL:10) 并且 把输入的换行符NL转换成回车符CR
                        c=13; // 转换成回车符(CR:13)
                if (I_UCLC(tty)) // 把输入的大写字符转换成小写
                        c=tolower(c); // 转换成小写字符
                if (L_CANON(tty)) { // 开启规范模式
                        if (c==KILL_CHAR(tty)) { // 该字符是“删除行字符”(^U)
                                /* deal with killing the input line */
                                // 循环删除一行字符直到出现下面三种情况：
                                // 1. 辅助队列为空
                                // 2. 辅助队列最后一个字符是换行符(NL:10)
                                // 3. 辅助队列最后一个字符是文件结束符(^D)
                                while(!(EMPTY(tty->secondary) || // 辅助队列为空 或者
                                        (c=LAST(tty->secondary))==10 || // 辅助队列最后一个字符是换行符(NL:10) 或者
                                        c==EOF_CHAR(tty))) { // 辅助队列最后一个字符是文件结束符(^D)
                                        if (L_ECHO(tty)) { // 回显字符
                                                if (c<32) // 小于32：这是控制字符（2字节）
                                                        PUTCH(127,tty->write_q); // 需要额外放入一个擦除字符(ERASE:127 ^H)
                                                PUTCH(127,tty->write_q); // 放入一个擦除字符(ERASE:127 ^H)
                                                tty->write(tty); // 写终端
                                        }
                                        DEC(tty->secondary.head); // 辅助队列的头指针后退一个字节
                                }
                                continue; // 读取下一个字符
                        }
                        if (c==ERASE_CHAR(tty)) { // 该字符字符是“擦除字符”(^H)
                                // 处理逻辑和循环逻辑一样，只是这里不用循环
                                if (EMPTY(tty->secondary) || 
                                    (c=LAST(tty->secondary))==10 ||
                                    c==EOF_CHAR(tty))
                                        continue; 
                                if (L_ECHO(tty)) {
                                        if (c<32)
                                                PUTCH(127,tty->write_q);
                                        PUTCH(127,tty->write_q);
                                        tty->write(tty);
                                }
                                DEC(tty->secondary.head);
                                continue;
                        }
                        if (c==STOP_CHAR(tty)) { // “停止终端”字符(^S)
                                tty->stopped=1; // 置位终端停止标志
                                continue; // 读取下一个字符
                        }
                        if (c==START_CHAR(tty)) { // “恢复终端”字符(^Q)
                                tty->stopped=0; // 复位终端停止标志
                                continue; // 读取下一个字符
                        }
                }
                if (L_ISIG(tty)) { // 允许处理信号
                        if (c==INTR_CHAR(tty)) { // “键盘中断”字符(^C)
                                tty_intr(tty,INTMASK); // 发送“进程中断Interrupt“信号给终端进程组的所有进程
                                continue; // 读取下一个字符
                        }
                        if (c==QUIT_CHAR(tty)) { // “键盘退出”字符(^\)
                                tty_intr(tty,QUITMASK); // 发送“进程退出QUIT”信号给终端进程组的所有进程
                                continue; // 读取下一个字符
                        }
                }
                if (c==10 || c==EOF_CHAR(tty)) // 换行符(NL:10) 或 文件终止符(^D) 
                        tty->secondary.data++; // 辅助队列记录的行数 + 1
                if (L_ECHO(tty)) { // 回显字符
                        if (c==10) { // 换行符(NL:10)
                                PUTCH(10,tty->write_q); // 换行符(NL:10)放入到写缓冲队列
                                PUTCH(13,tty->write_q); // 回车符(CR:13)放入到写缓冲队列
                        } else if (c<32) { // 控制字符
                                if (L_ECHOCTL(tty)) { // 回显控制字符(^C, ^D ....)
                                        PUTCH('^',tty->write_q); // '^'放入到写缓冲队列 
                                        PUTCH(c+64,tty->write_q); // '该字符的ASCII+64'放入到写缓冲队列
                                }
                        } else
                                PUTCH(c,tty->write_q); // 普通字符直接放入到写缓冲队列
                        tty->write(tty); // 把字符写到终端
                }
                PUTCH(c,tty->secondary); // 把字符放入到辅助队列中，并且头指针 + 1 
        }
        wake_up(&tty->secondary.proc_list); // 唤醒等待“该辅助队列为空”的其他进程（如果有的话）
}


/**
 * tty读函数：从终端辅助队列读取指定数量的字符，放到用户指定的缓冲区中
 *
 * channel: 子设备号
 * buf: 用户缓冲区指针
 * nr: 欲读字节数
 *
 * 成功：返回读取到的字符数，失败：返回错误号
 * 
 */
int tty_read(unsigned channel, char * buf, int nr)
{
        struct tty_struct * tty;
        char c, *b=buf;
        int minimum,time,flag=0;
        long oldalarm;

        if (channel>2 || nr<0) return -1; // 终端子设备号 或 欲读字节数 非法，直接返回-1
        tty = &tty_table[channel]; // 获取对应的终端结构指针
        oldalarm = current->alarm; // 获得当前进程的报警定时值（滴答数）
        time = 10L*tty->termios.c_cc[VTIME]; // 计算读操作超时定时值（单位：滴答数，而VTIME是一个1/10秒计数计时值）
        minimum = tty->termios.c_cc[VMIN]; // 获得至少要读的字符数
        if (time && !minimum) { // 设置了读操作超时值，并且没有设置最少要读的字符数
                minimum=1; // 设置至少要读一个字符
                // 这里设置是否要超时的flag
                if ((flag=(!oldalarm || time+jiffies<oldalarm))) // 当前报警定时值为空 或 读超时的时间比报警定时要早
                        current->alarm = time+jiffies; // 设置当前进程的报警定时为读操作超时时刻
        }
        if (minimum>nr) // 至少要读的字符数 > 想要读的字符数
                minimum=nr; // 设置最小要读的字符为“想要读的字符数”
        // 现在开始从辅助队列读取字符并放到要读用户缓冲区中。当预读的字符数 > 0，则执行下面循环
        while (nr>0) {
                if (flag && (current->signal & ALRMMASK)) { // 允许超时 并且 进程信号位图ALARM位已经置位（收到一个定时报警信号）
                        current->signal &= ~ALRMMASK; // 复位进程信号位图中的定时报警位
                        break; // 超时，中断循环
                }
                if (current->signal) // 有信号要处理
                        break; // 中断循环
                // 下面情况下：当前进程进入可中断的休眠状态：
                // 1. 终端辅助队列已经空
                // 2. 处于规范模式下 并且 辅助队列的数据不满一行 并且 辅助队列的空闲字节数 > 20 
                if (EMPTY(tty->secondary) || (L_CANON(tty) &&
                                              !tty->secondary.data && LEFT(tty->secondary)>20)) {
                        sleep_if_empty(&tty->secondary); // 当前进程进入可中断的睡眠状态
                        continue; // 从头开始执行循环
                }
                do {
                        GETCH(tty->secondary,c); // 从辅助队列读取一个字符到变量c
                        if (c==EOF_CHAR(tty) || c==10) // 该字符是文件结束字符(^D)或换行符(NL:10)
                                tty->secondary.data--; // 辅助队列的行数递减1
                        if (c==EOF_CHAR(tty) && L_CANON(tty)) // 如果该字符是文件结束符(^D)并且处于规范模式下
                                return (b-buf); // 返回实际读取的字节数
                        else {
                                put_fs_byte(c,b++); // 把读取到的字符放入到用户缓冲区中，对应的缓冲区指针b向前移动一个字节
                                if (!--nr) // 想要读取的字节数 - 1，并判断是否大于0
                                        break; // 如果等于0：已经全部读取完毕，中断当前循环
                        }
                } while (nr>0 && !EMPTY(tty->secondary)); // 想读取的字节数 > 0 并且辅助队列不为空，继续这个循环

                // 执行到这里，有两种情况：
                // 1. 读完所有的字节数
                // 2. 辅助队列已空
                if (time && !L_CANON(tty)) { // 有超时值 并且 非规范模式下
                        // 这里再次设置是否超时的flag值
                        // 注意：实际上这里是为了处理辅助队列为空，让其他进程有时间写入辅助读列。但这里的逻辑比较混乱，后面版本重写过超时的逻辑
                        if ((flag=(!oldalarm || time+jiffies<oldalarm))) // 当前报警定时值为空 或 读超时的时间比报警定时要早
                                current->alarm = time+jiffies; // 设置当前进程的报警定时为读操作超时时刻
                        else
                                current->alarm = oldalarm; // 当前进程的报警定时恢复为原来设置的报警定时
                }
                if (L_CANON(tty)) { // 规范模式
                        if (b-buf) // 已经读取了至少一个字符
                                break; // 中断最外层循环
                } else if (b-buf >= minimum) // 非规范模式下，读取的字符数超过了最少要读的字符数
                        break; // 中断最外层循环
        }
        current->alarm = oldalarm; // 当前进程的报警定时恢复为原来设置的报警定时
        if (current->signal && !(b-buf)) // 已经捕获到信号 并且 没有读取到任何的字符
                return -EINTR; // 返回 EINTR（被信号中断）做为错误值
        return (b-buf); // 返回已经读取到的字符数
}

/**
 * 写终端函数：把用户缓冲区的字符写入到指定的终端去
 *
 * channel: 子设备号
 * buf: 用户缓冲区指针
 * nr: 欲写的字节数
 *
 * 成功：返回写入的字符数，失败：返回错误号
 * 
 */
int tty_write(unsigned channel, char * buf, int nr)
{
        static int cr_flag = 0; // 回车处理标志
        struct tty_struct *tty;
        char c, *b=buf;

        if (channel>2 || nr<0) // 终端子设备号 或 欲写字节数 非法，直接返回-1
                return -1;
        tty = channel + tty_table; // 获得对应的终端结构指针
        // 开始从用户缓冲区中读取字符放入到终端写队列的循环
        while (nr>0) { // 要写的字节数 > 0 
                sleep_if_full(&tty->write_q); // 如果终端写队列满了，则当前进程进入可中断的睡眠状态 
                if (current->signal) // 当前进程有信号要处理
                        break; // 直接退出最外层循环
                while (nr>0 && !FULL(tty->write_q)) { // 还有要写的字节 并且 终端写队列不满
                        c = get_fs_byte(b); // 从用户缓冲区读取一个字符到变量c
                        if (O_POST(tty)) { // 执行输出处理
                                if (c == '\r' && O_CRNL(tty)) // 字符是回车符(CR:13) 并且 把回车符CR转换成换行符NL输出
                                        c='\n'; // 转换成换行符(NL:10)
                                else if (c=='\n' && O_NLRET(tty)) // 字符是换行符(NL:10) 并且 换行符NL是否执行回车的功能 
                                        c='\r'; // 转换成回车符(CR:13)
                                else if (c=='\n' && !cr_flag && O_NLCR(tty)) { // 字符是换行符(NL:10) 并且 回车标志没有置位 并且 把换行符NL转换成回车符CR输出
                                        cr_flag = 1; // 置位回车标志
                                        PUTCH(13,tty->write_q); // 向终端写队列多写入一个回车符(CR:13)
                                        continue; // 处理下一个字符：此时的回车标志处于被置位中！
                                } else if (O_LCUC(tty)) // 转换小写字符为大写字符输出
                                        c=toupper(c); // 转换成大写字符
                        }
                        b++; nr--; // 用户缓冲区指针前移，要写的字节数 - 1
                        cr_flag = 0; // 复位回车标志
                        PUTCH(c,tty->write_q); // 把字符写入到终端写队列中
                }

                // 执行到这里： 有2种情况：
                // 1. 要写的字节已经全部写到终端的写队列中了
                // 2. 终端的写缓冲队列已经满了
                
                // 无论哪种情况，都先把终端写队列中当前的字符序列输出到真正的终端设备去
                tty->write(tty); // 控制台：调用con_write()，串行口：调用rs_write()
                if (nr>0) // 如果还有要写的字符：说明终端写队列已满
                        schedule(); // 调度其他任务，等待上面的输出操作使得写队列不满
        }
        
        return (b - buf); // 返回写成功的字节数
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */

/** 
 * tty中断调用函数：在rs_io.s读字符和keyboard.S的键盘中断时被调用
 *
 * tty: 终端号，实际上就是终端结构数组表的下标
 *
 * 无返回
 *
 */
void do_tty_interrupt(int tty)
{
        // tty_table + tty : 可以获得对应终端结构的指针
        // 注意：如果开启回显模式，不仅写入辅助队列的规范模式的，而是同时也会写入到该终端的写队列，再通过调用tty_write()函数显示在控制台或由串行口发送出去
        copy_to_cooked(tty_table + tty); // 对应终端的读队列缓冲区中的字符转换成规范模式下的字符序列并放入到辅助队列中
}

void chr_dev_init(void)
{
}
