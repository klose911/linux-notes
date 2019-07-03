/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

/**
 * 字符设备的控制操作
 * 
 */
#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

/*
 * 波特率因子数组：波特率=1.8432MHz/(16 * 波特率因子)
 * 例如：波特率是2400bit/s，对应的因子是48(0x30)，波特率是9600bit/s，对应的因子是12(0x1c) ...
 * 
 */
static unsigned short quotient[] = {
        0, 2304, 1536, 1047, 857,
        768, 576, 384, 192, 96,
        64, 48, 24, 12, 6, 3
};

/*
 * 修改传输的波特率
 *
 * tty: 终端结构指针
 *
 * 无返回
 * 
 */
static void change_speed(struct tty_struct * tty)
{
        unsigned short port,quot;

        if (!(port = tty->read_q.data)) // 读取串行口对应的端口基地址
                return; // 无法获得端口基地址，直接返回
        quot = quotient[tty->termios.c_cflag & CBAUD]; // 查找对应的波特率因子
        cli(); // 关中断
        outb_p(0x80,port+3);       // 开启串行口线路控制寄存器LCR的DLAB位（第7位）
        outb_p(quot & 0xff,port);  // 波特率因子低8位写到串行口LSR寄存器
        outb_p(quot >> 8,port+1);  // 波特额因子高8位写到串行块MSR寄存器
        outb(0x03,port+3);		// 禁止串行口线路控制寄存器LCR的DLAB位（第7位）
        sti(); // 开中断
}

/*
 * 刷新终端的某个缓冲队列
 *
 * queue: 终端缓冲队列
 *
 * 无返回
 * 
 */
static void flush(struct tty_queue * queue)
{
        cli(); // 关中断
        // 注意：缓冲队列是一个类似圆圈的自循环队列，所以无须让头指针，尾指针重新指向开头处！
        queue->head = queue->tail; // 缓冲队列的头指针 == 缓冲队列的尾指针：实际就是无法从缓冲队列再读取到任何字符（从而达到清空缓冲的效果）
        sti(); // 开中断
}

/*
 * 等待字符发送：未实现
 *
 */
static void wait_until_sent(struct tty_struct * tty)
{
        /* do nothing - not implemented */
}

/*
 * 发送BREAK字符：未实现
 * 
 */
static void send_break(struct tty_struct * tty)
{
        /* do nothing - not implemented */
}

/*
 * 读取终端termios结构（POSIX标准）信息
 *
 * tty: 终端结构指针
 * termios: 存放termios的用户缓冲区，放置结果
 * 
 * 成功：返回0
 * 
 */
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
        int i;

        verify_area(termios, sizeof (*termios)); // 验证用户缓冲区是否有足够的空间存放，如果不够，则重新分配内存页
        for (i=0 ; i< (sizeof (*termios)) ; i++)
                put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios ); // 从内核的tty终端结构中复制信息到用户缓冲区
        return 0; // 成功：返回0
}

/*
 * 设置tty终端的配置信息
 *
 * tty: 终端结构指针
 * termios: 用户进程存放termios结构的指针
 *
 * 成功：返回0
 * 
 */
static int set_termios(struct tty_struct * tty, struct termios * termios)
{
        int i;

        for (i=0 ; i< (sizeof (*termios)) ; i++)
                ((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios); // 从用户缓存区复制配置信息到内核中tty终端结构
        change_speed(tty); // 如果需要：刷新串行口的传输速度（控制台终端不需要）
        return 0; // 成功：返回0
}

/*
 * 读取终端结构到用户进程内存的termio结构（AT&T的System V）指针
 *
 * tty: 终端结构指针
 * temio: 用户进程内存的termio结构指针
 *
 * 成功：返回0
 * 
 */
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
        int i;
        struct termio tmp_termio;

        verify_area(termio, sizeof (*termio)); // 验证用户缓冲区是否有足够的空间存放，如果不够，则重新分配内存页
        // 把终端结构中保存的termios结构信息复制到临时termio结构中
        // 两种结构基本相同，不同的是termios结构的值是long类型，而termio结构的值是short类型
        tmp_termio.c_iflag = tty->termios.c_iflag;
        tmp_termio.c_oflag = tty->termios.c_oflag;
        tmp_termio.c_cflag = tty->termios.c_cflag;
        tmp_termio.c_lflag = tty->termios.c_lflag;
        tmp_termio.c_line = tty->termios.c_line;
        for(i=0 ; i < NCC ; i++)
                tmp_termio.c_cc[i] = tty->termios.c_cc[i];
        for (i=0 ; i< (sizeof (*termio)) ; i++)
                put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio ); // 这里和前面逻辑类似
        return 0; // 成功：返回0
}

