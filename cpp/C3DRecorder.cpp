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

//nombres OpenXRHandJoints -> cambiando palm y wrist de orden porque asi se reciben
// Cambiamos INDEX_PROXIMAl por FIN para que coincida con Gait Model (LFIN y RFIN)
static const char* kHandJointNames[26] = {
        "WRIST",
        "PALM",
        "THUMB_METACARPAL",
        "THUMB_PROXIMAL",
        "THUMB_DISTAL",
        "THUMB_TIP",
        "INDEX_METACARPAL",
        "FIN",
        "INDEX_INTERMEDIATE",
        "INDEX_DISTAL",
        "INDEX_TIP",
        "MIDDLE_METACARPAL",
        "MIDDLE_PROXIMAL",
        "MIDDLE_INTERMEDIATE",
        "MIDDLE_DISTAL",
        "MIDDLE_TIP",
        "RING_METACARPAL",
        "RING_PROXIMAL",
        "RING_INTERMEDIATE",
        "RING_DISTAL",
        "RING_TIP",
        "LITTLE_METACARPAL",
        "LITTLE_PROXIMAL",
        "LITTLE_INTERMEDIATE",
        "LITTLE_DISTAL",
        "LITTLE_TIP"
};

void C3DRecorder::buildPointNames(std::vector<std::string>& outPointNames) const {
    outPointNames.clear();
    outPointNames.reserve(5 + 26 + 26 + 4);

    //segun el modelo de Gait sera RFHD o LFWD
    outPointNames.emplace_back("HEAD");
    outPointNames.emplace_back("LFHD");
    outPointNames.emplace_back("RFHD");
    outPointNames.emplace_back("LCTRL");
    outPointNames.emplace_back("RCTRL");

    // Joints mano izquierda siguiendo el orden de XrHandJointEXT
    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back(std::string("L") + kHandJointNames[i]);
    }

    // Joints mano derecha siguiendo el mismo orden
    for (int i = 0; i < 26; ++i) {
        outPointNames.emplace_back(std::string("R") + kHandJointNames[i]);
    }

    // Puntos extra tipo Plug-in Gait para la muñeca
    outPointNames.emplace_back("LWRA");
    outPointNames.emplace_back("LWRB");
    outPointNames.emplace_back("RWRA");
    outPointNames.emplace_back("RWRB");
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
    addQuat("LCTRL");
    addQuat("RCTRL");

    // Manos: L_*
    for (int i = 0; i < 26; ++i) {
        addQuat(std::string("L") + kHandJointNames[i]);
    }

    // Manos: R_*
    for (int i = 0; i < 26; ++i) {
        addQuat(std::string("R") + kHandJointNames[i]);
    }
    // Canal extra: timestamp en segundos
    outAnalogNames.emplace_back("RealTime");
}

