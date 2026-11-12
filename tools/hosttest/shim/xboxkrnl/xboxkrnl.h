#ifndef FAKE_XBOXKRNL_H
#define FAKE_XBOXKRNL_H
#include <windows.h>
void KeQuerySystemTime(LARGE_INTEGER *t);
unsigned long long KeQueryPerformanceCounter(void);
#endif
