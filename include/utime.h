#ifndef _UTIME_H
#define _UTIME_H

#include <sys/types.h>	/* I know - shouldn't do this, but .. */

struct utimbuf {
        time_t actime; // 文件访问时间：UNIX时间格式
        time_t modtime; // 文件修改时间：UNIX时间格式
};

extern int utime(const char *filename, struct utimbuf *times);

#endif
