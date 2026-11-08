#include <mach/mach_types.h>
extern kern_return_t NVPCFBridge_kern_start(kmod_info_t *, void *);
extern kern_return_t NVPCFBridge_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(as.saeckl.NVPCFBridge, "1.0.0", NVPCFBridge_kern_start, NVPCFBridge_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = NVPCFBridge_kern_start;
__private_extern__ kmod_stop_func_t *_antimain = NVPCFBridge_kern_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
