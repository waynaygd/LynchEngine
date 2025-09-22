#pragma once
#include <DirectXMath.h>
using namespace DirectX;

struct Camera {
    // Положение и ориентация (yaw вокруг Y, pitch вокруг X)
    XMFLOAT3 pos{ 0,0,-5 };
    float    yaw = 0.0f;           // радианы
    float    pitch = 0.0f;           // радианы

    // Проекция
    float fovY = XM_PIDIV4;        // 45°
    float aspect = 16.0f / 9.0f;
    float zn = 0.1f;
    float zf = 3000.0f;

    // Скорости
    float moveSpeed = 3.5f;          // м/с
    float mouseSens = 0.0025f;       // рад/пикс

    // Матрицы (кешируем как XMFLOAT4X4)
    XMFLOAT4X4 view{}, proj{};

    void SetLens(float fovY_, float aspect_, float zn_, float zf_) {
        fovY = fovY_; aspect = aspect_; zn = zn_; zf = zf_;
        XMMATRIX P = XMMatrixPerspectiveFovLH(fovY, aspect, zn, zf);
        XMStoreFloat4x4(&proj, P);
    }

    // движения в локальных осях камеры
    void Walk(float d) { // вперёд-назад
        XMVECTOR dir = Forward();
        XMVECTOR p = XMLoadFloat3(&pos) + d * dir;
        XMStoreFloat3(&pos, p);
    }
    void Strafe(float d) { // влево-вправо
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
        // ограничим pitch, чтобы не переворачивало
        const float limit = XM_PIDIV2 - 0.01f;
        if (pitch > limit) pitch = limit;
        if (pitch < -limit) pitch = -limit;
    }

    // Обновляем view из pos/yaw/pitch
    void UpdateView() {
        XMVECTOR F = Forward();
        XMVECTOR R = Right();
        XMVECTOR U = XMVector3Cross(F, R) * -1.0f; // орто-нормализация нестрога, хватает

        XMVECTOR eye = XMLoadFloat3(&pos);
        XMMATRIX V = XMMatrixLookToLH(eye, F, XMVectorSet(0, 1, 0, 0)); // up как мировая Y
        XMStoreFloat4x4(&view, V);
    }

    // Вектор вперёд/вправо из yaw/pitch
    XMVECTOR Forward() const {
        float cy = cosf(yaw), sy = sinf(yaw);
        float cp = cosf(pitch), sp = sinf(pitch);
        // LH: +Z вперёд
        return XMVector3Normalize(XMVectorSet(sy * cp, sp, cy * cp, 0));
    }
    XMVECTOR Right() const {
        // правый вектор как cross(up, forward)
        XMVECTOR F = Forward();
        XMVECTOR U = XMVectorSet(0, 1, 0, 0);
        return XMVector3Normalize(XMVector3Cross(U, F));
    }

    XMMATRIX View() const { return XMLoadFloat4x4(&view); }
    XMMATRIX Proj() const { return XMLoadFloat4x4(&proj); }
};
