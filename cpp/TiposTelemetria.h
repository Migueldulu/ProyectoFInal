#pragma once
#include <string>

// Configuracion para uploader y gestor
struct UploaderConfig {
    std::string endpointUrl;  // Se carga desde initialConfig.json
    std::string apiKey;       // Se carga desde initialConfig.json
    std::string sessionId;    // Viene del initialize (Unity/UE)
    std::string deviceInfo;   // Viene del initialize (Unity/UE)
    int framesPerFile = 5400;
};

// Estructura que se envia desde Unity/UE a C++
struct TelemetryConfigPlain {
    const char* sessionId;     // UTF-8
    const char* deviceInfo;    // UTF-8
};