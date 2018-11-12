/**
 * 向硬件端口写入一个字节
 *
 * value: 欲输出的字节
 * port：端口号
 */
#define outb(value,port)                                    \
        __asm__ ("outb %%al,%%dx"::"a" (value),"d" (port))


/**
 * 从硬件端口读入一个字节
 *
 * port: 端口号
 */
#define inb(port) ({                                                    \
                        unsigned char _v;                               \
                        __asm__ volatile ("inb %%dx,%%al":"=a" (_v):"d" (port)); \
                        _v;                                             \
                })


/**
 * 带延迟地向硬件端口写入一个字节，用两个跳转语句来延迟一会
 *
 * value: 欲输出的字节
 * port：端口号
 */

//向前跳转到标号 1 处（即下一条语句) 
#define outb_p(value,port) \
__asm__ ("outb %%al,%%dx\n"                     \
        "\tjmp 1f\n" \
"1:\tjmp 1f\n" \
"1:"::"a" (value),"d" (port))

/**
 * 带延迟地从硬件端口读入一个字节
 *
 * port: 端口号
 */        
#define inb_p(port) ({                                                  \
                                             unsigned char _v;          \
                                             __asm__ volatile ("inb %%dx,%%al\n" \
                                                               "\tjmp 1f\n" \
                                                               "1:\tjmp 1f\n" \
                                                               "1:":"=a" (_v):"d" (port)); \
                                             _v;                        \
                                     })
