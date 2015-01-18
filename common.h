
#ifndef COMMON_H
#define COMMON_H

/* helper macros */
#define ARRAY_SIZE(arr)		(sizeof(arr)/sizeof(*(arr)))
#define FATAL(ret,...)		do { fprintf(stderr, __VA_ARGS__); exit(ret); } while (0)

#endif
