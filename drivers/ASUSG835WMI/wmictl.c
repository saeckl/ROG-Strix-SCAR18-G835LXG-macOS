#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "wmictl profile=quiet|dsts=ID|devs=ID.VAL\n"); return 2; }
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("ASUSG835WMI"));
    if (!s) { puts("ASUSG835WMI nicht geladen"); return 1; }
    CFStringRef v = CFStringCreateWithCString(NULL, argv[1], kCFStringEncodingASCII);
    CFDictionaryRef d = CFDictionaryCreate(NULL, (const void *[]){CFSTR("ASUSCommand")}, (const void *[]){v}, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    kern_return_t r = IORegistryEntrySetCFProperties(s, d);
    printf("%s -> 0x%x\n", argv[1], r);
    return 0;
}
