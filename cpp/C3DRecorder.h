#pragma once
#include <string>
#include <vector>
#include <mutex>

#include "TiposTelemetria.h"
#include "TiposVR.h"

// Pequeno helper para generar un C3D por sesion de VR. Se llama desde telemetriaAPI

class C3DRecorder {
public:
    C3DRecorder();
    ~C3DRecorder();

    // Inicializa el recorder para una sesion concreta.
    // - cfg.sessionId se usa para el nombre del archivo.
    // - frameRate es el que has leido de initialConfig.json.
    bool C3Dinitialize(const UploaderConfig& cfg, int frameRate);

    // Registra un frame de telemetria (paralelo al JSON).
    void C3DrecordFrame(const VRFrameDataPlain& frame);

    // Por si acaso lo implemento
    void C3Dflush();

    // Cierra la sesion y escribe el archivo C3D en disco.
    void C3Dfinalize();

    bool C3DisInitialized() const;

private:
    mutable std::mutex mtx_;
    bool initialized_ = false;
    bool finalized_   = false;

    std::string filePath_;
    int frameRate_ = 60;

    // Guardamos todos los frames y luego los volcaremos a ezc3d en finalize.
    std::vector<VRFrameDataPlain> frames_;

    // Helpers internos
    std::string buildOutputPath(const UploaderConfig& cfg);
    std::string sanitizeSessionId(const std::string& raw) const;

    // Construccion de nombres de puntos/canales
    void buildPointNames(std::vector<std::string>& outPointNames) const;
    void buildAnalogNames(std::vector<std::string>& outAnalogNames) const;

    // Escribimos el C3D usando ezc3d a partir de frames_
    void writeC3D();
};
