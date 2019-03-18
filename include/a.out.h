#ifndef _A_OUT_H
#define _A_OUT_H

#define __GNU_EXEC_MACROS__

// 第一部分：关于目标文件执行(exec)结构以及相关宏
struct exec {
        unsigned long a_magic; // 执行文件魔数
        unsigned a_text; // 代码段长度，字节数
        unsigned a_data; // 数据段长度，字节数
        unsigned a_bss; // bss段长度，字节数
        unsigned a_syms; // 符号表长度，字节数
        unsigned a_entry; // 开始执行地址
        unsigned a_trsize; // 代码重定位信息长度，字节数
        unsigned a_drsize; // 数据重定位信息长度，字节数
};

// 取exec结构中的魔数域
#ifndef N_MAGIC
#define N_MAGIC(exec) ((exec).a_magic)
#endif


#ifndef OMAGIC
// 目标文件为老旧的可执行文件（不纯的）
#define OMAGIC 0407
// 目标文件为纯可执行文件：代码段在一个只读页，数据段在另外的读写页上
#define NMAGIC 0410
// 目标文件为需要分页的可执行文件，其头结构占用开始处1K空间
#define ZMAGIC 0413
#endif /* not OMAGIC */

#ifndef N_BADMAG
// 判断魔数是否可被识别，如果不是上面定义的这三种，返回“真”
#define N_BADMAG(x)                                     \
        (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC   \
         && N_MAGIC(x) != ZMAGIC)
#endif

#define _N_BADMAG(x)                                    \
        (N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC   \
         && N_MAGIC(x) != ZMAGIC)

// exec结构末端到1024字节之间的长度
#define _N_HDROFF(x) (SEGMENT_SIZE - sizeof (struct exec))

#ifndef N_TXTOFF
// 计算“代码”部分起始偏移值
// 如果是 ZMAGIC 那么代码部分从执行文件的1024字节偏移处开始( _N_HDROFF + sizeof(struct exec) )
// 反之，直接从exec结构结尾处开始(32字节)
#define N_TXTOFF(x)                                                     \
        (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) : sizeof (struct exec))
#endif

#ifndef N_DATOFF
// 计算“数据”部分的起始偏移值：从“代码”部分末端开始
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

#ifndef N_TRELOFF
// 计算“代码重定位信息”部分的起始偏移值：从“数据”部分末端开始
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

#ifndef N_DRELOFF
// 计算“数据重定位信息”部分的起始偏移值：从“代码重定位信息”部分末端开始
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

#ifndef N_SYMOFF
// 计算“符号表”部分的起始偏移值：从“数据重定位信息”部分末端开始
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

#ifndef N_STROFF
// 计算“字符串”部分的起始偏移值：从“符号表”部分末端开始
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

/* Address of text segment in memory after it is loaded.  */
// 下面对可执行文件被加载到内存（逻辑地址空间）的位置情况进行操作
#ifndef N_TXTADDR
// 代码段加载后在内存中的地址，代码段从地址0开始执行
#define N_TXTADDR(x) 0
#endif

/* Address of data segment in memory after it is loaded.
   Note that it is up to you to define SEGMENT_SIZE
   on machines not listed here.  */
/*
 * 定义不同cpu架构下的段，页大小常量
 *
 * 注意：对于下面没有列出的cpu架构类型，需要你自己定义
 * 
 */
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE PAGE_SIZE
#endif
#ifdef	hp300
#define	PAGE_SIZE	4096
#endif
#ifdef	sony
#define	SEGMENT_SIZE	0x2000
#endif	/* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

// 对于386机器来说，页内存为4K，段大小为1K 
#define PAGE_SIZE 4096
#define SEGMENT_SIZE 1024

// 以段为大小的“进位”方式
#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

// 内存中代码段的尾地址
#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

#ifndef N_DATADDR
// 计算内存中数据段的开始地址
// 1. 如果是 OMAGIC，直接紧接在代码段之后
// 2. 反之，从代码段后面的段边界开始（1KB边界对齐）        
#define N_DATADDR(x)                                \
        (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x))     \
         : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))
#endif

/* Address of bss segment in memory after it is loaded.  */
#ifndef N_BSSADDR
// 计算内存中bss段开始地址
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif


#ifndef N_NLIST_DECLARED
struct nlist {
        union {
                char *n_name;
                struct nlist *n_next;
                long n_strx;
        } n_un;
        unsigned char n_type;
        char n_other;
        short n_desc;
        unsigned long n_value;
};
#endif

#ifndef N_UNDF
#define N_UNDF 0
#endif
#ifndef N_ABS
#define N_ABS 2
#endif
#ifndef N_TEXT
#define N_TEXT 4
#endif
#ifndef N_DATA
#define N_DATA 6
#endif
#ifndef N_BSS
#define N_BSS 8
#endif
#ifndef N_COMM
#define N_COMM 18
#endif
#ifndef N_FN
#define N_FN 15
#endif

#ifndef N_EXT
#define N_EXT 1
#endif
#ifndef N_TYPE
#define N_TYPE 036
#endif
#ifndef N_STAB
#define N_STAB 0340
#endif

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */
#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */

/* These appear as input to LD, in a .o file.  */
#define	N_SETA	0x14		/* Absolute set element symbol */
#define	N_SETT	0x16		/* Text set element symbol */
#define	N_SETD	0x18		/* Data set element symbol */
#define	N_SETB	0x1A		/* Bss set element symbol */

/* This is output from LD.  */
#define N_SETV	0x1C		/* Pointer to set vector in data area.  */

#ifndef N_RELOCATION_INFO_DECLARED

/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */

struct relocation_info
{
        /* Address (within segment) to be relocated.  */
        int r_address;
        /* The meaning of r_symbolnum depends on r_extern.  */
        unsigned int r_symbolnum:24;
        /* Nonzero means value is a pc-relative offset
           and it should be relocated for changes in its own address
           as well as for changes in the symbol or section specified.  */
        unsigned int r_pcrel:1;
        /* Length (as exponent of 2) of the field to be relocated.
           Thus, a value of 2 indicates 1<<2 bytes.  */
        unsigned int r_length:2;
        /* 1 => relocate with value of symbol.
           r_symbolnum is the index of the symbol
           in file's the symbol table.
           0 => relocate with the address of a segment.
           r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
           (the N_EXT bit may be set also, but signifies nothing).  */
        unsigned int r_extern:1;
        /* Four bits that aren't used, but when writing an object file
           it is desirable to clear them.  */
        unsigned int r_pad:4;
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */


#endif /* __A_OUT_GNU_H__ */
