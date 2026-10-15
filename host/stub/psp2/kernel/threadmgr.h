#ifndef STUB_THREADMGR_H
#define STUB_THREADMGR_H
#include <psp2/types.h>
typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);
typedef struct SceKernelThreadOptParam SceKernelThreadOptParam;
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority,
                             SceSize stackSize, SceUInt attr, int cpuAffinityMask,
                             const SceKernelThreadOptParam *option);
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout);
int sceKernelDeleteThread(SceUID thid);
int sceKernelDelayThread(SceUInt delay);
SceUInt64 sceKernelGetSystemTimeWide(void);
#endif
