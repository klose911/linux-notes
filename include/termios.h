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

/**
 * 控制字符数组c_cc[]项的下标值
 * 
 */
#define VINTR 0 // INTR (^C) \003 中断字符
#define VQUIT 1 // QUIT (^\) \034 退出字符 
#define VERASE 2 // ERASE (^H) \177 擦除字符
#define VKILL 3 // KILL (^U) \025 终止字符（删除行） 
#define VEOF 4 // EOF (^D) \004 文件结束字符
#define VTIME 5 // TIME (\0) \0 定时器值
#define VMIN 6 // MIN (\1) \1 至少读取的字符个数
#define VSWTC 7 // SWTC (\0) \0 交换字符
#define VSTART 8 // START (^Q) \021 开始字符
#define VSTOP 9 // STOP (^S) \023 停止字符
#define VSUSP 10 // SUSP (^Z) \034 挂起字符
#define VEOL 11 // EOL (\0) \0 行结束字符
#define VREPRINT 12 // REPRINT (^R) \022 重显示字符
#define VDISCARD 13 // DISCARD (^O) \017 丢弃字符
#define VWERASE 14 // WERASE (^W) \027 单词擦除字符
#define VLNEXT 15// LNEXT (^V) \026 下一行字符
#define VEOL2 16 // EOL2 (\0) \0 行结束字符2 

/* c_iflag bits */

/**
 * c_iflag输入模式标志的比特位
 * 
 */
#define IGNBRK	0000001 // 输入时忽略BREAK条件
#define BRKINT	0000002 // BREAK发生时产生SIGINT信号
#define IGNPAR	0000004 // 忽略奇偶校验出错的字符
#define PARMRK	0000010 // 标记奇偶校验出错
#define INPCK	0000020 // 允许奇偶校验
#define ISTRIP	0000040 // 屏蔽字符第8位
#define INLCR	0000100 // 输入时将换行符NL映射成回车符CR
#define IGNCR	0000200 // 忽略回车符CR
#define ICRNL	0000400 // 输入时将回车符CR映射成换行符NL
#define IUCLC	0001000 // 输入时将大写字符转换成小写字符
#define IXON	0002000 // 允许开始/停止(XON/XOFF)的输出控制
#define IXANY	0004000 // 允许任何字符重启输出
#define IXOFF	0010000 // 允许开始/停止(XON/XOFF)的输入控制
#define IMAXBEL	0020000 // 输入队列满时响铃

/* c_oflag bits */

/**
 * c_oflag输出模式标志的比特位
 * 
 */
#define OPOST	0000001 // 执行输出处理
#define OLCUC	0000002 // 输出时将小写字符转换成大写字符
#define ONLCR	0000004 // 输出时将换行符NL映射成回车符CR
#define OCRNL	0000010 // 输出时将回车符CR映射成换行符NL
#define ONOCR	0000020 // 在0列不输出回车符CR
#define ONLRET	0000040 // 换行符CR执行回车符的功能
#define OFILL	0000100 // 延迟时使用填充字符而不是时间延迟
#define OFDEL	0000200 // 填充字符是ASCII码DEL，如果未设置，则使用ASCII码的NULL
#define NLDLY	0000400 // 选择换行延迟
#define   NL0	0000000 // 换行延迟类型0
#define   NL1	0000400 // 换行延迟类型1
#define CRDLY	0003000 // 选择回车延迟
#define   CR0	0000000 // 回车延迟类型0
#define   CR1	0001000 // 回车延迟类型1 
#define   CR2	0002000 // 回车延迟类型2
#define   CR3	0003000 // 回车延迟类型3
#define TABDLY	0014000 // 选择水平制表(TAB)延迟
#define   TAB0	0000000 // TAB延迟类型0
#define   TAB1	0004000 // TAB延迟类型1
#define   TAB2	0010000 // TAB延迟类型2
#define   TAB3	0014000 // TAB延迟类型3
#define   XTABS	0014000 // 将TAB转换成空格，该值表示空格数
#define BSDLY	0020000 // 选择BACKSPACE延迟
#define   BS0	0000000 // BACKSPACE延迟类型0 
#define   BS1	0020000 // BACKSPACE延迟类型1 
#define VTDLY	0040000 // 选择纵向制表(VT)延迟
#define   VT0	0000000 // VT延迟类型0
#define   VT1	0040000 // VT延迟类型1 
#define FFDLY	0040000 // 选择换页(FF)延迟
#define   FF0	0000000 // FF延迟类型0
#define   FF1	0040000 // FF延迟类型1 

/* c_cflag bit meaning */

/**
 * c_cflag控制模式标志的符号常数
 * 
 */