/*
 * This only works as the 386 is low-byt-first
 */

/*
 * 根据用户进程中的termio结构设置tty终端的配置信息
 *
 * tty: 终端结构指针
 * termios: 用户进程存放termio结构的指针
 *
 * 成功：返回0
 *
 * 注意：这段代码只对低字节在前的386架构有效！
 * 
 */
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
        int i;
        struct termio tmp_termio;

        for (i=0 ; i< (sizeof (*termio)) ; i++)
                ((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio); // 从用户进程内存复制termio结构信息到内核临时termio结构中去
        // 复制内核临时termio结构中的信息到内核终端tty结构中的termios域去
        *(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
        *(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
        *(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
        *(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
        tty->termios.c_line = tmp_termio.c_line;
        for(i=0 ; i < NCC ; i++)
                tty->termios.c_cc[i] = tmp_termio.c_cc[i];
        change_speed(tty); // 如果有必要刷新串行传输速率
        return 0; // 成功：返回0
}

int tty_ioctl(int dev, int cmd, int arg)
{
        struct tty_struct * tty;
        if (MAJOR(dev) == 5) {
                dev=current->tty;
                if (dev<0)
                        panic("tty_ioctl: dev<0");
        } else
                dev=MINOR(dev);
        tty = dev + tty_table;
        switch (cmd) {
		case TCGETS:
                return get_termios(tty,(struct termios *) arg);
		case TCSETSF:
                flush(&tty->read_q); /* fallthrough */
		case TCSETSW:
                wait_until_sent(tty); /* fallthrough */
		case TCSETS:
                return set_termios(tty,(struct termios *) arg);
		case TCGETA:
                return get_termio(tty,(struct termio *) arg);
		case TCSETAF:
                flush(&tty->read_q); /* fallthrough */
		case TCSETAW:
                wait_until_sent(tty); /* fallthrough */
		case TCSETA:
                return set_termio(tty,(struct termio *) arg);
		case TCSBRK:
                if (!arg) {
                        wait_until_sent(tty);
                        send_break(tty);
                }
                return 0;
		case TCXONC:
                return -EINVAL; /* not implemented */
		case TCFLSH:
                if (arg==0)
                        flush(&tty->read_q);
                else if (arg==1)
                        flush(&tty->write_q);
                else if (arg==2) {
                        flush(&tty->read_q);
                        flush(&tty->write_q);
                } else
                        return -EINVAL;
                return 0;
		case TIOCEXCL:
                return -EINVAL; /* not implemented */
		case TIOCNXCL:
                return -EINVAL; /* not implemented */
		case TIOCSCTTY:
                return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP:
                verify_area((void *) arg,4);
                put_fs_long(tty->pgrp,(unsigned long *) arg);
                return 0;
		case TIOCSPGRP:
                tty->pgrp=get_fs_long((unsigned long *) arg);
                return 0;
		case TIOCOUTQ:
                verify_area((void *) arg,4);
                put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
                return 0;
		case TIOCINQ:
                verify_area((void *) arg,4);
                put_fs_long(CHARS(tty->secondary),
                            (unsigned long *) arg);
                return 0;
		case TIOCSTI:
                return -EINVAL; /* not implemented */
		case TIOCGWINSZ:
                return -EINVAL; /* not implemented */
		case TIOCSWINSZ:
                return -EINVAL; /* not implemented */
		case TIOCMGET:
                return -EINVAL; /* not implemented */
		case TIOCMBIS:
                return -EINVAL; /* not implemented */
		case TIOCMBIC:
                return -EINVAL; /* not implemented */
		case TIOCMSET:
                return -EINVAL; /* not implemented */
		case TIOCGSOFTCAR:
                return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:
                return -EINVAL; /* not implemented */
		default:
                return -EINVAL;
        }
}
