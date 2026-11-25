#include "android_bridge.hpp"

#if defined(__ANDROID__) || defined(ANDROID)

#include <borealis/core/logger.hpp>

#ifdef ABSOLUTE
#undef ABSOLUTE
#endif
#ifdef RELATIVE
#undef RELATIVE
#endif

#include <SDL.h>
#include <dlfcn.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>
#include <webp/decode.h>

#ifdef USE_LIBROMFS
#include <romfs/romfs.hpp>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <mutex>

namespace
{

std::string g_storagePath = ".";

std::mutex g_jniMutex;
jclass g_platformUtilsClass = nullptr;
jmethodID g_getWebPSizeMid  = nullptr;
jmethodID g_decodeWebPMid   = nullptr;
jclass g_mainActivityClass  = nullptr;
jmethodID g_consumeIntentMid = nullptr;

void clearException(JNIEnv* env)
{
    if (env && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
}

void initJniRefs()
{
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return;

    std::lock_guard<std::mutex> lock(g_jniMutex);

    if (!g_platformUtilsClass)
    {
        if (jclass cls = env->FindClass("org/libsdl/app/PlatformUtils"))
        {
            g_platformUtilsClass = static_cast<jclass>(env->NewGlobalRef(cls));
            env->DeleteLocalRef(cls);
            if (g_platformUtilsClass)
            {
                g_getWebPSizeMid = env->GetStaticMethodID(
                    g_platformUtilsClass, "getWebPSize", "([B)[I");
                clearException(env);
                g_decodeWebPMid = env->GetStaticMethodID(
                    g_platformUtilsClass, "decodeWebP", "([B)[B");
                clearException(env);
            }
        }
        clearException(env);
    }

    if (!g_mainActivityClass)
    {
        if (jclass cls = env->FindClass("com/nxtorrent/player/MainActivity"))
        {
            g_mainActivityClass = static_cast<jclass>(env->NewGlobalRef(cls));
            env->DeleteLocalRef(cls);
            if (g_mainActivityClass)
            {
                g_consumeIntentMid = env->GetStaticMethodID(
                    g_mainActivityClass, "consumePendingIntentUri",
                    "()Ljava/lang/String;");
                clearException(env);
            }
        }
        clearException(env);
    }

    static bool g_avVmSet = false;
    if (!g_avVmSet)
    {
        JavaVM* vm = nullptr;
        if (env->GetJavaVM(&vm) == JNI_OK && vm)
        {
            using AvJniSetJavaVmFn = int (*)(void*, void*);
            AvJniSetJavaVmFn setVm = reinterpret_cast<AvJniSetJavaVmFn>(
                dlsym(RTLD_DEFAULT, "av_jni_set_java_vm"));
            if (!setVm)
            {
                if (void* h = dlopen("libavcodec.so", RTLD_NOW | RTLD_NOLOAD))
                {
                    setVm = reinterpret_cast<AvJniSetJavaVmFn>(
                        dlsym(h, "av_jni_set_java_vm"));
                    dlclose(h);
                }
            }
            if (setVm)
            {
                setVm(vm, nullptr);
                g_avVmSet = true;
            }
        }
    }
}

void extractRomfsFile(const char* romPath, const char* diskPath)
{
#ifdef USE_LIBROMFS
    try
    {
        const auto& res = romfs::get(romPath);
        if (!res.valid() || res.size() == 0) return;

        struct stat st {};
        if (stat(diskPath, &st) == 0 &&
            static_cast<size_t>(st.st_size) == res.size())
            return;

        if (FILE* f = std::fopen(diskPath, "wb"))
        {
            std::fwrite(res.data(), 1, res.size(), f);
            std::fclose(f);
        }
    }
    catch (...)
    {
    }
#else
    (void)romPath;
    (void)diskPath;
#endif
}

} // namespace

void androidInitPlatform()
{
    if (const char* base = SDL_AndroidGetInternalStoragePath())
    {
        if (base[0])
        {
            g_storagePath = base;
            chdir(base);
        }
    }

    extractRomfsFile("cacert.pem", "cacert.pem");
    extractRomfsFile("font/switch_font.ttf", "subfont.ttf");

    mkdir("patterns", 0777);
    extractRomfsFile("patterns/black.png", "patterns/black.png");
    extractRomfsFile("patterns/white.png", "patterns/white.png");
    extractRomfsFile("patterns/greyscale.png", "patterns/greyscale.png");
    extractRomfsFile("patterns/gamma.png", "patterns/gamma.png");
    extractRomfsFile("patterns/colour.png", "patterns/colour.png");

    initJniRefs();
}

const char* androidStoragePath()
{
    return g_storagePath.c_str();
}

std::string androidConsumePendingIntentUri()
{
    initJniRefs();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || !g_mainActivityClass || !g_consumeIntentMid) return "";

