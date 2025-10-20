#include "TelemetriaAPI.h"
#include "GestorTelemetria.h"
#include "AndroidUploader.h"
#include <android/log.h>
#include <mutex>
#include "configReader.h"

#define LOG_TAG "telemetria"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Estado global minimo
static JavaVM* g_vm = nullptr;
static jobject g_activity = nullptr; // global ref
static std::mutex g_mutex;

static GestorTelemetria g_gestor;
static AndroidUploader g_uploader;

// Captura JavaVM en carga de la libreria (a ver si asi funciona)
jint JNI_OnLoad(JavaVM* vm, void*) {
    // Captura JavaVM en carga de la libreria
    std::lock_guard<std::mutex> lock(g_mutex);
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" {

void telemetry_set_java_context(JavaVM* vm, jobject activity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (vm) g_vm = vm;
    // Crea global ref de activity para uso futuro
    if (g_activity) {
        JNIEnv* env = nullptr;
        if (g_vm && g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) {
            env->DeleteGlobalRef(g_activity);
        }
        g_activity = nullptr;
    }
    if (activity) {
        if (!g_vm) {
            LOGE("telemetry_set_java_context called without JavaVM");
            return;
        }
        JNIEnv* env = nullptr;
        if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
            LOGE("GetEnv failed");
            return;
        }
        g_activity = env->NewGlobalRef(activity);
    }
    LOGI("Java context set");
}

int telemetry_initialize(const TelemetryConfigPlain* cfg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!cfg) return -1;
    if (!g_vm || !g_activity) {
        LOGE("Java context not set");
        return -2;
    }
    // Contexto Java para el uploader
    g_uploader.setJavaContext(g_vm, g_activity);

    // Tomamos sesion y disp desde el caller
    UploaderConfig ucfg;
    ucfg.sessionId   = cfg->sessionId ? cfg->sessionId : "";
    ucfg.deviceInfo = cfg->deviceInfo ? cfg->deviceInfo : "";

    //Cargamos el resto de info desde el fichero initialConfig.json o info por defecto
    configReader::setConfig(ucfg);
    if (!configReader_setConfig(ucfg)) {
        LOGI("Config file not found or unreadable; endpoint/apiKey will likely be localhost:1414 - no apiKey.");
    }
    LOGI("config final: endpointUrl='%s', apiKey.len=%d, framesPerFile=%d, sessionId='%s', deviceInfo.len=%d",
         ucfg.endpointUrl.c_str(),
         (int)ucfg.apiKey.size(),
         ucfg.framesPerFile,
         ucfg.sessionId.c_str(),
         (int)ucfg.deviceInfo.size());

    if (!g_uploader.initialize(ucfg)) {
        LOGE("Uploader initialize failed");
        return -3;
    }
    if (!g_gestor.initialize(ucfg, &g_uploader)) {
        LOGE("Gestor initialize failed");
        return -4;
    }
    LOGI("telemetry initialized");
    int frameRate = 60;
    configReader::getFrameRate(frameRate);
    // Garantizando limites [1,240]
    if (frameRate < 1 || frameRate > 240) frameRate = 60;
    return frameRate;
}

void telemetry_record_frame(const VRFrameDataPlain* frame) {
    if (!frame) return;
    g_gestor.recordFrame(*frame);
}

void telemetry_force_upload() {
    g_gestor.flushAndUpload();
}

void telemetry_shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_gestor.shutdown();
    g_uploader.shutdown();

    if (g_activity) {
        JNIEnv* env = nullptr;
        if (g_vm && g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) {
            env->DeleteGlobalRef(g_activity);
        }
        g_activity = nullptr;
    }
    LOGI("telemetry shutdown complete");
}

} // extern "C"
