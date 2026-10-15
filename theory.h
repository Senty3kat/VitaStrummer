#ifndef STUB_TOUCH_H
#define STUB_TOUCH_H
#include <psp2/types.h>
#define SCE_TOUCH_PORT_FRONT 0
typedef enum { SCE_TOUCH_SAMPLING_STATE_STOP = 0, SCE_TOUCH_SAMPLING_STATE_START = 1 } SceTouchSamplingState;
typedef struct { SceUInt8 id; SceUInt8 force; SceUInt16 x; SceUInt16 y; SceUInt8 reserved[8]; SceUInt16 info; } SceTouchReport;
typedef struct { SceUInt64 timeStamp; SceUInt32 status; SceUInt32 reportNum; SceTouchReport report[8]; } SceTouchData;
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state);
int sceTouchPeek(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs);
#endif
