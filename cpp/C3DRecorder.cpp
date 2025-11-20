#include "C3DRecorder.h"
#include "configReader.h"

#include <android/log.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <ezc3d/ezc3d.h> // Libreria externa open source para tratamiento de archivos c3d
#include <ezc3d/Parameters.h>
#include <ezc3d/Data.h>

#define LOG_TAG "telemetria"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct Vector3 {
    float x, y, z;
};

C3DRecorder::C3DRecorder() {}
C3DRecorder::~C3DRecorder() {
    // Por seguridad: si alguien se olvida de llamar a finalize,
    if (initialized_ && !finalized_) {
        C3Dfinalize();
    }
}

bool C3DRecorder::C3DisInitialized() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return initialized_;
}

// Por si el nombre de sesion trae algun caracter no permitido
std::string C3DRecorder::sanitizeSessionId(const std::string& raw) const {
    std::string s = raw;
    // Reemplaza caracteres raros por '_'
    for (char& c : s) {
        if (!( (c >= '0' && c <= '9') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') ||
               c == '-' || c == '_' )) {
            c = '_';
        }
    }
    if (s.empty()) s = "unknown";
    return s;
}

// Construye /sdcard/Android/data/<pkg>/files/session_<sessionId>.c3d
// reutilizando configReader::getExpectedConfigPath
std::string C3DRecorder::buildOutputPath(const UploaderConfig& cfg) {
    std::string cfgPath;
    if (!configReader::getExpectedConfigPath(cfgPath)) {
        LOGE("C3DRecorder: getExpectedConfigPath failed, cannot build C3D path");
        return "";
    }

    // Nos quedamos solo con el directorio de initialConfig.json
    size_t pos = cfgPath.find_last_of("/\\");
    std::string dir;
    if (pos == std::string::npos) {
        dir = "/sdcard";
    } else {
        dir = cfgPath.substr(0, pos);
    }

    std::string sid = sanitizeSessionId(cfg.sessionId);
    std::string path = dir + "/session_" + sid + ".c3d";
    return path;
}

bool C3DRecorder::C3Dinitialize(const UploaderConfig& cfg, int frameRate) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (initialized_) {
        LOGI("C3DRecorder already initialized, ignoring.");
        return true;
    }

    filePath_ = buildOutputPath(cfg);
    if (filePath_.empty()) {
        LOGE("C3DRecorder: empty file path, disabling C3D recording");
        initialized_ = false;
        return false;
    }

    frameRate_ = frameRate;
    if (frameRate_ <= 0) frameRate_ = 60;

    frames_.clear();
    finalized_ = false;
    initialized_ = true;

    LOGI("C3DRecorder initialized. Path='%s', frameRate=%d",
         filePath_.c_str(), frameRate_);

    // No abrimos nada todavia: ezc3d escribira en finalize.
    return true;
}

void C3DRecorder::C3DrecordFrame(const VRFrameDataPlain& frame) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || finalized_) {
        return;
    }
    frames_.push_back(frame);
}

// Streaming real mas adelante,
void C3DRecorder::C3Dflush() {
    // En esta primera version no hacemos flush parcial para evitar
    // complicar el layout del C3D. Se deja preparado para futuro.
}

void C3DRecorder::C3Dfinalize() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || finalized_) {
        return;
    }

    if (frames_.empty()) {
        LOGI("C3DRecorder: no frames to write, skipping C3D file.");
        finalized_ = true;
        return;
    }

    LOGI("C3DRecorder: writing C3D file '%s' with %zu frames",
         filePath_.c_str(), frames_.size());

// Sin try/catch porque estamos compilando con -fno-exceptions
    writeC3D();

    finalized_ = true;
}

void C3DRecorder::buildPointNames(std::vector<std::string>& outPointNames) const {
    outPointNames.clear();
    outPointNames.reserve(5 + 26 + 26);

    //segun el modelo de Gait sera RFHD o LFWD
    outPointNames.emplace_back("HEAD");
    outPointNames.emplace_back("LFHD");
    outPointNames.emplace_back("RFHD");
    outPointNames.emplace_back("L_CTRL");
    outPointNames.emplace_back("R_CTRL");

    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back("L_J" + std::to_string(i));
    }
    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back("R_J" + std::to_string(i));
    }
}

void C3DRecorder::buildAnalogNames(std::vector<std::string>& outAnalogNames) const {
    outAnalogNames.clear();

    auto addQuat = [&](const std::string& prefix) {
        outAnalogNames.emplace_back(prefix + "_qx");
        outAnalogNames.emplace_back(prefix + "_qy");
        outAnalogNames.emplace_back(prefix + "_qz");
        outAnalogNames.emplace_back(prefix + "_qw");
    };

    addQuat("HMD");
    addQuat("LFHD");
    addQuat("RFHD");
    addQuat("L_CTRL");
    addQuat("R_CTRL");

    for (int i = 0; i < 26; ++i) {
        addQuat("L_J" + std::to_string(i));
    }
    for (int i = 0; i < 26; ++i) {
        addQuat("R_J" + std::to_string(i));
    }
    // Canal extra: timestamp en segundos
    outAnalogNames.emplace_back("RealTime");
}