#define CBAUD	0000017 // 传输速率位的屏蔽码
#define  B0	0000000		/* hang up */ // 挂断线路
#define  B50	0000001 // 波特率50
#define  B75	0000002 // 波特率75
#define  B110	0000003 // 波特率110
#define  B134	0000004 // 波特率134
#define  B150	0000005 // 波特率150
#define  B200	0000006 // 波特率200
#define  B300	0000007 // 波特率300
#define  B600	0000010 // 波特率600
#define  B1200	0000011 // 波特率1200
#define  B1800	0000012 // 波特率1800
#define  B2400	0000013 // 波特率2400
#define  B4800	0000014 // 波特率4800
#define  B9600	0000015 // 波特率9600
#define  B19200	0000016 // 波特率19200
#define  B38400	0000017 // 波特率38400
#define EXTA B19200 // 扩展波特率A
#define EXTB B38400 // 扩展波特率B
#define CSIZE	0000060 // 字符位宽度屏蔽码
#define   CS5	0000000 // 每字符5位
#define   CS6	0000020 // 每字符6位
#define   CS7	0000040 // 每字符7位
#define   CS8	0000060 // 每字符8位
#define CSTOPB	0000100 // 设置2个停止位，而不是1个
#define CREAD	0000200 // 允许接收
#define CPARENB	0000400 // 开启输出时产生奇偶位，输入时进行奇偶校验
#define CPARODD	0001000 // 输入/输出校验是奇校验
#define HUPCL	0002000 // 最后进程关闭时挂断
#define CLOCAL	0004000 // 忽略调制解调器(modem)控制线路
#define CIBAUD	03600000		/* input baud rate (not used) */ //输入波特率（未使用）
#define CRTSCTS	020000000000		/* flow control */ // 流控制

#define PARENB CPARENB
#define PARODD CPARODD

/* c_lflag bits */

/**
 * c_lflag本地模式标志的符号常数
 * 
 */
#define ISIG	0000001 // 当收到INTR，QUIT，SUSP或DSUSP时候产生相应的信号
#define ICANON	0000002 // 开启规范模式（熟模式）
#define XCASE	0000004 // 如果设置了ICANON，则终端是大写字符的
#define ECHO	0000010 // 回显输入字符
#define ECHOE	0000020 // 如果设置了ICANON，则ERASE/WERASE将擦除前一个字符/单词
#define ECHOK	0000040 // 如果设置了ICANON，则KILL字符将擦除当前行
#define ECHONL	0000100 // 如果设置了ICANON，则即使ECHO没有开启，也回显NL字符
#define NOFLSH	0000200 // 当生成SIGINT和SIGQUIT时不刷新输入输出队列，当生成SIGSUSP信号时，刷新输入队列
#define TOSTOP	0000400 // 后台进程试图写自己的控制终端，发送SIGTTOU信号到后台进程的进程组，
#define ECHOCTL	0001000 // 如果设置了ECHO，则除TAB，NL，START和STOP以外的ASCII控制信号将被回显成^X式样，X值是控制符+0x40
#define ECHOPRT	0002000 // 如果设置了ICANON和ECHO，字符在被擦除时将被显示
#define ECHOKE	0004000 // 如果设置了ICANON，则通过KILL擦除的行上的所有字符将被显示
#define FLUSHO	0010000 // 输出被刷新。通过键入DISCARD字符，该标志被翻转
#define PENDIN	0040000 // 当下一个字符是读时，输入队列的所有字符将被重显
#define IEXTEN	0100000 // 开始实现时定义的输入处理

/* modem lines */

/**
 * modem 线路信号符号常数
 * 
 */
#define TIOCM_LE	0x001 // 线路允许
#define TIOCM_DTR	0x002 // 数据终端就绪
#define TIOCM_RTS	0x004 // 请求发送
#define TIOCM_ST	0x008 // 串行数据发送
#define TIOCM_SR	0x010 // 串行数据接收
#define TIOCM_CTS	0x020 // 清除发送
#define TIOCM_CAR	0x040 // 载波监测
#define TIOCM_RNG	0x080 // 响铃指示
#define TIOCM_DSR	0x100 // 数据设备就绪
#define TIOCM_CD	TIOCM_CAR 
#define TIOCM_RI	TIOCM_RNG

/* tcflow() and TCXONC use these */

/**
 * tcflow() 和 TCXONC 使用的常数
 */
#define	TCOOFF		0 // 挂起输出
#define	TCOON		1 // 重启被挂起的输出
#define	TCIOFF		2 // 系统传输一个STOP字符，使设备停止向系统传输数据
#define	TCION		3 // 系统传输一个START字符，使设备开始向系统传输输出

/* tcflush() and TCFLSH use these */

/**
 * tcflush() 和 TCFLSH 使用的常数
 * 
 */
#define	TCIFLUSH	0 // 清接收到的数据但不读
#define	TCOFLUSH	1 // 清已写的数据但不传送
#define	TCIOFLUSH	2 // 清接收到的数据但不读，清已写的数据但不传送

/* tcsetattr uses these */

/**
 * tcsetattr() 使用的常数
 */
#define	TCSANOW		0 // 改变立即发生
#define	TCSADRAIN	1 // 改变在所有已写的输出被传输之后发生
#define	TCSAFLUSH	2 // 改变在所有已写的输出被传输之后发生，并且在所有接收到但还没有读的数据被丢弃之后发生

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
