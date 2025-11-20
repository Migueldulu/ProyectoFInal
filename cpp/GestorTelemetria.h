#pragma once
#include <vector>
#include <string>
#include <mutex>
#include "TiposTelemetria.h"
#include "TiposVR.h"
#include <deque>
#include <condition_variable>
#include <thread>

class AndroidUploader;

// Gestor basico de buffering y disparo de subida
class GestorTelemetria {
public:
    GestorTelemetria();
    ~GestorTelemetria();

    bool initialize(const UploaderConfig& cfg, AndroidUploader* uploader);
    void recordFrame(const VRFrameDataPlain& frame);
    void flushAndUpload();
    void shutdown();

private:
    std::mutex mtx_;
    std::vector<VRFrameDataPlain> buffer_;
    UploaderConfig cfg_;
    AndroidUploader* uploader_ = nullptr;
    int framesCount_ = 0;
    std::string sessionId_;
    std::string deviceInfo_;


    void serializeAndSend(const std::vector<VRFrameDataPlain>& chunk);

// --- Cola y worker para subida asincrona ---
    std::thread worker_;
    std::mutex qmtx_;
    std::condition_variable qcv_;
    std::deque<std::vector<VRFrameDataPlain>> queue_;
    bool stopWorker_ = false;
    size_t maxQueuedChunks_ = 4; // backpressure: maximo de chunks en cola (no deberia ni siquiera llegar a usar 3)
    void workerLoop(); // hilo que serializa y sube chunks
};
