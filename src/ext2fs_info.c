#include <mach/mach_types.h>
 
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__private_extern__ kern_return_t ext2fs_start(kmod_info_t *ki, void *data);
__private_extern__ kern_return_t ext2fs_stop(kmod_info_t *ki, void *data);
 
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(net.sourceforge.ext2fs.fs.ext2, "1.4", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = ext2fs_start;
__private_extern__ kmod_stop_func_t *_antimain = ext2fs_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__ ;
