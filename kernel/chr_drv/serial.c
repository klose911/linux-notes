/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h> 
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

#define WAKEUP_CHARS (TTY_BUF_SIZE/4) // 当写队列缓冲区含有WAKEUP_CHARS(256)的字符时候就开始发送，貌似这里没用到

extern void rs1_interrupt(void); // 串行口1的中断处理入口(rs_io.s)
extern void rs2_interrupt(void); // 串行口2的中断处理入口(rs_io.s)

/*
 * 初始化串行口
 *
 * port: 串行口基础端口，0x3f8或0x2f8
 * 
 */
static void init(int port)
{
        //允许访问两个除数锁存寄存器LSB和MSB，这必须设置线路控制寄存器LCR的第8位DLAB = 1
        // 把0x80（第8位为1）写入LCR寄存器(port+3)
        outb_p(0x80,port+3);	/* set DLAB of line control reg */
        // 设置波特率为2400：因此把0x30(24)写入LSB，把0x00(00)写入MSB(port+1) 
        outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */
        outb_p(0x00,port+1);	/* MS of divisor */
        // 复位LCR中的DLAB位为0
        outb_p(0x03,port+3);	/* reset DLAB */
        // 设置modem运行在高效的中断方式下：把0x0b(0b1011)写入MCR寄存器(port+4)
        // 位0：请求发送RTS引脚有效
        // 位1：数据中断就绪DTR引脚有效
        // 位2：OUT2置位，INTRPT引脚到8259A的电路，modem运行在中断方式
        outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */
        // 设置中断允许标志：把0x0d(1101)写入中断允许寄存器IER(port+1)
        // 位0：允许已接受到数据发出中断
        // 位1：允许发送保存寄存器为空时发出中断
        // 位2：允许接受线路出错时发出中断
        // 位3：允许modem状态变化时发出中断
        outb_p(0x0d,port+1);	/* enable all intrs but writes */
        // 通过读取THR寄存器来复位：这里无法理解？？？
        (void)inb(port);	/* read data port to reset things (?) */
}

/**
 * 初始化两个串行接口的中断入口
 * 
 */
void rs_init(void)
{
        set_intr_gate(0x24,rs1_interrupt); // 设置串口1的中断处理程序为 rs1_interrupt（IRQ4信号）
        set_intr_gate(0x23,rs2_interrupt); // 设置串口2的中断处理程序为 rs2_interrupt（IRQ3信号）
        init(tty_table[1].read_q.data); // 初始化串口1的寄存器状态（data域保存的是串行端口基地址）
        init(tty_table[2].read_q.data); // 初始化串口2的寄存器状态
        outb(inb_p(0x21)&0xE7,0x21); // 允许主8259A芯片响应IRQ3和IRQ4中断请求
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */

/**
 * 发送数据到终端对应的串行端口
 *
 * tty: 串行中断，其中data是串行端口的基地址
 *
 * 实际上只是开启了允许“发送保持寄存器”为空时发送中断的标志
 *
 * 此后一旦“发送保存寄存器”THR为空时，UART就会产生中断请求
 * 相应中断请求的处理过程中，会从写队列读取字符，并输出到THR寄存器
 * 一旦UART把字符发送出去了，则THR又变为空，则再次发出中断请求
 * 如此循环，直到写队列为空时候，则禁止“发送保持寄存器”为空时发送中断的标志
 * 
 */
void rs_write(struct tty_struct * tty)
{
        cli();
        if (!EMPTY(tty->write_q)) // 判断写队列缓冲是否为空，
                // 不为空则开启允许“发送保持寄存器”为空时发送中断的标志
                // 中断允许寄存器IER(data+1), | 0x02 : 位1代表允许“发送保持寄存器”为空时发送中断
                outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
        sti();
}
