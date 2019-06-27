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

void tty_init(void)
{
        rs_init();
        con_init();
}

void tty_intr(struct tty_struct * tty, int mask)
{
        int i;

        if (tty->pgrp <= 0)
                return;
        for (i=0;i<NR_TASKS;i++)
                if (task[i] && task[i]->pgrp==tty->pgrp)
                        task[i]->signal |= mask;
}

static void sleep_if_empty(struct tty_queue * queue)
{
        cli();
        while (!current->signal && EMPTY(*queue))
                interruptible_sleep_on(&queue->proc_list);
        sti();
}

static void sleep_if_full(struct tty_queue * queue)
{
        if (!FULL(*queue))
                return;
        cli();
        while (!current->signal && LEFT(*queue)<128)
                interruptible_sleep_on(&queue->proc_list);
        sti();
}

void wait_for_keypress(void)
{
        sleep_if_empty(&tty_table[0].secondary);
}

void copy_to_cooked(struct tty_struct * tty)
{
        signed char c;

        while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
                GETCH(tty->read_q,c);
                if (c==13)
                        if (I_CRNL(tty))
                                c=10;
                        else if (I_NOCR(tty))
                                continue;
                        else ;
                else if (c==10 && I_NLCR(tty))
                        c=13;
                if (I_UCLC(tty))
                        c=tolower(c);
                if (L_CANON(tty)) {
                        if (c==KILL_CHAR(tty)) {
                                /* deal with killing the input line */
                                while(!(EMPTY(tty->secondary) ||
                                        (c=LAST(tty->secondary))==10 ||
                                        c==EOF_CHAR(tty))) {
                                        if (L_ECHO(tty)) {
                                                if (c<32)
                                                        PUTCH(127,tty->write_q);
                                                PUTCH(127,tty->write_q);
                                                tty->write(tty);
                                        }
                                        DEC(tty->secondary.head);
                                }
                                continue;
                        }
                        if (c==ERASE_CHAR(tty)) {
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
                        if (c==STOP_CHAR(tty)) {
                                tty->stopped=1;
                                continue;
                        }
                        if (c==START_CHAR(tty)) {
                                tty->stopped=0;
                                continue;
                        }
                }
                if (L_ISIG(tty)) {
                        if (c==INTR_CHAR(tty)) {
                                tty_intr(tty,INTMASK);
                                continue;
                        }
                        if (c==QUIT_CHAR(tty)) {
                                tty_intr(tty,QUITMASK);
                                continue;
                        }
                }
                if (c==10 || c==EOF_CHAR(tty))
                        tty->secondary.data++;
                if (L_ECHO(tty)) {
                        if (c==10) {
                                PUTCH(10,tty->write_q);
                                PUTCH(13,tty->write_q);
                        } else if (c<32) {
                                if (L_ECHOCTL(tty)) {
                                        PUTCH('^',tty->write_q);
                                        PUTCH(c+64,tty->write_q);
                                }
                        } else
                                PUTCH(c,tty->write_q);
                        tty->write(tty);
                }
                PUTCH(c,tty->secondary);
        }
        wake_up(&tty->secondary.proc_list);
}

int tty_read(unsigned channel, char * buf, int nr)
{
        struct tty_struct * tty;
        char c, * b=buf;
        int minimum,time,flag=0;
        long oldalarm;

        if (channel>2 || nr<0) return -1;
        tty = &tty_table[channel];
        oldalarm = current->alarm;
        time = 10L*tty->termios.c_cc[VTIME];
        minimum = tty->termios.c_cc[VMIN];
        if (time && !minimum) {
                minimum=1;
                if ((flag=(!oldalarm || time+jiffies<oldalarm)))
                        current->alarm = time+jiffies;
        }
        if (minimum>nr)
                minimum=nr;
        while (nr>0) {
                if (flag && (current->signal & ALRMMASK)) {
                        current->signal &= ~ALRMMASK;
                        break;
                }
                if (current->signal)
                        break;
                if (EMPTY(tty->secondary) || (L_CANON(tty) &&
                                              !tty->secondary.data && LEFT(tty->secondary)>20)) {
                        sleep_if_empty(&tty->secondary);
                        continue;
                }
                do {
                        GETCH(tty->secondary,c);
                        if (c==EOF_CHAR(tty) || c==10)
                                tty->secondary.data--;
                        if (c==EOF_CHAR(tty) && L_CANON(tty))
                                return (b-buf);
                        else {
                                put_fs_byte(c,b++);
                                if (!--nr)
                                        break;
                        }
                } while (nr>0 && !EMPTY(tty->secondary));
                if (time && !L_CANON(tty)) {
                        if ((flag=(!oldalarm || time+jiffies<oldalarm)))
                                current->alarm = time+jiffies;
                        else
                                current->alarm = oldalarm;
                }
                if (L_CANON(tty)) {
                        if (b-buf)
                                break;
                } else if (b-buf >= minimum)
                        break;
        }
        current->alarm = oldalarm;
        if (current->signal && !(b-buf))
                return -EINTR;
        return (b-buf);
}

int tty_write(unsigned channel, char * buf, int nr)
{
        static int cr_flag = 0;
        struct tty_struct *tty;
        char c, *b=buf;

        if (channel>2 || nr<0)
                return -1;
        tty = channel + tty_table;
        while (nr>0) {
                sleep_if_full(&tty->write_q);
                if (current->signal)
                        break;
                while (nr>0 && !FULL(tty->write_q)) {
                        c = get_fs_byte(b);
                        if (O_POST(tty)) {
                                if (c == '\r' && O_CRNL(tty))
                                        c='\n';
                                else if (c=='\n' && O_NLRET(tty))
                                        c='\r';
                                else if (c=='\n' && !cr_flag && O_NLCR(tty)) {
                                        cr_flag = 1;
                                        PUTCH(13,tty->write_q);
                                        continue;
                                } else if (O_LCUC(tty))
                                        c=toupper(c);
                        }
                        b++; nr--;
                        cr_flag = 0;
                        PUTCH(c,tty->write_q);
                }
                
                tty->write(tty);

                if (nr>0)
                        schedule();
        }
        
        return (b - buf);
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
void do_tty_interrupt(int tty)
{
        copy_to_cooked(tty_table+tty);
}

void chr_dev_init(void)
{
}
