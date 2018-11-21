/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */

/**
 * 这不是标准C库函数，仅供内核使用。因此并不关心小于1970年以前的年份，但假定一切均正常
 * 类似地，时区 TZ 这里也可以忽略掉，尽可能简单地处理问题
 */

#define MINUTE 60 // 一分钟的秒数
#define HOUR (60*MINUTE) // 一小时的秒数
#define DAY (24*HOUR) // 一天的秒数
#define YEAR (365*DAY) // 一年的秒数（非闰年！！！）

/* interestingly, we assume leap-years */
// 下面以年为界限，定义了每个月开始的秒数
// 注意：这里假定是闰年！！！
static int month[12] = {
        0,
        DAY*(31),
        DAY*(31+29), // 闰年
        DAY*(31+29+31),
        DAY*(31+29+31+30),
        DAY*(31+29+31+30+31),
        DAY*(31+29+31+30+31+30),
        DAY*(31+29+31+30+31+30+31),
        DAY*(31+29+31+30+31+30+31+31),
        DAY*(31+29+31+30+31+30+31+31+30),
        DAY*(31+29+31+30+31+30+31+31+30+31),
        DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

/**
 * 计算从1970年1月1日午夜0时起所经过的时间（秒），作为开机时间
 *
 * tm: 结构中的各字段已经在 init/main.c 中被赋值，信息取自 CMOS 
 */
long kernel_mktime(struct tm * tm)
{
        long res;
        int year;

        //计算从 1970年 开始经过的年份数
        year = tm->tm_year - 70; // 由于只用”2位数“表示年份，所以会有千年虫问题
/* magic offsets (y+1) needed to get leapyears right.*/
        // 由于 unix 年份从1970年开始，1972年是第一个闰年，从1973年开始需要计入闰年多出来的一天，以后每隔4年就是一个闰年
        // 因此闰年数的计算方法是 1 + (y - 3) / 4 = (y + 1) / 4 
        res = YEAR*year + DAY*((year+1)/4); // 计算从 1970年1月1号开始到去年12月31号的秒数
        res += month[tm->tm_mon]; // 今年到上个月为止过去的秒数，注意如果月份大于2，这里已经计入了闰年多出一天的秒数，所以下面需要判断今年是否是闰年，来决定是否做调整
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
        if (tm->tm_mon>1 && ((year+2)%4)) // tm->tm_mon > 1 => 月份大于2 ， (year + 2) 不能被 4 整除 => 今年不是闰年 
                res -= DAY; // 这时候需要减去一天 
        res += DAY*(tm->tm_mday-1); // 加上本月过去的天数的秒数时间
        res += HOUR*tm->tm_hour; // 加上今天过去的小时的秒数时间
        res += MINUTE*tm->tm_min; // 加上当前小时过去的分钟的秒数时间
        res += tm->tm_sec; // 加上当前分钟过去的秒数时间
        return res;
}
