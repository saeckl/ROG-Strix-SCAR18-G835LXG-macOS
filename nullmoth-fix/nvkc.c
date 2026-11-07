// nvkc: compile SPIR-V compute modules with NullMoth's NVK directly (no Metal), to reproduce NAK failures.
// usage: nvkc file.spv [file2.spv ...]   -> prints OK / FAIL per module
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*icd_gipa_t)(VkInstance, const char *);
static icd_gipa_t gipa;
#define IP(name) PFN_##name p##name = (PFN_##name)gipa(inst, #name)

typedef struct { uint32_t set, binding, type, count, runtime; } Bind;

static uint32_t *readf(const char *p, size_t *nw)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, 2); long n = ftell(f); rewind(f);
    uint32_t *w = malloc(n); fread(w, 1, n, f); fclose(f); *nw = n / 4; return w;
}

// Collect (set, binding, descriptor type) from the module.
static int bindings(const uint32_t *w, size_t nw, Bind *out, int max)
{
    uint32_t bound = w[3];
    uint32_t *set = calloc(bound, 4), *bnd = calloc(bound, 4), *has = calloc(bound, 4);
    uint32_t *op = calloc(bound, 4), **args = calloc(bound, sizeof(void *));
    uint32_t *block = calloc(bound, 4), *bufblock = calloc(bound, 4);
    int n = 0;
    for (size_t i = 5; i < nw;) {
        uint32_t len = w[i] >> 16, o = w[i] & 0xffff; const uint32_t *a = w + i + 1;
        if (o == 71) { // OpDecorate
            if (a[1] == 34) { set[a[0]] = a[2]; has[a[0]] |= 1; }
            if (a[1] == 33) { bnd[a[0]] = a[2]; has[a[0]] |= 2; }
            if (a[1] == 2) block[a[0]] = 1;
            if (a[1] == 3) bufblock[a[0]] = 1;
        }
        if (o >= 19 && o <= 39 && len > 1 && a[0] < bound) { op[a[0]] = o; args[a[0]] = (uint32_t *)a; }
        if (o == 59 && (has[a[1]] & 3) == 3 && n < max) { // OpVariable type id sc
            uint32_t pt = a[0], sc = a[2], t = args[pt] ? args[pt][2] : 0, count = 1, runtime = 0;
            if (op[t] == 28) { // array
                uint32_t lenid = args[t][2]; count = 1;
                // literal constant lookup skipped: use 64
                count = 64; t = args[t][1];
            } else if (op[t] == 29) { runtime = 1; count = 4096; t = args[t][1]; }
            uint32_t dt = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if (sc == 12) dt = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            else if (sc == 2) dt = bufblock[t] ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            else if (sc == 0) {
                if (op[t] == 26) dt = VK_DESCRIPTOR_TYPE_SAMPLER;
                else if (op[t] == 27) dt = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                else if (op[t] == 25) {
                    uint32_t dim = args[t][2], sampled = args[t][6];
                    if (dim == 5) dt = sampled == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                    else dt = sampled == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                } else if (op[t] == 5341) dt = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            }
            out[n++] = (Bind){ set[a[1]], bnd[a[1]], dt, count, runtime };
        }
        i += len ? len : 1;
    }
    return n;
}

