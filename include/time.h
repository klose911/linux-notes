/**
 * 用于涉及处理时间和日期的函数，这是标准C库中的头文件之一。
 * 当前版本中主要为 init/main.c 和 kernel/mktime.c 提供 tm 结构类型，用于内核从系统 CMOS 芯片读取实时时钟信息（日历时间），从而可以设定系统开机时间
 * 系统开机时间是指从 1970年1月1日午夜0时起所经过的时间（秒），它将保存在全局变量 startup_time 中供内核所有代码读取
 *
 * 另外，这里的一些函数声明均是标准 C 库提供的函数，内核中不包括这些函数
 */
#ifndef _TIME_H
#define _TIME_H

#ifndef _TIME_T
#define _TIME_T
typedef long time_t; // 从 GMT 1970年1月1日午夜0时起所经过的时间（秒）
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#define CLOCKS_PER_SEC 100 // 系统时间滴答频率 100HZ 

typedef long clock_t; // 从进程开始执行计起的系统经过的时钟滴答数

struct tm {
        int tm_sec; // 秒数 [0, 59]
        int tm_min; // 分钟数 [0,59]
        int tm_hour; // 小时数字 [0,23]
        int tm_mday; // 一个月的天数 [0,31] 
        int tm_mon; // 一年中的月份 [0, 11]
        int tm_year; //  从 1900 年开始的年数
        int tm_wday; //  一星期中的某天 [0, 6] （星期天 = 0）
        int tm_yday; //  一年中的某天 [0, 365] 
        int tm_isdst; // 夏令时标志，正数：使用，0：没有使用，负数：无效
};

// 以下是有关时间操作的函数原型
clock_t clock(void);
time_t time(time_t * tp);
double difftime(time_t time2, time_t time1);
// 将 tm 结构表示的时间 转换成 从1970年1月1日午夜0时起所经过的时间（秒）
time_t mktime(struct tm * tp);

char * asctime(const struct tm * tp);
char * ctime(const time_t * tp);
struct tm * gmtime(const time_t *tp);
struct tm *localtime(const time_t * tp);
size_t strftime(char * s, size_t smax, const char * fmt, const struct tm * tp);
void tzset(void);

#endif
