#ifndef STUB_FCNTL_H
#define STUB_FCNTL_H
#include <psp2/types.h>
int sceIoRemove(const char *file);
int sceIoRename(const char *oldname, const char *newname);
#endif
