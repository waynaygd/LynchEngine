// d3d_init.h
#pragma once
#include "someshit.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

// ─── imgui ──────────────────────────────────────────────────────────────────
void InitImGui(HWND hWnd);
void ShutdownImGui();
void BuildEditorUI();

// ─── низкоуровневые шаги ────────────────────────────────────────────────────
void DX_CreateDeviceAndQueue();
void DX_CreateFenceAndUploadList();
void DX_CreateSwapchain(HWND hWnd, UINT w, UINT h);
void DX_CreateRTVs();
void DX_CreateGBuffer(UINT w, UINT h);
void CreateGBufferRSandPSO();
void CreateLightingRSandPSO();
void CreateTerrainRSandPSO();
void DX_CreateDepth(UINT w, UINT h);
void DX_CreateSRVHeap(UINT numDescriptors = 1);
void DX_CreateImGuiHeap();
void DX_CreateFrameCmdObjects();
void CreateCB();

// ─── upload-фаза (один список команд) ───────────────────────────────────────
void DX_BeginUpload();
void DX_EndUploadAndFlush();

// ─── пайплайн и камера ──────────────────────────────────────────────────────
void DX_CreateRootSigAndPSO();     // твой текущий RS/PSO
void DX_InitCamera(UINT w, UINT h);

// ─── «крючки» для ассетов (пока пусто) ──────────────────────────────────────
void DX_LoadAssets();  // здесь позже вызовешь UploadOBJ/UploadTexture/…
// (сейчас можно оставить пустым — всё соберётся)
void DX_LoadTerrain();

// ─── главный оркестратор ────────────────────────────────────────────────────
void InitD3D12(HWND hWnd, UINT w, UINT h);

void DX_Resize(UINT w, UINT h);
void DX_Shutdown();

void DX_CreateSamplerHeap();
static D3D12_FILTER ToFilter(int uiFilter);
static D3D12_TEXTURE_ADDRESS_MODE ToAddress(int uiAddr);
void DX_FillSamplers();
D3D12_GPU_DESCRIPTOR_HANDLE DX_GetSamplerHandle(int addrMode, int filterMode);

UINT SRV_Alloc();
D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPU(UINT index);
D3D12_GPU_DESCRIPTOR_HANDLE SRV_GPU(UINT index);

void DX_DestroyGBuffer();
void GBuffer_GetRTVs(D3D12_CPU_DESCRIPTOR_HANDLE out[3]);
void Transition(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES& stateVar,
    D3D12_RESOURCE_STATES newState);

void CreatePerObjectCB(UINT maxPerFrame);
void CreateTerrainCB();
