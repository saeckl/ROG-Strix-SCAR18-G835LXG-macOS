#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
int main(void)
{
    io_service_t expert = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleACPIPlatformExpert"));
    if (!expert) {
        puts("ACPI platform service unavailable");
        return 1;
    }
    CFTypeRef tables = IORegistryEntryCreateCFProperty(expert, CFSTR("ACPI Tables"), kCFAllocatorDefault, 0);
    IOObjectRelease(expert);
    if (!tables) {
        puts("AppleACPIPlatformExpert does not publish ACPI Tables");
        return 2;
    }
    if (CFGetTypeID(tables) != CFDictionaryGetTypeID()) {
        CFRelease(tables);
        return 3;
    }
    CFDataRef dsdt = CFDictionaryGetValue(tables, CFSTR("DSDT"));
    if (!dsdt || CFGetTypeID(dsdt) != CFDataGetTypeID()) {
        CFRelease(tables);
        return 4;
    }
    FILE *out = fopen("work/tas2781/live-dsdt.aml", "wb");
    if (!out) {
        CFRelease(tables);
        return 5;
    }
    size_t length = (size_t) CFDataGetLength(dsdt);
    size_t written = fwrite(CFDataGetBytePtr(dsdt), 1, length, out);
    int close_status = fclose(out);
    CFRelease(tables);
    return written != length || close_status;
}
