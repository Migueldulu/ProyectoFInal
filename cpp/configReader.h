#pragma once
#include <string>
#include "TiposTelemetria.h"

// Lector de configuracion inicial guardada en:
//   /sdcard/Android/data/<paquete>/files/initialConfig.json

namespace configReader {
    // Devuelve la ruta construida a partir de /proc/self/cmdline
    bool getExpectedConfigPath(std::string& outPath);

    // Lee un fichero completo a string (binario opaco)
    bool readFileToString(const std::string& path, std::string& outText);

    // Lee el archivo, logea contenido (truncado) y rellena endpointUrl/apiKey de outCfg.
    bool setConfig(UploaderConfig& outCfg);

    // Obtiene frameRate desde initialConfig.json; si falta, 60. Si está fuera de [1,240], se ignora y se usa 60.
    bool getFrameRate(int& outFrameRate);

    // bit0=handTracking, bit1=primary, bit2=secondary, bit3=grip, bit4=trigger, bit5=joystick
    inline unsigned getFeatureFlagsBitmask(const UploaderConfig& cfg) {
        unsigned m = 0;
        if (cfg.handTracking)  m |= 1u << 0;
        if (cfg.primaryButton) m |= 1u << 1;
        if (cfg.secondaryButton)m |= 1u << 2;
        if (cfg.grip)          m |= 1u << 3;
        if (cfg.trigger)       m |= 1u << 4;
        if (cfg.joystick)      m |= 1u << 5;
        return m;
    }
}
extern "C" bool configReader_setConfig(UploaderConfig& outCfg);
extern "C" int  configReader_getFrameRate();