/*
    Copyright 2023 xfangfang

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <strings.h>
#include <arpa/inet.h>
#include <jni.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/platforms/android/android_platform.hpp>

namespace brls
{

    static inline void clearJniException(JNIEnv* env) {
        if (env && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    bool static getPlatformBool(const std::string& method, bool defaultValue = false) {
        auto env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr)
            return defaultValue;

        jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
        if (env->ExceptionCheck() || utilsClass == nullptr) {
            clearJniException(env);
            if (utilsClass) env->DeleteLocalRef(utilsClass);
            return defaultValue;
        }
        jmethodID jmethod = env->GetStaticMethodID(utilsClass, method.c_str(), "()Z");
        if (env->ExceptionCheck() || jmethod == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return defaultValue;
        }
        jboolean value = env->CallStaticBooleanMethod(utilsClass, jmethod);
        bool failed = env->ExceptionCheck();
        clearJniException(env);
        env->DeleteLocalRef(utilsClass);
        return failed ? defaultValue : (bool)value;
    }

    int static getPlatformInt(const std::string& method, int defaultValue = 100) {
        auto env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr)
            return defaultValue;

        jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
        if (env->ExceptionCheck() || utilsClass == nullptr) {
            clearJniException(env);
            if (utilsClass) env->DeleteLocalRef(utilsClass);
            return defaultValue;
        }
        jmethodID jmethod = env->GetStaticMethodID(utilsClass, method.c_str(), "()I");
        if (env->ExceptionCheck() || jmethod == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return defaultValue;
        }
        jint value = env->CallStaticIntMethod(utilsClass, jmethod);
        bool failed = env->ExceptionCheck();
        clearJniException(env);
        env->DeleteLocalRef(utilsClass);
        return failed ? defaultValue : (int)value;
    }

    bool AndroidPlatform::canShowBatteryLevel(){
        return getPlatformBool("isBatterySupported");
    }

    int AndroidPlatform::getBatteryLevel() {
        return getPlatformInt("getBatteryLevel");
    }

    bool AndroidPlatform::isBatteryCharging() {
        return getPlatformBool("isBatteryCharging");
    }

    bool AndroidPlatform::canShowWirelessLevel() {
        return getPlatformBool("isWifiSupported");
    }

    bool AndroidPlatform::hasWirelessConnection() {
        return getPlatformBool("isWifiConnected");
    }

    int AndroidPlatform::getWirelessLevel() {
        int level = getPlatformInt("getWifiSignalStrength");
        if (level >= -60) {
            return 3;
        } else if (level >= -70) {
            return 2;
        }  else if (level >= -80) {
            return 1;
        }
        return 0;
    }

    bool AndroidPlatform::hasEthernetConnection() {
        return getPlatformBool("isEthernetConnected");
    }

    std::string AndroidPlatform::getIpAddress() {
        std::string ipaddr = "-";
        int sock;
        struct ifconf conf;
        char data[4096];
        struct ifreq* ifr;
        struct sockaddr_in* sock_addr;

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            return "-";
        }

        conf.ifc_len = sizeof(data);
        conf.ifc_buf = (caddr_t)data;
        if (ioctl(sock, SIOCGIFCONF, &conf) < 0)
        {
            close(sock);
            return "-";
        }

        ifr = (struct ifreq*)data;
        while ((char*)ifr < data + conf.ifc_len)
        {
            if (ifr->ifr_addr.sa_family == AF_INET && memcmp(ifr->ifr_name, "lo", 2) != 0)
            {
                sock_addr          = (struct sockaddr_in*)&ifr->ifr_addr;
                const char* result = inet_ntoa(sock_addr->sin_addr);
                if (result) {
                    ipaddr = std::string{ result };
                    break;
                }
            }
            ifr = (struct ifreq*)((char*)ifr + sizeof(*ifr));
        }
        close(sock);

        return ipaddr;
    }

    std::string AndroidPlatform::getDnsServer() {
        return "-";
    }

    void AndroidPlatform::openBrowser(std::string url) {
        auto env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr)
            return;

        jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
        if (env->ExceptionCheck() || utilsClass == nullptr) {
            clearJniException(env);
            if (utilsClass) env->DeleteLocalRef(utilsClass);
            return;
        }

        jmethodID openBrowserMethod = env->GetStaticMethodID(utilsClass, "openBrowser", "(Ljava/lang/String;)V");
        if (env->ExceptionCheck() || openBrowserMethod == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return;
        }

        auto jurl = env->NewStringUTF(url.c_str());
        if (jurl != nullptr) {
            env->CallStaticVoidMethod(utilsClass, openBrowserMethod, jurl);
            clearJniException(env);
            env->DeleteLocalRef(jurl);
        } else {
            clearJniException(env);
        }
        env->DeleteLocalRef(utilsClass);
    }

    float AndroidPlatform::getBacklightBrightness() {
        auto env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr)
            return 0.0f;

        jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
        if (env->ExceptionCheck() || utilsClass == nullptr) {
            clearJniException(env);
            if (utilsClass) env->DeleteLocalRef(utilsClass);
            return 0.0f;
        }
        auto activity = (jobject)SDL_AndroidGetActivity();
        if (activity == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return 0.0f;
        }
        jmethodID jmethod = env->GetStaticMethodID(utilsClass, "getAppScreenBrightness",
                                                   "(Landroid/app/Activity;)F");
        if (env->ExceptionCheck() || jmethod == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(utilsClass);
            return 0.0f;
        }
        jfloat value = env->CallStaticFloatMethod(utilsClass, jmethod, activity);
        bool failed = env->ExceptionCheck();
        clearJniException(env);
        env->DeleteLocalRef(activity);
        env->DeleteLocalRef(utilsClass);
        return failed ? 0.0f : value;
    }

    void AndroidPlatform::setBacklightBrightness(float brightness) {
        auto env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr)
            return;

        jclass utilsClass = env->FindClass("org/libsdl/app/PlatformUtils");
        if (env->ExceptionCheck() || utilsClass == nullptr) {
            clearJniException(env);
            if (utilsClass) env->DeleteLocalRef(utilsClass);
            return;
        }
        jmethodID method = env->GetStaticMethodID(utilsClass, "setAppScreenBrightness",
                                                             "(Landroid/app/Activity;F)V");
        if (env->ExceptionCheck() || method == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return;
        }

        auto activity = (jobject)SDL_AndroidGetActivity();
        if (activity == nullptr) {
            clearJniException(env);
            env->DeleteLocalRef(utilsClass);
            return;
        }
        env->CallStaticVoidMethod(utilsClass, method, activity, brightness);
        clearJniException(env);
        env->DeleteLocalRef(activity);
        env->DeleteLocalRef(utilsClass);
    }

    bool AndroidPlatform::canSetBacklightBrightness() {
        return true;
    }

} // namespace brls
