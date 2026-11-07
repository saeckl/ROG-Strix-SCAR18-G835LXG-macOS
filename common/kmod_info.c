// kmod_info block that Xcode normally generates for a kext.
// KEXT_ID and KEXT_VERSION are passed in by build.sh.
#include <mach/mach_types.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

#define KMOD_DECL_EXPANDED(id, ver, start, stop) KMOD_EXPLICIT_DECL(id, ver, start, stop)
__attribute__((visibility("default"))) KMOD_DECL_EXPANDED(KEXT_ID, KEXT_VERSION, _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
