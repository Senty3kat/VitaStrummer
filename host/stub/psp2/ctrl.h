#ifndef STUB_CTRL_H
#define STUB_CTRL_H
#include <psp2/types.h>
enum { SCE_CTRL_SELECT = 0x1, SCE_CTRL_START = 0x8, SCE_CTRL_UP = 0x10, SCE_CTRL_RIGHT = 0x20, SCE_CTRL_DOWN = 0x40,
       SCE_CTRL_LEFT = 0x80, SCE_CTRL_LTRIGGER = 0x100, SCE_CTRL_RTRIGGER = 0x200, SCE_CTRL_TRIANGLE = 0x1000,
       SCE_CTRL_CIRCLE = 0x2000, SCE_CTRL_CROSS = 0x4000, SCE_CTRL_SQUARE = 0x8000 };
typedef enum { SCE_CTRL_MODE_DIGITAL = 0, SCE_CTRL_MODE_ANALOG = 1 } SceCtrlPadInputMode;
typedef struct { SceUInt64 timeStamp; unsigned int buttons; unsigned char lx, ly, rx, ry; } SceCtrlData;
int sceCtrlSetSamplingMode(SceCtrlPadInputMode mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count);
#endif
