#ifndef _TERMIOS_H
#define _TERMIOS_H

#define TTY_BUF_SIZE 1024 // tty中的缓冲区长度

/* 0x54 is just a magic number to make these relatively uniqe ('T') */
/*0x54只是一个魔数，目的是使下面这些常数唯一*/

/**
 * TC开头的表示：tty 设备的ioctl调用中的cmd常数，ioctl把命令编码在低位字中
 *
 */
// 取相应终端的terminos结构中的信息
#define TCGETS		0x5401
// 设置相应终端terminos结构中的信息
#define TCSETS		0x5402
// 设置terminos结构中的信息之前，需要先等待输出队列中的数据处理完毕
#define TCSETSW		0x5403
// 设置terminos结构中的信息之前，需要先等待输出队列中的数据处理完毕，并且刷新（清空）输入队列
#define TCSETSF		0x5404
// 取相应终端termio结构中的信息
#define TCGETA		0x5405
// 设置相应终端termio结构中的信息
#define TCSETA		0x5406
// 设置termio结构中的信息之前，需要先等待输出队列中的数据处理完毕
#define TCSETAW		0x5407
// 设置termio结构中的信息之前，需要先等待输出队列中的数据处理完毕，并且刷新（清空）输入队列
#define TCSETAF		0x5408
// 等待输出队列数据处理完毕，若参数值是0,则发送一个break
#define TCSBRK		0x5409
// 开始/停止控制：如果参数是0,则挂起输出，如果参数是1,则重新开启挂起的输出，如果是2,则挂起输入，如果是3,则重启开启挂起的输入
#define TCXONC		0x540A
// 刷新已写输出但还没发送或已收但还没有读的数据。如果参数是0,则刷新输入队列,如果参数是1,则刷新输出队列，如果参数是2,则刷新输入和输出队列
#define TCFLSH		0x540B

/**
 * TIO 开头的表示：tty输入输出控制命令
 * 
 */
// 设置终端串行线路专用模式
#define TIOCEXCL	0x540C
// 复位终端串行线路专用模式
#define TIOCNXCL	0x540D
// 设置tty为控制终端
#define TIOCSCTTY	0x540E
// 读取指定终端设备的进程组ID
#define TIOCGPGRP	0x540F
// 设置指定终端设备的进程组ID
#define TIOCSPGRP	0x5410
// 返回输出队列还没送出的字符数
#define TIOCOUTQ	0x5411
// 模拟终端输入：该命令以一个指向字符的指针作为参数，并假装该字符是在终端键入的。用户必须在该控制终端上具有超级用户权限或具有读权限
#define TIOCSTI		0x5412
// 读取终端设备窗口大小
#define TIOCGWINSZ	0x5413
// 设置终端设备窗口大小
#define TIOCSWINSZ	0x5414
// 返回modem状态控制引线的当前状态比特位标志集
#define TIOCMGET	0x5415
// 设置单个modem状态控制引线的状态（true或false）
#define TIOCMBIS	0x5416
// 复位单个modem状态控制引线的状态
#define TIOCMBIC	0x5417
// 设置modem状态控制引线的当前状态比特位标志集。如果某一比特位被置位，则modem对应的状态引线将会设置为有效
#define TIOCMSET	0x5418
// 读取软件载波检测标志（1：开启，0：关闭）
// 对于本地连接的终端设备，软件载波标志是开启的，对于使用modem线路的终端设备，软件载波标志是关闭的
// 为了能使用这两个ioctl调用，tty线路应该以O_NDELAY方式打开，这样open()就不会等待载波
#define TIOCGSOFTCAR	0x5419
// 设置软件载波检测标志
#define TIOCSSOFTCAR	0x541A
// 返回输入队列中还未取走的字符
#define TIOCINQ		0x541B

/**
 * 窗口大小属性结构
 * 
 * 在图形界面环境下可用于基于屏幕的应用程序
 * 
 */
struct winsize {
        unsigned short ws_row; // 窗口字符行数
        unsigned short ws_col; // 窗口字符列数
        unsigned short ws_xpixel; // 窗口宽度（像素值）
        unsigned short ws_ypixel; // 窗口高度（像素值）
};


#define NCC 8 // termio结构中的控制字符数组的长度

/**
 * AT&T系统V的termio结构
 * 
 */
