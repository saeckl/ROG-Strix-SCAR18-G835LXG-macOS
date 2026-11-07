// Sendet einen Befehl an TAS2781DirectI2C und gibt das Ergebnis aus.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "Usage: direct-control status|reset|init|powerup|shutdown|atten=NN|autooff=SS|dump=BB.PP|wr=CH.BB.PP.RR.VV|verb=HEX|coefdump|codecdump\n"); return 2; }
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("TAS2781DirectI2C"));
    if (!s) { fputs("TAS2781DirectI2C is not loaded.\n", stderr); return 1; }
    CFStringRef a = CFStringCreateWithCString(NULL, argv[1], kCFStringEncodingASCII);
    const void *k[] = {CFSTR("TAS2781Command")}, *v[] = {a};
    CFDictionaryRef d = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    kern_return_t r = IORegistryEntrySetCFProperties(s, d);
    CFRelease(d); CFRelease(a); IOObjectRelease(s);
    printf("%s -> %#x\n", argv[1], r);
    return r == KERN_SUCCESS ? 0 : 1;
}
