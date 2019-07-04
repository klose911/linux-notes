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


/**
 * 终端输入输出控制：被fs/ioctl.c中的sys_ioctl()函数所调用
 *
 * dev：终端次设备号
 * cmd：功能
 * arg：可选参数
 *
 * 成功：返回值依赖于cmd，失败：返回对应的错误码
 * 
 */
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
		case TCGETS: // 获取终端信息：保存到arg指针指向的用户进程内存处
                return get_termios(tty,(struct termios *) arg);
		case TCSETSF: // 设置终端之前，需要先等待输出队列中的数据处理完毕，并且刷新（清空）输入队列
                flush(&tty->read_q); /* fallthrough */
		case TCSETSW: // 设置终端之前，需要先等待输出队列中的数据处理完毕
                wait_until_sent(tty); /* fallthrough */
		case TCSETS: // 根据用户进程内存中的termios结构(arg指向这个结构)中的信息来设置相应终端信
                return set_termios(tty,(struct termios *) arg);
		case TCGETA: // 或取相应终端信息：保存到arg指针指向的用户进程内存处（保存为termio结构格式）
                return get_termio(tty,(struct termio *) arg);
		case TCSETAF: // 设置termio结构中的信息之前，需要先等待输出队列中的数据处理完毕，并且刷新（清空）输入队列
                flush(&tty->read_q); /* fallthrough */
		case TCSETAW: // 设置termio结构中的信息之前，需要先等待输出队列中的数据处理完毕
                wait_until_sent(tty); /* fallthrough */
		case TCSETA: // 根据用户进程内存中的termio结构(arg指向这个结构)中的信息来设置相应终端信息
                return set_termio(tty,(struct termio *) arg);
		case TCSBRK: // 待输出队列数据处理完毕，若参数值是0,则发送一个break字符
                if (!arg) {
                        wait_until_sent(tty);
                        send_break(tty);
                }
                return 0;
		case TCXONC: // 开始/停止控制：如果参数是0,则挂起输出，如果参数是1,则重新开启挂起的输出，如果是2,则挂起输入，如果是3,则重启开启挂起的输入
                return -EINVAL; // 未实现
		case TCFLSH: // 刷新已写输出但还没发送或已收但还没有读的数据
                if (arg==0) // 如果参数是0,则刷新输入队列
                        flush(&tty->read_q);
                else if (arg==1) // 如果参数是1,则刷新输出队列
                        flush(&tty->write_q);
                else if (arg==2) { // 如果参数是2,则刷新输入和输出队列
                        flush(&tty->read_q);
                        flush(&tty->write_q);
                } else
                        return -EINVAL; // 参数无效
                return 0;
		case TIOCEXCL: // 设置终端串行线路专用模式，未实现
                return -EINVAL; /* not implemented */
		case TIOCNXCL: // 复位终端串行线路专用模式，未实现
                return -EINVAL; /* not implemented */
		case TIOCSCTTY: // 设置tty为控制终端，未实现
                return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP: // 读取指定终端设备的进程组ID到用户进程的内存中
                verify_area((void *) arg,4); // 验证用户进程内存是否足够，不够则分配新页面
                put_fs_long(tty->pgrp,(unsigned long *) arg); // 进程的进程组ID写入到用户进程的内存去
                return 0;
		case TIOCSPGRP: // 设置指定终端设备的进程组ID
                tty->pgrp=get_fs_long((unsigned long *) arg); // 从用户进程的内存区复制进程组ID到终端pgrp域
                return 0;
		case TIOCOUTQ: // 返回输出队列还没送出的字符数给用户进程
                verify_area((void *) arg,4);
                put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
                return 0;
		case TIOCINQ: // 返回输入队列中还未取走的字符给用户进程
                verify_area((void *) arg,4);
                put_fs_long(CHARS(tty->secondary),
                            (unsigned long *) arg);
                return 0;
		case TIOCSTI: // 模拟终端输入：该命令以一个指向字符的指针作为参数，并假装该字符是在终端键入的。用户必须在该控制终端上具有超级用户权限或具有读权限
                return -EINVAL; // 未实现
		case TIOCGWINSZ: // 读取终端设备窗口大小
                return -EINVAL; // 未实现
		case TIOCSWINSZ: // 设置终端设备窗口大小
                return -EINVAL; // 未实现
		case TIOCMGET: // 返回modem状态控制引线的当前状态比特位标志集
                return -EINVAL; // 未实现
		case TIOCMBIS: // 设置单个modem状态控制引线的状态（true或false）
                return -EINVAL; // 未实现
		case TIOCMBIC: // 复位单个modem状态控制引线的状态
                return -EINVAL; // 未实现
		case TIOCMSET: // 设置modem状态控制引线的当前状态比特位标志集。如果某一比特位被置位，则modem对应的状态引线将会设置为有效
                return -EINVAL; // 未实现
		case TIOCGSOFTCAR: // 读取软件载波检测标志（1：开启，0：关闭）
                return -EINVAL; // 未实现
		case TIOCSSOFTCAR: // 设置软件载波检测标志
                return -EINVAL; // 未实现
		default: // 命令无效
                return -EINVAL; // 返回错误号： EINVAL
        }
}