//pequeno helper para calcular las posiciones exteriores de la cabeza
//primero rotamos el vector y luego sumamos 7.5cm (15/2) a cada lado
void calculateHeadWidth(const VRPosePlain& midPos,Vector3& pointRight,Vector3& pointLeft, float headWidth) {
    const float halfWidth = headWidth / 2.0f;

    // Extraer componentes
    float qx = midPos.rotation[0], qy =  midPos.rotation[1], qz =  midPos.rotation[2], qw =  midPos.rotation[3];

    // Vector lateral en el sistema local de la cabeza
    Vector3 vLocal;
    vLocal.x = halfWidth; vLocal.y = 0.0f; vLocal.z = 0.0f;

    // Parte vectorial del cuaternion
    Vector3 qv;
    qv.x = qx; qv.y = qy; qv.z = qz;

    // t = 2 * cross(qv, vLocal)
    Vector3 t;
    t.x = 2.0f * (qv.y * vLocal.z - qv.z * vLocal.y);
    t.y = 2.0f * (qv.z * vLocal.x - qv.x * vLocal.z);
    t.z = 2.0f * (qv.x * vLocal.y - qv.y * vLocal.x);

    // vWorld = vLocal + qw * t + cross(qv, t)
    Vector3 vWorld;
    vWorld.x = vLocal.x + qw * t.x + (qv.y * t.z - qv.z * t.y);
    vWorld.y = vLocal.y + qw * t.y + (qv.z * t.x - qv.x * t.z);
    vWorld.z = vLocal.z + qw * t.z + (qv.x * t.y - qv.y * t.x);

    Vector3 worldOffset = vWorld;

    // Aplicar a la posicion
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
void calculateWristWidthFromJoint(const JointSamplePlain& joint, Vector3& pointRight, Vector3& pointLeft, float wristWidth) {
    const float halfWidth = wristWidth / 2.0f;

    float qx = joint.qx;
    float qy = joint.qy;
    float qz = joint.qz;
    float qw = joint.qw;

    // Vector lateral en el sistema local de la muneca
    Vector3 vLocal;
    vLocal.x = halfWidth; vLocal.y = 0.0f; vLocal.z = 0.0f;

    // Parte vectorial del cuaternion
    Vector3 qv;
    qv.x = qx; qv.y = qy; qv.z = qz;

    // t = 2 * cross(qv, vLocal)
    Vector3 t;
    t.x = 2.0f * (qv.y * vLocal.z - qv.z * vLocal.y);
    t.y = 2.0f * (qv.z * vLocal.x - qv.x * vLocal.z);
    t.z = 2.0f * (qv.x * vLocal.y - qv.y * vLocal.x);

    // vWorld = vLocal + qw * t + cross(qv, t)
    Vector3 vWorld;
    vWorld.x = vLocal.x + qw * t.x + (qv.y * t.z - qv.z * t.y);
    vWorld.y = vLocal.y + qw * t.y + (qv.z * t.x - qv.x * t.z);
    vWorld.z = vLocal.z + qw * t.z + (qv.x * t.y - qv.y * t.x);

    // Aplicar a la posicion del joint
    pointRight = {
            joint.px + vWorld.x,
            joint.py + vWorld.y,
            joint.pz + vWorld.z
    };

    pointLeft = {
            joint.px - vWorld.x,
            joint.py - vWorld.y,
            joint.pz - vWorld.z
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
            calculateHeadWidth(f.hmdPose, lfhdVec, rfhdVec, 0.15f);
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

            // Puntos extra de muneca tipo LWRA/LWRB/RWRA/RWRB
            const size_t IDX_LWRA = 5 + 26 + 26;       // despues de L+R joints
            const size_t IDX_LWRB = IDX_LWRA + 1;
            const size_t IDX_RWRA = IDX_LWRA + 2;
            const size_t IDX_RWRB = IDX_LWRA + 3;

            // Mano izquierda: asumimos joint 0 = WRIST (como en kHandJointNames)
            if (f.leftHandJointCount > 0) {
                const auto& wrist = f.leftHandJoints[0];
                if (wrist.hasPose) {
                    Vector3 wra, wrb;
                    calculateWristWidthFromJoint(wrist, wra, wrb, 0.06f);
                    {
                        Point p;
                        p.x(wra.x);
                        p.y(wra.y);
                        p.z(wra.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_LWRA));
                    }
                    {
                        Point p;
                        p.x(wrb.x);
                        p.y(wrb.y);
                        p.z(wrb.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_LWRB));
                    }
                }
            }

            // Mano derecha
            if (f.rightHandJointCount > 0) {
                const auto& wrist = f.rightHandJoints[0];
                if (wrist.hasPose) {
                    Vector3 wra, wrb;
                    calculateWristWidthFromJoint(wrist, wra, wrb, 0.06f);
                    {
                        Point p;
                        p.x(wra.x);
                        p.y(wra.y);
                        p.z(wra.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_RWRA));
                    }
                    {
                        Point p;
                        p.x(wrb.x);
                        p.y(wrb.y);
                        p.z(wrb.z);
                        p.residual(0.0);
                        points.point(p, static_cast<int>(IDX_RWRB));
                    }
                }
            }
            frame.points() = points;
        }

        // --- Analogicos (cuaterniones + flag) ---
        {
            using ezc3d::DataNS::AnalogsNS::Channel;
            using ezc3d::DataNS::AnalogsNS::SubFrame;
            using ezc3d::DataNS::AnalogsNS::Analogs;
            const size_t nbAnalogs = analogNames.size();
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

            while (idx + 1 < nbAnalogs) { // dejamos el ultimo para RealTime
                setChan(idx++, 0.0f);
            }

            // Canal extra: tiempo real del frame en segundos
            if (nbAnalogs > 0) {
                const size_t timeIdx = nbAnalogs - 1; // siempre el ultimo
                setChan(timeIdx, static_cast<float>(f.timestampSec));
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