//pequeno helper para calcular las posiciones exteriores de la cabeza
//primero rotamos el vector y luego sumamos 7.5cm (15/2) a cada lado
void calculateHeadWidth(const VRPosePlain& midPos,Vector3& pointRight,Vector3& pointLeft, float headWidth = 0.15f) {
    const float halfWidth = headWidth / 2.0f;

    // Extraer componentes
    float qx = midPos.rotation[0], qy =  midPos.rotation[1], qz =  midPos.rotation[2], qw =  midPos.rotation[3];

    // Calcular directamente la rotacion del vector (halfWidth, 0, 0)
    float tx = 2.0f * (qy * 0 - qz * 0);           // = 0
    float ty = 2.0f * (qz * halfWidth - qx * 0);   // = 2 * qz * halfWidth
    float tz = 2.0f * (qx * 0 - qy * halfWidth);   // = -2 * qy * halfWidth

    float wx = qw * halfWidth + qy * tz - qz * ty;
    float wy = qw * 0 + qz * tx - qx * tz;
    float wz = qw * 0 + qx * ty - qy * tx;
    float ww = -qx * halfWidth - qy * 0 - qz * 0;

    // Multiplicar por el conjugado
    Vector3 worldOffset;
    worldOffset.x = wx * qw + ww * -qx + wy * -qz - wz * -qy;
    worldOffset.y = wy * qw + ww * -qy + wz * -qx - wx * -qz;
    worldOffset.z = wz * qw + ww * -qz + wx * -qy - wy * -qx;

    // Aplicar a la posición
    pointRight = {
            midPos.position[0] + worldOffset.x,
            midPos.position[1] + worldOffset.y,
            midPos.position[2] + worldOffset.z
    };

    pointLeft = {
            midPos.position[0] - worldOffset.x,
            midPos.position[1] - worldOffset.y,
            midPos.position[2] - worldOffset.z
    };
}

