/* SPDX-License-Identifier: MIT – API doubles, not Apple's parser/SDK. */
#ifndef CPU_CF_MOCK_H
#define CPU_CF_MOCK_H
#include <stddef.h>
typedef const void *CFTypeRef,*CFDictionaryRef,*CFPropertyListRef,*CFNumberRef,*CFDateRef,*CFBooleanRef,*CFDataRef;
typedef const char *CFStringRef;
typedef long CFIndex;
#define CFSTR(x) (x)
#define kCFNumberDoubleType 1
#define kCFPropertyListImmutable 0
CFDataRef CFDataCreate(void *,const unsigned char *,CFIndex);
CFPropertyListRef CFPropertyListCreateWithData(void *,CFDataRef,int,void *,void *);
void CFRelease(CFTypeRef);
unsigned long CFGetTypeID(CFTypeRef),CFDictionaryGetTypeID(void),CFNumberGetTypeID(void),CFDateGetTypeID(void),CFBooleanGetTypeID(void);
CFTypeRef CFDictionaryGetValue(CFDictionaryRef,CFStringRef);
int CFNumberGetValue(CFNumberRef,int,void *);
int CFBooleanGetValue(CFBooleanRef);
double CFAbsoluteTimeGetCurrent(void),CFDateGetAbsoluteTime(CFDateRef);
#endif
