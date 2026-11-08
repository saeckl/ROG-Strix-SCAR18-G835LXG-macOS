/* SPDX-License-Identifier: MIT
 * Deliberately small API doubles for logic tests on Linux. NOT an Apple SDK. */
#ifndef APPLE_API_MOCK_H
#define APPLE_API_MOCK_H
#include <stdint.h>
#include <stddef.h>
typedef int kern_return_t;
typedef uint32_t io_connect_t,io_service_t,io_object_t,natural_t;
typedef void *CFTypeRef;
typedef const char *CFStringRef;
typedef void *CFRunLoopTimerRef;
typedef void *IONotificationPortRef;
typedef struct {long version;void *info;void *retain,*release,*copyDescription;} CFRunLoopTimerContext;
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define kIOMainPortDefault 0
#define kCFRunLoopCommonModes ((void*)1)
#define kIOPSACPowerValue "AC Power"
#define CFSTR(x) (x)
#define kIOMessageCanSystemSleep 10u
#define kIOMessageSystemWillSleep 11u
#define kIOMessageSystemHasPoweredOn 12u
kern_return_t IOConnectCallMethod(io_connect_t,uint32_t,const uint64_t*,uint32_t,const void*,size_t,uint64_t*,uint32_t*,void*,size_t*);
kern_return_t IOConnectCallScalarMethod(io_connect_t,uint32_t,const uint64_t*,uint32_t,uint64_t*,uint32_t*);
void *IOServiceMatching(const char*);
io_service_t IOServiceGetMatchingService(uint32_t,void*);
kern_return_t IOServiceOpen(io_service_t,uint32_t,uint32_t,io_connect_t*);
int IOObjectRelease(io_object_t);
int IOServiceClose(io_connect_t);
uint32_t mach_task_self(void);
CFTypeRef IOPSCopyPowerSourcesInfo(void);
CFStringRef IOPSGetProvidingPowerSourceType(CFTypeRef);
int CFEqual(const void*,const void*);
void CFRelease(const void*);
void *CFRunLoopGetCurrent(void);
void CFRunLoopStop(void*);
void CFRunLoopRun(void);
io_connect_t IORegisterForSystemPower(void*,IONotificationPortRef*,void(*)(void*,io_service_t,natural_t,void*),io_object_t*);
void *IONotificationPortGetRunLoopSource(IONotificationPortRef);
void IONotificationPortDestroy(IONotificationPortRef);
void CFRunLoopAddSource(void*,void*,void*);
void CFRunLoopAddTimer(void*,CFRunLoopTimerRef,void*);
double CFAbsoluteTimeGetCurrent(void);
CFRunLoopTimerRef CFRunLoopTimerCreate(void*,double,double,unsigned long,long,void(*)(CFRunLoopTimerRef,void*),CFRunLoopTimerContext*);
void CFRunLoopTimerInvalidate(CFRunLoopTimerRef);
int IOAllowPowerChange(io_connect_t,intptr_t);
int IODeregisterForSystemPower(io_object_t*);
#endif
