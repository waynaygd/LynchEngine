#pragma once
#include <DirectXMath.h>
using namespace DirectX;

struct Camera {
    XMFLOAT3 pos{ 0,0,-5 };
    float    yaw = 0.0f;      
    float    pitch = 0.0f;       

    float fovY = XM_PIDIV4;   
    float aspect = 16.0f / 9.0f;
    float zn = 0.1f;
    float zf = 3000.0f;

    float moveSpeed = 3.5f;      
    float mouseSens = 0.0025f; 

    XMFLOAT4X4 view{}, proj{};

    void SetLens(float fovY_, float aspect_, float zn_, float zf_) {
        fovY = fovY_; aspect = aspect_; zn = zn_; zf = zf_;
        XMMATRIX P = XMMatrixPerspectiveFovLH(fovY, aspect, zn, zf);
        XMStoreFloat4x4(&proj, P);
    }

    void Walk(float d) { 
        XMVECTOR dir = Forward();
        XMVECTOR p = XMLoadFloat3(&pos) + d * dir;
        XMStoreFloat3(&pos, p);
    }
    void Strafe(float d) {
        XMVECTOR dir = Right();
        XMVECTOR p = XMLoadFloat3(&pos) + d * dir;
        XMStoreFloat3(&pos, p);
    }
    void UpDown(float d) {
        XMVECTOR p = XMLoadFloat3(&pos) + d * XMVectorSet(0, 1, 0, 0);
        XMStoreFloat3(&pos, p);
    }

    void AddYawPitch(float dyaw, float dpitch) {
        yaw += dyaw;
        pitch += dpitch;
        const float limit = XM_PIDIV2 - 0.01f;
        if (pitch > limit) pitch = limit;
        if (pitch < -limit) pitch = -limit;
    }

    void UpdateView() {
        XMVECTOR F = Forward();
        XMVECTOR R = Right();
        XMVECTOR U = XMVector3Cross(F, R) * -1.0f;

        XMVECTOR eye = XMLoadFloat3(&pos);
        XMMATRIX V = XMMatrixLookToLH(eye, F, XMVectorSet(0, 1, 0, 0));
        XMStoreFloat4x4(&view, V);
    }

    XMVECTOR Forward() const {
        float cy = cosf(yaw), sy = sinf(yaw);
        float cp = cosf(pitch), sp = sinf(pitch);

        return XMVector3Normalize(XMVectorSet(sy * cp, sp, cy * cp, 0));
    }
    XMVECTOR Right() const {

        XMVECTOR F = Forward();
        XMVECTOR U = XMVectorSet(0, 1, 0, 0);
        return XMVector3Normalize(XMVector3Cross(U, F));
    }

    XMMATRIX View() const { return XMLoadFloat4x4(&view); }
    XMMATRIX Proj() const { return XMLoadFloat4x4(&proj); }
};