// --- Escritura real con ezc3d (version en memoria, simple) ---
void C3DRecorder::writeC3D() {
    // Creamos un C3D vacio
    ezc3d::c3d c3d;

    // Nombres de puntos y canales
    std::vector<std::string> pointNames;
    std::vector<std::string> analogNames;
    buildPointNames(pointNames);
    buildAnalogNames(analogNames);

    const size_t nbPoints  = pointNames.size();
    const size_t nbAnalogs = analogNames.size();
    const size_t nbFrames  = frames_.size();

    // Declarar los puntos y canales para que los parametros se ajusten
    for (const auto& name : pointNames) {
        c3d.point(name);
    }
    for (const auto& name : analogNames) {
        c3d.analog(name);
    }

    // Ajustar parametros basicos de RATE
    {
        // POINT:UNITS
        ezc3d::ParametersNS::GroupNS::Parameter pointUnits("UNITS");
        pointUnits.set(std::vector<std::string>{"m"});
        c3d.parameter("POINT", pointUnits);
        // POINT:RATE
        ezc3d::ParametersNS::GroupNS::Parameter pointRate("RATE");
        pointRate.set(std::vector<double>{ static_cast<double>(frameRate_) });
        c3d.parameter("POINT", pointRate);

        // ANALOG:RATE = frameRate tambien (simple: 1 sample analog por frame)
        ezc3d::ParametersNS::GroupNS::Parameter analogRate("RATE");
        analogRate.set(std::vector<double>{ static_cast<double>(frameRate_) });
        c3d.parameter("ANALOG", analogRate);
    }

    // Dejar que ezc3d actualice header segun parametros y datos
    // (lo hace internamente cuando metemos frames).

    // Construir frames uno a uno
    for (size_t i = 0; i < nbFrames; ++i) {
        const VRFrameDataPlain& f = frames_[i];

        ezc3d::DataNS::Frame frame;

        // --- Puntos 3D ---
        {
            using ezc3d::DataNS::Points3dNS::Point;
            using ezc3d::DataNS::Points3dNS::Points;

            Points points(nbPoints);

            // Inicializamos todos como invalidos (residual = -1)
            for (size_t pi = 0; pi < nbPoints; ++pi) {
                Point p;
                p.x(0.0);
                p.y(0.0);
                p.z(0.0);
                p.residual(-1.0);
                points.point(p, static_cast<int>(pi));
            }

            // HMD en indice 0
            {
                Point p;
                p.x(f.hmdPose.position[0]);
                p.y(f.hmdPose.position[1]);
                p.z(f.hmdPose.position[2]);
                p.residual(0.0);
                points.point(p, 0);
            }

            Vector3 lfhdVec, rfhdVec;
            calculateHeadWidth(f.hmdPose, lfhdVec, rfhdVec);
            // LFHD
            {
                Point p;
                p.x(lfhdVec.x);
                p.y(lfhdVec.y);
                p.z(lfhdVec.z);
                p.residual(0.0);
                points.point(p, 1);
            }

            // RFHD
            {
                Point p;
                p.x(rfhdVec.x);
                p.y(rfhdVec.y);
                p.z(rfhdVec.z);
                p.residual(0.0);
                points.point(p, 2);
            }

            // L_CTRL en indice 1 (solo si esta activo)
            if (f.leftCtrl.isActive) {
                Point p;
                p.x(f.leftCtrl.pose.position[0]);
                p.y(f.leftCtrl.pose.position[1]);
                p.z(f.leftCtrl.pose.position[2]);
                p.residual(0.0);
                points.point(p, 3);
            }

            // R_CTRL en indice 2 (solo si esta activo)
            if (f.rightCtrl.isActive) {
                Point p;
                p.x(f.rightCtrl.pose.position[0]);
                p.y(f.rightCtrl.pose.position[1]);
                p.z(f.rightCtrl.pose.position[2]);
                p.residual(0.0);
                points.point(p, 4);
            }

            // Manos: L_J0..L_J25
            for (int j = 0; j < f.leftHandJointCount && j < 26; ++j) {
                const auto& s = f.leftHandJoints[j];
                Point p;
                p.x(s.px);
                p.y(s.py);
                p.z(s.pz);
                p.residual(s.hasPose ? 0.0 : -1.0);
                points.point(p, 5 + j);
            }

            // Manos: R_J0..R_J25
            for (int j = 0; j < f.rightHandJointCount && j < 26; ++j) {
                const auto& s = f.rightHandJoints[j];
                Point p;
                p.x(s.px);
                p.y(s.py);
                p.z(s.pz);
                p.residual(s.hasPose ? 0.0 : -1.0);
                points.point(p, 5 + 26 + j);
            }

            frame.points() = points;
        }

        // --- Analogicos (cuaterniones + flag) ---
        {
            using ezc3d::DataNS::AnalogsNS::Channel;
            using ezc3d::DataNS::AnalogsNS::SubFrame;
            using ezc3d::DataNS::AnalogsNS::Analogs;

            // Solo 1 subframe por frame (ANALOG:RATE == POINT:RATE)
            SubFrame subframe;
            subframe.nbChannels(nbAnalogs);

            auto setChan = [&](size_t idx, float value) {
                Channel ch;
                ch.data(value);
                subframe.channel(ch, idx);
            };

            size_t idx = 0;

            // HMD_q*
            setChan(idx++, f.hmdPose.rotation[0]);
            setChan(idx++, f.hmdPose.rotation[1]);
            setChan(idx++, f.hmdPose.rotation[2]);
            setChan(idx++, f.hmdPose.rotation[3]);

            // L_CTRL_q*
            setChan(idx++, f.leftCtrl.pose.rotation[0]);
            setChan(idx++, f.leftCtrl.pose.rotation[1]);
            setChan(idx++, f.leftCtrl.pose.rotation[2]);
            setChan(idx++, f.leftCtrl.pose.rotation[3]);

            // R_CTRL_q*
            setChan(idx++, f.rightCtrl.pose.rotation[0]);
            setChan(idx++, f.rightCtrl.pose.rotation[1]);
            setChan(idx++, f.rightCtrl.pose.rotation[2]);
            setChan(idx++, f.rightCtrl.pose.rotation[3]);

            // L_Joints
            for (int j = 0; j < f.leftHandJointCount && j < 26; ++j) {
                const auto& s = f.leftHandJoints[j];
                setChan(idx++, s.qx);
                setChan(idx++, s.qy);
                setChan(idx++, s.qz);
                setChan(idx++, s.qw);
            }
            // R_Joints
            for (int j = 0; j < f.rightHandJointCount && j < 26; ++j) {
                const auto& s = f.rightHandJoints[j];
                setChan(idx++, s.qx);
                setChan(idx++, s.qy);
                setChan(idx++, s.qz);
                setChan(idx++, s.qw);
            }

            // Canal extra: tiempo real del frame en segundos
            if (idx < nbAnalogs) {
                setChan(idx++, static_cast<float>(f.timestampSec));
            }

            Analogs analogs;
            analogs.subframe(subframe, 0);
            frame.analogs() = analogs;
        }

        // Anadimos el frame al c3d
        c3d.frame(frame);
    }

    // Por ultimo, escribimos el archivo en disco
    c3d.write(filePath_);
    LOGI("C3DRecorder: file written OK: '%s'", filePath_.c_str());
}
