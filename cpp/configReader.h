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
}
extern "C" bool configReader_setConfig(UploaderConfig& outCfg);
extern "C" int  configReader_getFrameRate();