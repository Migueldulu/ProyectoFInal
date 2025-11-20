#pragma once
#include <jni.h>
#include "TiposVR.h"
#include "TiposTelemetria.h"

// Export macro para Android
#ifndef TELEMETRIA_API
#define TELEMETRIA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Contexto Java: activity es obligatorio para cargar clases del AAR.
// vm puede ser null si fue capturado en JNI_OnLoad.
TELEMETRIA_API void telemetry_set_java_context(JavaVM* vm, jobject activity);
// Inicializa el gestor y la capa de subida
// En exito devuelve el frameRate (1..240). En error, devuelve un codigo negativo.
TELEMETRIA_API int  telemetry_initialize(const TelemetryConfigPlain* cfg);
// Registra un frame de telemetria
TELEMETRIA_API void telemetry_record_frame(const VRFrameDataPlain* frame);
// Fuerza subida de datos pendientes
TELEMETRIA_API void telemetry_force_upload();
// Libera recursos
TELEMETRIA_API void telemetry_shutdown();

// NUEVO: bitmask de flags de características leídas del JSON bit0=handTracking, bit1=primary, bit2=secondary, bit3=grip, bit4=trigger, bit5=joystick
TELEMETRIA_API unsigned telemetry_get_feature_flags();

#ifdef __cplusplus
}
#endif
