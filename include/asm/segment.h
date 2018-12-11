/**
 * 读取fs端段指定地址的字节
 * 
 * addr: 指定的内存偏移地址
 * 
 * %0: 返回的字节 _v , %1: 内存地址
 * 
 * 返回：fs:[addr]处的字节
 */
// _v 是一个寄存器变量，被保存在一个寄存器中，以便高效访问和存储
// get_fs_word, get_fs_long 和 get_fs_byte 类似
static inline unsigned char get_fs_byte(const char * addr)
{
	unsigned register char _v;

	__asm__ ("movb %%fs:%1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

static inline unsigned short get_fs_word(const unsigned short *addr)
{
	unsigned short _v;

	__asm__ ("movw %%fs:%1,%0":"=r" (_v):"m" (*addr));
	return _v;
}

static inline unsigned long get_fs_long(const unsigned long *addr)
{
	unsigned long _v;

	__asm__ ("movl %%fs:%1,%0":"=r" (_v):"m" (*addr)); \
	return _v;
}

/**
 * 将一个字节放在 fs段 指定的地址处
 * 
 * val: 字节值 
 * addr: 内存偏移地址 
 * 
 * 无返回值
 *
 * %0: 字节值，%1: 内存偏移地址
 */
// put_fs_word 和 put_fs_long 与 put_fs_byte 类似
static inline void put_fs_byte(char val,char *addr)
{
__asm__ ("movb %0,%%fs:%1"::"r" (val),"m" (*addr));
}

static inline void put_fs_word(short val,short * addr)
{
__asm__ ("movw %0,%%fs:%1"::"r" (val),"m" (*addr));
}

static inline void put_fs_long(unsigned long val,unsigned long * addr)
{
__asm__ ("movl %0,%%fs:%1"::"r" (val),"m" (*addr));
}

/*
 * Someone who knows GNU asm better than I should double check the followig.
 * It seems to work, but I don't know if I'm doing something subtly wrong.
 * --- TYT, 11/24/91
 * [ nothing wrong here, Linus ]
 */

static inline unsigned long get_fs() 
{
	unsigned short _v;
	__asm__("mov %%fs,%%ax":"=a" (_v):);
	return _v;
}

static inline unsigned long get_ds() 
{
	unsigned short _v;
	__asm__("mov %%ds,%%ax":"=a" (_v):);
	return _v;
}

static inline void set_fs(unsigned long val)
{
	__asm__("mov %0,%%fs"::"a" ((unsigned short) val));
}

