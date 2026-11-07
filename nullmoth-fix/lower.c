#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef int (*lower_ex_t)(const void*, size_t, uint32_t, uint8_t**, size_t*, uint32_t*, char*, size_t);
typedef void (*free_t)(uint8_t*, size_t);
int main(int c, char **v) {
  void *h = dlopen("/Library/GPUBundles/NVMTLDriver.bundle/Contents/MacOS/libnvmtl_translate.dylib", RTLD_NOW);
  if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
  lower_ex_t f = (lower_ex_t)dlsym(h, "nvmtl_lower_buffer_addresses_ex"); free_t fr = (free_t)dlsym(h, "nvmtl_translate_free");
  FILE *in = fopen(v[1], "rb"); fseek(in, 0, 2); long n = ftell(in); rewind(in); uint8_t *b = malloc(n); fread(b, 1, n, in); fclose(in);
  uint32_t flags = (uint32_t)strtoul(v[3], 0, 0), info[4] = {0}; uint8_t *out = 0; size_t on = 0; char err[512] = {0};
  int r = f(b, n, flags, &out, &on, info, err, sizeof err);
  printf("rc=%d out=%zu info=%u %u %u %u err=%s\n", r, on, info[0], info[1], info[2], info[3], err);
  if (!r) { FILE *o = fopen(v[2], "wb"); fwrite(out, 1, on, o); fclose(o); fr(out, on); }
  return r;
}