struct termio {
        unsigned short c_iflag;		/* input mode flags */ // 输入模式标志
        unsigned short c_oflag;		/* output mode flags */ // 输出模式标志
        unsigned short c_cflag;		/* control mode flags */ // 控制模式标志
        unsigned short c_lflag;		/* local mode flags */ // 本地模式标志
        unsigned char c_line;		/* line discipline */ // 线路规程（速率） 
        unsigned char c_cc[NCC];	/* control characters */ // 控制字符数组
};

#define NCCS 17 // terminos结构中的控制字符数组的长度

/**
 * POSIX中的terminos结构
 * 
 * 此结构和termio完全类似，只是控制字符数组的长度不同
 * 
 */
struct termios {
	unsigned long c_iflag;		/* input mode flags */
	unsigned long c_oflag;		/* output mode flags */
	unsigned long c_cflag;		/* control mode flags */
	unsigned long c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCCS];	/* control characters */
};

/* c_cc characters */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSWTC 7
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16

/* c_iflag bits */
#define IGNBRK	0000001
#define BRKINT	0000002
#define IGNPAR	0000004
#define PARMRK	0000010
#define INPCK	0000020
#define ISTRIP	0000040
#define INLCR	0000100
#define IGNCR	0000200
#define ICRNL	0000400
#define IUCLC	0001000
#define IXON	0002000
#define IXANY	0004000
#define IXOFF	0010000
#define IMAXBEL	0020000

/* c_oflag bits */
#define OPOST	0000001
#define OLCUC	0000002
#define ONLCR	0000004
#define OCRNL	0000010
#define ONOCR	0000020
#define ONLRET	0000040
#define OFILL	0000100
#define OFDEL	0000200
#define NLDLY	0000400
#define   NL0	0000000
#define   NL1	0000400
#define CRDLY	0003000
#define   CR0	0000000
#define   CR1	0001000
#define   CR2	0002000
#define   CR3	0003000
#define TABDLY	0014000
#define   TAB0	0000000
#define   TAB1	0004000
#define   TAB2	0010000
#define   TAB3	0014000
#define   XTABS	0014000
#define BSDLY	0020000
#define   BS0	0000000
#define   BS1	0020000
#define VTDLY	0040000
#define   VT0	0000000
#define   VT1	0040000
#define FFDLY	0040000
#define   FF0	0000000
#define   FF1	0040000

/* c_cflag bit meaning */
#define CBAUD	0000017
#define  B0	0000000		/* hang up */
#define  B50	0000001
#define  B75	0000002
#define  B110	0000003
#define  B134	0000004
#define  B150	0000005
#define  B200	0000006
#define  B300	0000007
#define  B600	0000010
#define  B1200	0000011
#define  B1800	0000012
#define  B2400	0000013
#define  B4800	0000014
#define  B9600	0000015
#define  B19200	0000016
#define  B38400	0000017
#define EXTA B19200
#define EXTB B38400
#define CSIZE	0000060
#define   CS5	0000000
#define   CS6	0000020
#define   CS7	0000040
#define   CS8	0000060
#define CSTOPB	0000100
#define CREAD	0000200
#define CPARENB	0000400
#define CPARODD	0001000
#define HUPCL	0002000
#define CLOCAL	0004000
#define CIBAUD	03600000		/* input baud rate (not used) */
#define CRTSCTS	020000000000		/* flow control */

#define PARENB CPARENB
#define PARODD CPARODD

/* c_lflag bits */
#define ISIG	0000001
#define ICANON	0000002
#define XCASE	0000004
#define ECHO	0000010
#define ECHOE	0000020
#define ECHOK	0000040
#define ECHONL	0000100
#define NOFLSH	0000200
#define TOSTOP	0000400
#define ECHOCTL	0001000
#define ECHOPRT	0002000
#define ECHOKE	0004000
#define FLUSHO	0010000
#define PENDIN	0040000
#define IEXTEN	0100000

/* modem lines */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */
#define	TCOOFF		0
#define	TCOON		1
#define	TCIOFF		2
#define	TCION		3

/* tcflush() and TCFLSH use these */
#define	TCIFLUSH	0
#define	TCOFLUSH	1
#define	TCIOFLUSH	2

/* tcsetattr uses these */
#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

typedef int speed_t;

extern speed_t cfgetispeed(struct termios *termios_p);
extern speed_t cfgetospeed(struct termios *termios_p);
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
extern int tcdrain(int fildes);
extern int tcflow(int fildes, int action);
extern int tcflush(int fildes, int queue_selector);
extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsendbreak(int fildes, int duration);
extern int tcsetattr(int fildes, int optional_actions,
	struct termios *termios_p);

#endif