int main(int argc, char **argv)
{
    void *h = dlopen("/Library/GPUBundles/nvmtl/libvulkan_nouveau.dylib", RTLD_NOW);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    gipa = (icd_gipa_t)dlsym(h, "vk_icdGetInstanceProcAddr");
    VkInstance inst = NULL;
    PFN_vkCreateInstance pCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "nvkc", 1, "nvkc", 1, VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai };
    if (pCreateInstance(&ici, NULL, &inst)) { puts("vkCreateInstance failed"); return 1; }
    IP(vkEnumeratePhysicalDevices); IP(vkGetPhysicalDeviceFeatures2); IP(vkCreateDevice); IP(vkGetDeviceProcAddr);
    IP(vkEnumerateDeviceExtensionProperties); IP(vkGetPhysicalDeviceProperties);
    uint32_t pc = 1; VkPhysicalDevice pd; pvkEnumeratePhysicalDevices(inst, &pc, &pd);
    if (!pc) { puts("no device"); return 1; }
    VkPhysicalDeviceProperties props; pvkGetPhysicalDeviceProperties(pd, &props);
    // enable every supported 1.1/1.2/1.3 feature plus extension features commonly needed
    VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
    VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &f12 };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f11 };
    pvkGetPhysicalDeviceFeatures2(pd, &f2);
    f2.features.robustBufferAccess = VK_FALSE;
    uint32_t ne = 0; pvkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL);
    VkExtensionProperties *ep = calloc(ne, sizeof *ep); pvkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ep);
    const char *want[] = { "VK_KHR_shader_float_controls2", "VK_EXT_shader_atomic_float", "VK_KHR_shader_relaxed_extended_instruction",
                           "VK_EXT_mutable_descriptor_type", "VK_EXT_descriptor_indexing", "VK_KHR_cooperative_matrix",
                           "VK_KHR_shader_subgroup_uniform_control_flow", "VK_KHR_shader_maximal_reconvergence",
                           "VK_KHR_shader_expect_assume", "VK_KHR_shader_quad_control", "VK_EXT_shader_image_atomic_int64",
                           "VK_KHR_shader_atomic_int64", "VK_KHR_ray_query", "VK_KHR_acceleration_structure",
                           "VK_KHR_deferred_host_operations", "VK_KHR_pipeline_robustness", "VK_EXT_pipeline_robustness" };
    const char *en[32]; uint32_t nen = 0;
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++)
        for (uint32_t j = 0; j < ne; j++) if (!strcmp(ep[j].extensionName, want[i])) { en[nen++] = want[i]; break; }
    float prio = 1;
    VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qi, 0, NULL, nen, en, NULL };
    VkDevice dev; VkResult r = pvkCreateDevice(pd, &dci, NULL, &dev);
    if (r) { printf("vkCreateDevice -> %d\n", r); return 1; }
#define DP(name) PFN_##name p##name = (PFN_##name)pvkGetDeviceProcAddr(dev, #name)
    DP(vkCreateShaderModule); DP(vkCreateDescriptorSetLayout); DP(vkCreatePipelineLayout); DP(vkCreateComputePipelines);
    DP(vkDestroyPipeline); DP(vkDestroyShaderModule);
    fprintf(stderr, "device: %s\n", props.deviceName);
    int fails = 0;
    for (int k = 1; k < argc; k++) {
        size_t nw; uint32_t *w = readf(argv[k], &nw);
        if (!w || nw < 5 || w[0] != 0x07230203) { printf("%s: not SPIR-V\n", argv[k]); continue; }
        Bind b[256]; int nb = bindings(w, nw, b, 256);
        VkDescriptorSetLayout sl[8] = { 0 }; uint32_t maxset = 0;
        for (int i = 0; i < nb; i++) if (b[i].set + 1 > maxset) maxset = b[i].set + 1;
        for (uint32_t s = 0; s < maxset; s++) {
            VkDescriptorSetLayoutBinding lb[256]; VkDescriptorBindingFlags fl[256]; uint32_t n = 0;
            for (int i = 0; i < nb; i++) if (b[i].set == s) {
                int dup = 0; for (uint32_t j = 0; j < n; j++) if (lb[j].binding == b[i].binding) dup = 1;
                if (dup) continue;
                lb[n] = (VkDescriptorSetLayoutBinding){ b[i].binding, b[i].type, b[i].count, VK_SHADER_STAGE_ALL, NULL };
                fl[n] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT; n++;
            }
            VkDescriptorSetLayoutBindingFlagsCreateInfo bf = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, NULL, n, fl };
            VkDescriptorSetLayoutCreateInfo lci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &bf, 0, n, lb };
            if (pvkCreateDescriptorSetLayout(dev, &lci, NULL, &sl[s])) { printf("%s: set layout %u failed\n", argv[k], s); }
        }
        VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, props.limits.maxPushConstantsSize };
        VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, maxset, sl, 1, &pcr };
        VkPipelineLayout pl; pvkCreatePipelineLayout(dev, &plci, NULL, &pl);
        VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, nw * 4, w };
        VkShaderModule mod; r = pvkCreateShaderModule(dev, &smi, NULL, &mod);
        if (r) { printf("%s: shader module -> %d\n", argv[k], r); fails++; continue; }
        VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, NULL, 0,
            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", NULL }, pl, VK_NULL_HANDLE, -1 };
        VkPipeline p; r = pvkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &p);
        printf("%s: %s (%d), %d bindings\n", argv[k], r ? "FAIL" : "OK", r, nb); fflush(stdout);
        if (r) fails++; else pvkDestroyPipeline(dev, p, NULL);
        pvkDestroyShaderModule(dev, mod, NULL); free(w);
    }
    return fails;
}
