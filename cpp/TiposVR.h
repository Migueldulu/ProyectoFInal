#pragma once

// Tipos basicos plain para VR

struct VRPosePlain {
    float position[3];
    float rotation[4];
};

struct ControllerStatePlain {
    VRPosePlain pose;
    unsigned int buttons;
    float trigger;
    float grip;
    // Analog stick
    float stickX;
    float stickY;
    int isActive;
};

// NEW: muestra de joint de mano
struct JointSamplePlain {
    int   idIndex;   // indice de joint en tu layout
    int   state;     // flags/estado por joint (TODO se puede consultar radio y velocidad con esto)
    float px, py, pz;
    float qx, qy, qz, qw;
    unsigned char hasPose; // 0/1
    unsigned char _pad_[3]; // padding para alinear a 4 bytes
};

struct VRFrameDataPlain {
    double timestampSec;
    VRPosePlain hmdPose;
    ControllerStatePlain leftCtrl;
    ControllerStatePlain rightCtrl;
    JointSamplePlain leftHandJoints[30];
    int leftHandJointCount;
    JointSamplePlain rightHandJoints[30];
    int rightHandJointCount;
};