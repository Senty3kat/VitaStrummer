#ifndef STUB_PROCESSMGR_H
#define STUB_PROCESSMGR_H
#include <psp2/types.h>
int sceKernelExitProcess(int res);
SceUInt64 sceKernelGetProcessTimeWide(void);
#endif