    jstring juri = static_cast<jstring>(
        env->CallStaticObjectMethod(g_mainActivityClass, g_consumeIntentMid));
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        if (juri) env->DeleteLocalRef(juri);
        return "";
    }
    if (!juri) return "";

    const char* utf = env->GetStringUTFChars(juri, nullptr);
    std::string out = utf ? utf : "";
    if (utf) env->ReleaseStringUTFChars(juri, utf);
    env->DeleteLocalRef(juri);
    return out;
}

extern "C" {

WEBP_EXTERN int WebPGetInfo(const uint8_t* data, size_t data_size,
                            int* width, int* height)
{
    if (!data || data_size == 0 || data_size > 32u * 1024u * 1024u)
        return 0;

    initJniRefs();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || !g_platformUtilsClass || !g_getWebPSizeMid)
        return 0;

    jbyteArray input = env->NewByteArray(static_cast<jsize>(data_size));
    if (!input)
    {
        clearException(env);
        return 0;
    }
    env->SetByteArrayRegion(input, 0, static_cast<jsize>(data_size),
                            reinterpret_cast<const jbyte*>(data));

    jintArray dims = static_cast<jintArray>(
        env->CallStaticObjectMethod(g_platformUtilsClass, g_getWebPSizeMid, input));
    env->DeleteLocalRef(input);
    if (env->ExceptionCheck() || !dims)
    {
        clearException(env);
        if (dims) env->DeleteLocalRef(dims);
        return 0;
    }

    jsize len = env->GetArrayLength(dims);
    int ok    = 0;
    if (len >= 2)
    {
        jint buf[2] = { 0, 0 };
        env->GetIntArrayRegion(dims, 0, 2, buf);
        if (!env->ExceptionCheck() && buf[0] > 0 && buf[1] > 0)
        {
            if (width) *width = buf[0];
            if (height) *height = buf[1];
            ok = 1;
        }
        clearException(env);
    }
    env->DeleteLocalRef(dims);
    return ok;
}

WEBP_EXTERN uint8_t* WebPDecodeRGBA(const uint8_t* data, size_t data_size,
                                    int* width, int* height)
{
    if (!data || data_size == 0 || data_size > 32u * 1024u * 1024u)
        return nullptr;

    initJniRefs();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || !g_platformUtilsClass || !g_decodeWebPMid)
        return nullptr;

    jbyteArray input = env->NewByteArray(static_cast<jsize>(data_size));
    if (!input)
    {
        clearException(env);
        return nullptr;
    }
    env->SetByteArrayRegion(input, 0, static_cast<jsize>(data_size),
                            reinterpret_cast<const jbyte*>(data));

    jbyteArray decoded = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(g_platformUtilsClass, g_decodeWebPMid, input));
    env->DeleteLocalRef(input);
    if (env->ExceptionCheck() || !decoded)
    {
        clearException(env);
        if (decoded) env->DeleteLocalRef(decoded);
        return nullptr;
    }

    jsize totalLen = env->GetArrayLength(decoded);
    if (totalLen <= 8)
    {
        env->DeleteLocalRef(decoded);
        return nullptr;
    }

    uint8_t hdr[8];
    env->GetByteArrayRegion(decoded, 0, 8, reinterpret_cast<jbyte*>(hdr));
    if (env->ExceptionCheck())
    {
        clearException(env);
        env->DeleteLocalRef(decoded);
        return nullptr;
    }

    uint32_t w = static_cast<uint32_t>(hdr[0]) |
                 (static_cast<uint32_t>(hdr[1]) << 8) |
                 (static_cast<uint32_t>(hdr[2]) << 16) |
                 (static_cast<uint32_t>(hdr[3]) << 24);
    uint32_t h = static_cast<uint32_t>(hdr[4]) |
                 (static_cast<uint32_t>(hdr[5]) << 8) |
                 (static_cast<uint32_t>(hdr[6]) << 16) |
                 (static_cast<uint32_t>(hdr[7]) << 24);

    if (w == 0 || h == 0 || w > 4096 || h > 4096)
    {
        env->DeleteLocalRef(decoded);
        return nullptr;
    }

    size_t rgbaLen = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (static_cast<size_t>(totalLen) != 8u + rgbaLen)
    {
        env->DeleteLocalRef(decoded);
        return nullptr;
    }

    uint8_t* rgba = static_cast<uint8_t*>(std::malloc(rgbaLen));
    if (!rgba)
    {
        env->DeleteLocalRef(decoded);
        return nullptr;
    }

    env->GetByteArrayRegion(decoded, 8, static_cast<jsize>(rgbaLen),
                            reinterpret_cast<jbyte*>(rgba));
    bool failed = env->ExceptionCheck();
    clearException(env);
    env->DeleteLocalRef(decoded);
    if (failed)
    {
        std::free(rgba);
        return nullptr;
    }

    if (width) *width = static_cast<int>(w);
    if (height) *height = static_cast<int>(h);
    return rgba;
}

WEBP_EXTERN void WebPFree(void* ptr)
{
    std::free(ptr);
}

} // extern "C"

#else

void androidInitPlatform() {}

const char* androidStoragePath() { return "./appdata"; }

std::string androidConsumePendingIntentUri() { return ""; }

#endif
