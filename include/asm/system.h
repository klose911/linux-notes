/** 
 * 移动到用户态去执行任务0
 * 
 * 保护机制下可以使用调用门，中断门等手段从低特权级转移到高特权级
 * 使用模拟“中断返回”来实现从高特权级到低特权级的转换
 * 
 * 注意：这里会有堆栈切换，所以除了代码段压栈，还需要把堆栈指针压栈
 */
// 1. esp -> eax 
// 2. 堆栈段选择子(ss) 0x17 压栈
// 3. 保存的堆栈段偏移(esp)压栈
// 4. eflags 压栈
// 5. 任务0的代码段选择子(cs) 0x0 压栈
// 6. 下面标号为1 的偏移值压栈
// 7. 中断返回，等价于以特权级3跳转到下面代码标号1处开始执行
// 8. eax = 0x17, 对于代码0的ds，es，fs, ss的选择子的值都是 0x17 
// 9 ds = es = fs = gs = ss = 0x17
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

/**
 * 在段描述符表中设置段描述符
 *
 * gate_addr: 描述符所处的“地址”
 * type：“类型”域值
 * dpl：特权级
 * base: 段的基地址
 * limit: 段的限长
 *
 * 这里的实际上内核并没有使用这个宏
 */
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {          \
                *((gate_addr)+1) = ((base) & 0xff000000) |      \ 
                        (((base) & 0x00ff0000)>>16) |           \
                        ((limit) & 0xf0000) |                   \
                        ((dpl)<<13) |                           \
                        (0x00408000) |                          \
                        ((type)<<8);                            \
                *(gate_addr) = (((base) & 0x0000ffff)<<16) |    \
                        ((limit) & 0x0ffff); }

/**
 * “全局段描述符表”中设置“任务状态段(tss)”或“局部描述符(ldt)”
 *
 * n: 全局段描述符表中项n所对应的地址
 * addr: 状态段/局部表所在的地址
 * type: 描述符的标志类型字节
 *
 * %0: eax(addr地址), %1: 描述符项n的地址, %2:描述符项n的地址偏移2字节, %3: 描述符项n的地址偏移4字节,
 * %4: 描述符项n的地址偏移5字节, %5: 描述符项n的地址偏移6字节, %6: 描述符项n的地址偏移7字节
 */
// 1. 将 TSS/LDT 的长度放入描述符的长度域（第 0~1 字节）
// 2. 将基地址的低字放入描述符的第 2~3 字节
// 3. 将基地址的高字循环移入ax中
// 4. 将基地址的高字中的低字节(al)移入描述符第4字节
// 5. 将标志型字移入描述符的第5字节
// 6. 描述符号的第6字节置为 0
// 7. 将基地址的高字中的高字节(ah)移入描述符第7字节
// 8. eax 再右循环 16比特，恢复原来的eax值
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
/**
 * 在全局表中设置状态段描述符，类型是"0x89"  
 */
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x89")

/**
 * 在全局表中设置局部表描述符，类型是"0x82"  
 */
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),((int)(addr)),"0x82")

