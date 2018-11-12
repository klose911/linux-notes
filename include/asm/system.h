#define move_to_user_mode()                     \
        __asm__ ("movl %%esp,%%eax\n\t"         \
                 "pushl $0x17\n\t"              \
                 "pushl %%eax\n\t"              \
                 "pushfl\n\t"                   \
                 "pushl $0x0f\n\t"              \
                 "pushl $1f\n\t"                \
                 "iret\n"                       \
                 "1:\tmovl $0x17,%%eax\n\t"     \
                 "movw %%ax,%%ds\n\t"           \
                 "movw %%ax,%%es\n\t"           \
                 "movw %%ax,%%fs\n\t"           \
                 "movw %%ax,%%gs"               \
                 :::"ax")

#define sti() __asm__ ("sti"::) // 开中断
#define cli() __asm__ ("cli"::) // 关中断
#define nop() __asm__ ("nop"::) // 空操作

#define iret() __asm__ ("iret"::) // 中断返回

/**
 * 设置门描述符宏：根据参数中的中断或异常处理过程地址，门描述符类型，特权级，设置门描述符
 *
 * gate_addr: 描述符地址
 * type: 描述符类型
 * dpl: 特权级
 * addr: 中断/异常处理过程地址
 *
 * %0: 由 dpl,type组成的类型标志字，%1: 描述符低4字节地址，%2: 描述符高4字节地址
 * %3: edx(程序偏移地址addr), %4: eax(高字中含有段选择符 0x8)
 */
/* 将”偏移地址低字”与“选择符”组合成“描述符低4字节” */
/* 将“类型标志字”与“偏移地址高字”组合成“描述符高4字节” */
//设置描述符地址的低4字节
// 设置描述符地址的高4字节
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
"movw %0,%%dx\n\t" \
"movl %%eax,%1\n\t" \
"movl %%edx,%2" \
: \
: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
        "o" (*((char *) (gate_addr))), \
        "o" (*(4+(char *) (gate_addr))), \
        "d" ((char *) (addr)), \
        "a" (0x00080000))

/**
 * 设置中断门函数（自动屏蔽随后的中断 IF = 0）
 *
 * n: 中断号
 * addr: 中断程序偏移地址
 *
 * 
 */
// &idt[n] 是“中断描述符表”中段号为 n 的对应项的偏移值，描述符类型为 14, 特权级为 0
#define set_intr_gate(n,addr)                                           \
                         _set_gate(&idt[n],14,0,addr)

/**
 * 设置陷阱门函数
 *
 * n: 中断号
 * addr: 中断程序偏移地址
 *
 * 
 */
 // &idt[n] 是“中断描述符表”中段号为 n 的对应项的偏移值，描述符类型为 15, 特权级为 0        
#define set_trap_gate(n,addr)                                           \
                         _set_gate(&idt[n],15,0,addr)

/**
 * 设置系统门函数
 *
 * n: 中断号
 * addr: 中断程序偏移地址
 *
 * 上面 set_trap_gate 的特权级是0，而系统门的特权级是3，可以被任何程序所调用
 * 主要是单步调试，溢出处理，边界出错
 */
 //  &idt[n] 是“中断描述符表”中段号为 n 的对应项的偏移值，描述符类型为 15, 特权级为 3        
#define set_system_gate(n,addr)                                         \
                         _set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {                  \
                                 *(gate_addr) = ((base) & 0xff000000) | \
                                         (((base) & 0x00ff0000)>>16) |  \
                                         ((limit) & 0xf0000) |          \
                                         ((dpl)<<13) |                  \
                                         (0x00408000) |                 \
                                         ((type)<<8);                   \
                                 *((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
                                         ((limit) & 0x0ffff); }

#define _set_tssldt_desc(n,addr,type)                                   \
                         __asm__ ("movw $104,%1\n\t"                    \
                                  "movw %%ax,%2\n\t"                    \
                                  "rorl $16,%%eax\n\t"                  \
                                  "movb %%al,%3\n\t"                    \
                                  "movb $" type ",%4\n\t"               \
                                  "movb $0x00,%5\n\t"                   \
                                  "movb %%ah,%6\n\t"                    \
                                  "rorl $16,%%eax"                      \
                                  ::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
                                   "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
                                 )

#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x89")
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x82")

