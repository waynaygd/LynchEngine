// d3d_init.cpp
#include "d3d_init.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <imgui/imgui_internal.h>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

void InitImGui(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_Init(
        g_device.Get(),
        kFrameCount,                      
        DXGI_FORMAT_R8G8B8A8_UNORM,         
        g_imguiHeap.Get(),                  
        g_imguiHeap->GetCPUDescriptorHandleForHeapStart(),
        g_imguiHeap->GetGPUDescriptorHandleForHeapStart()
    );

    io.Fonts->AddFontDefault();   
    bool okBuild = io.Fonts->Build();
    IM_ASSERT(okBuild && "Font atlas build failed (STB truetype disabled?)");

    ImGui_ImplDX12_CreateDeviceObjects();
}

void DX_CreateImGuiHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    HR(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_imguiHeap)));
}

void ShutdownImGui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

int g_selectedEntity = -1;

void BuildEditorUI()
{
    ImGui::SetNextWindowSize(ImVec2(420, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Editor")) { ImGui::End(); return; }

    if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
    {
        if (ImGui::BeginTabItem("Scene"))
        {
            if (ImGui::Button("Save Scene")) {
                if (!SaveScene(L"assets\\scenes\\autosave.scene"))
                    OutputDebugStringA("SaveScene failed\n");
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Scene")) {
                if (!LoadScene(L"assets\\scenes\\autosave.scene"))
                    OutputDebugStringA("LoadScene failed\n");
            }

            ImGui::Separator();

            // список сущностей
            for (int i = 0; i < (int)g_entities.size(); ++i) {
                std::string label = "Entity " + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), g_selectedEntity == i))
                    g_selectedEntity = i;
            }

            ImGui::Separator();
            if (ImGui::Button("Add Cube")) {
                UINT cube = CreateCubeMeshGPU();
                UINT tex = 0u;
                Scene_AddEntity(cube, tex, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
            }

            if (g_selectedEntity >= 0 && g_selectedEntity < (int)g_entities.size()) {
                Entity& e = g_entities[g_selectedEntity];
                ImGui::Text("Transform");
                ImGui::DragFloat3("Position", &e.pos.x, 0.01f);
                ImGui::DragFloat3("Rotation (deg)", &e.rotDeg.x, 0.1f);
                ImGui::DragFloat3("Scale", &e.scale.x, 0.01f, 0.01f, 100.0f);

                ImGui::SliderFloat("UV multiplier", &e.uvMul, 0.1f, 32.0f, "%.2f");

                if (ImGui::TreeNode("Bindings")) {
                    if (ImGui::BeginListBox("Mesh")) {
                        for (UINT i = 0; i < g_meshes.size(); ++i) {
                            bool sel = (e.meshId == i);
                            char buf[32]; sprintf_s(buf, "mesh %u", i);
                            if (ImGui::Selectable(buf, sel)) e.meshId = i;
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndListBox();
                    }
                    if (ImGui::BeginListBox("Texture")) {
                        for (UINT i = 0; i < g_textures.size(); ++i) {
                            bool sel = (e.texId == i);
                            char buf[32]; sprintf_s(buf, "tex %u", i);
                            if (ImGui::Selectable(buf, sel)) e.texId = i;
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndListBox();
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Sampling"))
        {
            static const char* addrNames[] = { "Wrap","Mirror","Clamp","Border","MirrorOnce" };
            static const char* filtNames[] = { "Point","Linear","Anisotropic" };

            ImGui::Text("Address Mode:");
            ImGui::Combo("##addr", &g_uiAddrMode, addrNames, IM_ARRAYSIZE(addrNames));

            ImGui::Text("Filter:");
            ImGui::Combo("##filter", &g_uiFilter, filtNames, IM_ARRAYSIZE(filtNames));

            if (g_uiFilter == 2) {
                int prev = g_uiAniso;
                if (ImGui::SliderInt("Anisotropy", &g_uiAniso, 1, 16)) {
                    DX_FillSamplers();
                }
            }
            ImGui::Text("Sampler idx = %d", g_uiAddrMode * 3 + g_uiFilter);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GBuffer"))
        {
            static const char* modes[] = { "Shaded", "Albedo", "Normal", "Depth" };
            ImGui::Combo("View", &g_gbufDebugMode, modes, IM_ARRAYSIZE(modes));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights"))
        {
            if (ImGui::Button("Add Directional")) g_lightsAuthor.push_back(LightAuthor{ LT_Dir,{1,1,1},1 });
            ImGui::SameLine();
            if (ImGui::Button("Add Point")) {
                LightAuthor a; a.type = LT_Point; a.posW = { 0,2,0 };
                a.radius = 6; a.intensity = 3; g_lightsAuthor.push_back(a);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Spot")) {
                LightAuthor a; a.type = LT_Spot; a.posW = { -2,2.5f,-1 };
                a.dirW = { 0.6f,-0.5f,0.6f }; a.radius = 8; a.innerDeg = 18; a.outerDeg = 24;
                a.intensity = 5; g_lightsAuthor.push_back(a);
            }

            ImGui::Separator();
            for (int i = 0; i < (int)g_lightsAuthor.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(("Light " + std::to_string(i)).c_str(), g_selectedLight == i))
                    g_selectedLight = i;
                ImGui::PopID();
            }

            if (g_selectedLight >= 0 && g_selectedLight < (int)g_lightsAuthor.size()) {
                auto& L = g_lightsAuthor[g_selectedLight];
                const char* types[] = { "Directional","Point","Spot" };
                int t = (int)L.type;
                if (ImGui::Combo("Type", &t, types, IM_ARRAYSIZE(types))) L.type = (LightType)t;

                ImGui::ColorEdit3("Color", &L.color.x);
                ImGui::DragFloat("Intensity", &L.intensity, 0.05f, 0.0f, 100.0f);
                if (L.type != LT_Dir) {
                    ImGui::DragFloat3("Position (W)", &L.posW.x, 0.1f);
                    ImGui::DragFloat("Radius", &L.radius, 0.1f, 0.1f, 100.0f);
                }
                if (L.type != LT_Point) {
                    ImGui::DragFloat3("Direction (W)", &L.dirW.x, 0.01f, -1.0f, 1.0f);
                }
                if (L.type == LT_Spot) {
                    ImGui::DragFloat("Inner (deg)", &L.innerDeg, 0.1f, 0.0f, 89.0f);
                    ImGui::DragFloat("Outer (deg)", &L.outerDeg, 0.1f, L.innerDeg, 90.0f);
                }
                if (ImGui::Button("Delete")) {
                    g_lightsAuthor.erase(g_lightsAuthor.begin() + g_selectedLight);
                    g_selectedLight = -1;
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Terrain"))
        {
            if (ImGui::SliderFloat("Height", &g_heightMap, 0.0f, 25.0f)) {
                UpdateTilesHeight(g_heightMap);
            }
            ImGui::Checkbox("One Tile Mode", &g_terrainonetile);
            ImGui::Checkbox("Show Wireframe", &g_terrainshow_wireframe);
            ImGui::Text("Frustum Tiles showed: %d (LOD %d..%d)", (int)leaves_count, (int)minL, (int)maxL);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}


void DX_CreateDeviceAndQueue()
{
#if defined(_DEBUG)
    if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif

    HR(CreateDXGIFactory1(IID_PPV_ARGS(&g_factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; g_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
    }
    HR(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_device)));

    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(g_device->CreateCommandQueue(&q, IID_PPV_ARGS(&g_cmdQueue)));
}

void DX_CreateFenceAndUploadList()
{
    HR(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValue = 1;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HR(g_fenceEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAlloc)));
    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAlloc.Get(), nullptr,
        IID_PPV_ARGS(&g_uploadList)));
    HR(g_uploadList->Close());
}

void DX_CreateSwapchain(HWND hWnd, UINT w, UINT h)
{
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.BufferCount = kFrameCount;
    sc.Width = w; sc.Height = h;
    sc.Format = g_backBufferFormat;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.SampleDesc = { 1,0 };

    ComPtr<IDXGISwapChain1> sc1;
    HR(g_factory->CreateSwapChainForHwnd(g_cmdQueue.Get(), hWnd, &sc, nullptr, nullptr, &sc1));
    HR(sc1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void DX_CreateRTVs()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE start(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; ++i) {
        HR(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(start, i, g_rtvInc);
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
    }
}

void DX_CreateGBuffer(UINT w, UINT h)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 3;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_gbufRTVHeap)));
    g_gbufRTVInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    DXGI_FORMAT fmtAlbedo = DXGI_FORMAT_R8G8B8A8_UNORM;  
    DXGI_FORMAT fmtNormal = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT fmtPosition = DXGI_FORMAT_R16G16B16A16_FLOAT;

    auto makeRT = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out, const FLOAT clear[4])
        {
            CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
                fmt, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            D3D12_CLEAR_VALUE cv{}; cv.Format = fmt;
            cv.Color[0] = clear[0]; cv.Color[1] = clear[1]; cv.Color[2] = clear[2]; cv.Color[3] = clear[3];

            HR(g_device->CreateCommittedResource(
                &heapDefault, D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                &cv,
                IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
        };

    const FLOAT clrA[4] = { 0,0,0,1 };
    const FLOAT clrN[4] = { 0,0,1,1 };

    makeRT(fmtAlbedo, g_gbufAlbedo, clrA);
    makeRT(fmtNormal, g_gbufNormal, clrN);

    g_gbuf[0] = g_gbufAlbedo;
    g_gbuf[1] = g_gbufNormal;

    g_gbufState[0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_gbufState[1] = D3D12_RESOURCE_STATE_RENDER_TARGET;

    auto rtvStart = g_gbufRTVHeap->GetCPUDescriptorHandleForHeapStart();
    g_gbufRTV[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 0, g_gbufRTVInc);
    g_gbufRTV[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 1, g_gbufRTVInc);

    g_device->CreateRenderTargetView(g_gbufAlbedo.Get(), nullptr, g_gbufRTV[0]);
    g_device->CreateRenderTargetView(g_gbufNormal.Get(), nullptr, g_gbufRTV[1]);

    auto makeSRV2D = [&](ID3D12Resource* res, DXGI_FORMAT fmt, UINT slot)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = fmt;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            g_device->CreateShaderResourceView(res, &sd, SRV_CPU(slot));
        };

    UINT base0 = SRV_Alloc();  
    UINT base1 = SRV_Alloc();  
    UINT base2 = SRV_Alloc(); 

    assert(base1 == base0 + 1 && base2 == base0 + 2);

    g_gbufAlbedoSRV = base0;
    g_gbufNormalSRV = base1;
    g_gbufDepthSRV = base2;

    makeSRV2D(g_gbufAlbedo.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, g_gbufAlbedoSRV);
    makeSRV2D(g_gbufNormal.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, g_gbufNormalSRV);
    makeSRV2D(g_depthBuffer.Get(), DXGI_FORMAT_R32_FLOAT, g_gbufDepthSRV);

    assert(g_gbufNormalSRV == g_gbufAlbedoSRV + 1);
    assert(g_gbufDepthSRV == g_gbufAlbedoSRV + 2);

}

void CreateGBufferRSandPSO()
{
 
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;     
    range.BaseShaderRegister = 0; 
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_ANISOTROPIC;
    samp.MaxAnisotropy = 8;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samp.ShaderRegister = 0;
    samp.RegisterSpace = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    rs.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rsGBuffer)));

    auto vs = CompileShaderFromFile(L"shaders\\gbuf_vs.hlsl", "main", "vs_5_1");
    auto ps = CompileShaderFromFile(L"shaders\\gbuf_ps.hlsl", "main", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      { "POSITION",0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "NORMAL",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rsGBuffer.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.InputLayout = { inputElems, _countof(inputElems) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_psoGBuffer)));
}

void CreateLightingRSandPSO()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 3;     
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[1].Descriptor.ShaderRegister = 1;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampLin{};
    sampLin.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampLin.AddressU = sampLin.AddressV = sampLin.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampLin.ShaderRegister = 0;

    D3D12_STATIC_SAMPLER_DESC sampDepth{};
    sampDepth.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampDepth.AddressU = sampDepth.AddressV = sampDepth.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDepth.ShaderRegister = 1;

    D3D12_STATIC_SAMPLER_DESC samps[] = { sampLin, sampDepth };

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 2;
    rs.pStaticSamplers = samps;
    rs.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rsLighting)));

    auto lvs = CompileShaderFromFile(L"shaders\\light_vs.hlsl", "main", "vs_5_1");
    auto lps = CompileShaderFromFile(L"shaders\\light_ps.hlsl", "main", "ps_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rsLighting.Get();
    pso.VS = { lvs->GetBufferPointer(), lvs->GetBufferSize() };
    pso.PS = { lps->GetBufferPointer(), lps->GetBufferSize() };
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState = rast;
    D3D12_DEPTH_STENCIL_DESC ds{}; // OFF
    ds.DepthEnable = FALSE; ds.StencilEnable = FALSE;
    pso.DepthStencilState = ds;
    pso.InputLayout = { nullptr, 0 }; 
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = g_backBufferFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_psoLighting)));
}

void CreateTerrainRSandPSO()
{
    D3D12_DESCRIPTOR_RANGE rHeight{};
    rHeight.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rHeight.NumDescriptors = 1;
    rHeight.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE rDiffuse{};
    rDiffuse.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rDiffuse.NumDescriptors = 1;
    rDiffuse.BaseShaderRegister = 1; 

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;     
    srvRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[4]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &rHeight;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &rDiffuse;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_ANISOTROPIC;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxAnisotropy = 8;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(params);
    rs.pParameters = params;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    if (err && err->GetBufferSize()) {
        OutputDebugStringA((char*)err->GetBufferPointer());
    }
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rsTerrain)));

    auto ter_vs = CompileShaderFromFile(L"shaders\\terrain_vs.hlsl", "VSBase", "vs_5_1");
    auto ter_ps = CompileShaderFromFile(L"shaders\\terrain_ps.hlsl", "main", "ps_5_1");
    if (!ter_vs || !ter_ps) {
        MessageBoxA(nullptr, "terrain VS/PS failed to compile (see debug output)", "Shader Error", MB_OK);
        return; 
    }

    D3D12_INPUT_ELEMENT_DESC ilBase[] = {
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // uv

    };

    D3D12_INPUT_ELEMENT_DESC ilSkirt[] = {
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // uv
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,    0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // skirtK
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rsTerrain.Get();
    pso.VS = { ter_vs->GetBufferPointer(), ter_vs->GetBufferSize() };
    pso.PS = { ter_ps->GetBufferPointer(), ter_ps->GetBufferSize() };
    pso.InputLayout = { ilBase, _countof(ilBase) };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = TRUE;

    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_psoTerrain)));

    auto ter_skirt_vs = CompileShaderFromFile(L"shaders\\terrain_vs.hlsl", "VSSkirt", "vs_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_skirt = {};
    pso_skirt.pRootSignature = g_rsTerrain.Get();
    pso_skirt.VS = { ter_skirt_vs->GetBufferPointer(), ter_skirt_vs->GetBufferSize() };
    pso_skirt.PS = { ter_ps->GetBufferPointer(), ter_ps->GetBufferSize() };
    pso_skirt.InputLayout = { ilSkirt, _countof(ilSkirt) };
    pso_skirt.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_skirt.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_skirt.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_skirt.RasterizerState.DepthBias = 2;
    pso_skirt.RasterizerState.SlopeScaledDepthBias = 1.0f;
    pso_skirt.RasterizerState.DepthBiasClamp = 0.0f;

    pso_skirt.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso_skirt.SampleMask = UINT_MAX;
    pso_skirt.NumRenderTargets = 2;
    pso_skirt.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_skirt.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso_skirt.DSVFormat = g_depthFormat;
    pso_skirt.SampleDesc = { 1, 0 };
    pso_skirt.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    HR(g_device->CreateGraphicsPipelineState(&pso_skirt, IID_PPV_ARGS(&g_psoTerrainSkirt)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_wireframe{};
    pso_wireframe.pRootSignature = g_rsTerrain.Get();
    pso_wireframe.VS = { ter_vs->GetBufferPointer(), ter_vs->GetBufferSize() };
    pso_wireframe.PS = { ter_ps->GetBufferPointer(), ter_ps->GetBufferSize() };
    pso_wireframe.InputLayout = { ilBase, _countof(ilBase) };
    pso_wireframe.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    pso_wireframe.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    pso_wireframe.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    pso_wireframe.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso_wireframe.DepthStencilState.DepthEnable = TRUE;

    pso_wireframe.SampleMask = UINT_MAX;
    pso_wireframe.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_wireframe.NumRenderTargets = 2;
    pso_wireframe.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_wireframe.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso_wireframe.DSVFormat = g_depthFormat;
    pso_wireframe.SampleDesc = { 1, 0 };

    HR(g_device->CreateGraphicsPipelineState(&pso_wireframe, IID_PPV_ARGS(&g_psoTerrainWF)));


}

const DXGI_FORMAT DEPTH_RES_FMT = DXGI_FORMAT_R32_TYPELESS;
const DXGI_FORMAT DEPTH_DSV_FMT = DXGI_FORMAT_D32_FLOAT;   
const DXGI_FORMAT DEPTH_SRV_FMT = DXGI_FORMAT_R32_FLOAT; 

void DX_CreateDepth(UINT w, UINT h)
{
    const DXGI_FORMAT DepthResFmt = DXGI_FORMAT_R32_TYPELESS;
    const DXGI_FORMAT DepthDSVFmt = DXGI_FORMAT_D32_FLOAT;  
    const DXGI_FORMAT DepthSRVFmt = DXGI_FORMAT_R32_FLOAT;   

    g_depthFormat = DepthDSVFmt;

    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    g_depthState = initial;

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    HR(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)));

    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DepthResFmt, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DepthDSVFmt;            
    clear.DepthStencil = { 1.0f, 0 };

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&g_depthBuffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DepthDSVFmt;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(
        g_depthBuffer.Get(), &dsv,
        g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DepthSRVFmt;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;

    UINT slot = SRV_Alloc();
    g_device->CreateShaderResourceView(g_depthBuffer.Get(), &sd, SRV_CPU(slot));
    g_gbufDepthSRV = slot;
}

void DX_CreateSRVHeap(UINT numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    HR(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_srvHeap)));

    g_srvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srvCapacity = numDescriptors;
    g_srvNext = 0;
}

void DX_CreateFrameCmdObjects()
{
    for (UINT i = 0; i < kFrameCount; ++i)
        HR(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i])));

    HR(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0].Get(), nullptr,
        IID_PPV_ARGS(&g_cmdList)));
    HR(g_cmdList->Close());
}

void DX_BeginUpload()
{
    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));
}

void DX_EndUploadAndFlush()
{
    HR(g_uploadList->Close());
    ID3D12CommandList* lists[]{ g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();
    g_uploadKeepAlive.clear();
}

void DX_CreateRootSigAndPSO()
{
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;     
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors = 1;    
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rp[3]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;              
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 0;         
    rs.pStaticSamplers = nullptr;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
        | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> sig, err;
    HR(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    HR(g_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&g_rootSig)));

    auto vs = CompileShaderFromFile(L"shaders\\cube_vs.hlsl", "main", "vs_5_1");
    auto ps = CompileShaderFromFile(L"shaders\\cube_ps.hlsl", "main", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = g_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { il, _countof(il) };
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = g_backBufferFormat;
    pso.DSVFormat = g_depthFormat;
    pso.SampleDesc = { 1,0 };

    auto blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& rt0 = blend.RenderTarget[0];
    rt0.BlendEnable = TRUE;
    rt0.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt0.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState = blend;

    HR(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_pso)));
}

void DX_InitCamera(UINT w, UINT h)
{
    g_viewport = { 0.f,0.f,(float)w,(float)h,0.f,1.f };
    g_scissor = { 0,0,(LONG)w,(LONG)h };
    g_cam.pos = { 0,0,-5 };
    g_cam.yaw = 0.f; g_cam.pitch = 0.f;
    g_cam.SetLens(XM_PIDIV4, float(w) / float(h), 0.5f, 3000.0f);
    g_cam.UpdateView();
}

auto dump = [](UINT id) {
    const auto& T = g_textures[id];
    OutputDebugStringW((std::wstring(L"SRV idx=") + std::to_wstring(T.heapIndex) +
        L" gpu.ptr=" + std::to_wstring(T.gpu.ptr) + L"\n").c_str());
    };

void DX_LoadAssets()
{

    UINT texDefault = RegisterTextureFromFile(L"assets\\textures\\default_white.png");
    UINT texError = RegisterTextureFromFile(L"assets\\textures\\error_tex.png");

    UINT texZagar = RegisterTextureFromFile(L"assets\\textures\\zagarskih_normal.dds");
    UINT texBogdan = RegisterTextureFromFile(L"assets\\textures\\bogdanov_diffuse.png");
    UINT texArsen = RegisterTextureFromFile(L"assets\\textures\\markaryan_diffuse.png");

    g_texFallbackId = texError;

    UINT meshZagarskih = RegisterOBJ(L"assets\\models\\zagarskih.obj");
    UINT meshBodganov = RegisterOBJ(L"assets\\models\\bogdanov.obj");
    UINT meshMarkaryan = RegisterOBJ(L"assets\\models\\markaryan.obj");

    UINT meshSponza = RegisterOBJ(L"assets\\models\\sponza.obj");

    Scene_AddEntity(meshZagarskih, texZagar, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
    Scene_AddEntity(meshBodganov, texBogdan, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
    Scene_AddEntity(meshMarkaryan, texArsen, { 0,0,0 }, { 0,0,0 }, { 1,1,1 });
    Scene_AddEntity(meshSponza, texDefault, { 0,0,0 }, { 0,0,0 }, { 0.01,0.01,0.01 });
}

void DX_LoadTerrain()
{
    ComPtr<ID3D12Resource> vbUp, ibUp;

    HR(g_uploadAlloc->Reset());
    HR(g_uploadList->Reset(g_uploadAlloc.Get(), nullptr));

    CreateTerrainGrid(g_device.Get(), g_uploadList.Get(), 64, vbUp, ibUp);
    CreateTerrainSkirt(g_device.Get(), g_uploadList.Get(), 64, vbUp, ibUp);

    HR(g_uploadList->Close());
    ID3D12CommandList* lists1[] = { g_uploadList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists1);
    WaitForGPU();

    g_pendingUploads.clear();
    vbUp.Reset(); 
    ibUp.Reset(); 

    terrain_diffuse = RegisterTextureFromFile(L"assets\\terrain\\terrain_diffuse.png");
    terrain_normal = RegisterTextureFromFile(L"assets\\terrain\\terrain_normal.png");
    terrain_height = RegisterTextureFromFile(L"assets\\terrain\\terrain_height.png");

    BuildLeafTilesGrid(uiGridN, uiWorldSize, g_heightMap, g_uiSkirtDepth,
        g_textures[terrain_height].gpu,
        g_textures[terrain_diffuse].gpu);

    g_cbTerrainStride = (sizeof(CBTerrainTile) + 255) & ~255u;
    UINT total = g_cbTerrainStride * g_tiles.size();
    if (total == 0) total = g_cbTerrainStride;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(total);

    HR(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&g_cbTerrainTiles)));

    HR(g_cbTerrainTiles->Map(0, nullptr, (void**)&g_cbTerrainTilesPtr));
}



void CreateCB()
{
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   desc = CD3DX12_RESOURCE_DESC::Buffer(UINT64(CB_SIZE_ALIGNED) * MAX_OBJECTS);

    HR(g_device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)));

    CD3DX12_RANGE range(0, 0);
    HR(g_cb->Map(0, &range, reinterpret_cast<void**>(&g_cbPtr)));
}

void CreatePerObjectCB(UINT maxPerFrame)
{
    g_cbMaxPerFrame = maxPerFrame;
    g_cbStride = (UINT)((sizeof(CBPerObject) + 255) & ~255u);
    const UINT totalSize = g_cbStride * g_cbMaxPerFrame * kFrameCount;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

    HR(g_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&g_cbPerObject)));

    HR(g_cbPerObject->Map(0, nullptr, reinterpret_cast<void**>(&g_cbPerObjectPtr)));
}

void CreateTerrainCB()
{
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(256);
    HR(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cbTerrain)));
    HR(g_cbTerrain->Map(0, nullptr, reinterpret_cast<void**>(&g_cbTerrainPtr)));

    HR(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cbScene)));
    HR(g_cbScene->Map(0, nullptr, (void**)&g_cbScenePtr));
}

template<class T>
static void CreateUploadCB(ComPtr<ID3D12Resource>& cb, uint8_t*& mappedPtr) {
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(T) + 255) & ~255);
    HR(g_device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&cb)));
    HR(cb->Map(0, nullptr, reinterpret_cast<void**>(&mappedPtr)));
}

void InitD3D12(HWND hWnd, UINT w, UINT h)
{
    g_hWnd = hWnd;

    DX_CreateDeviceAndQueue();
    DX_CreateFenceAndUploadList();
    DX_CreateSwapchain(hWnd, w, h);
    DX_CreateRTVs();

    DX_CreateSRVHeap(256);
    DX_CreateSamplerHeap();
    DX_FillSamplers();

    DX_CreateDepth(w, h);
    DX_CreateGBuffer(w, h);
    CreateGBufferRSandPSO();

    CreateTerrainRSandPSO();

    CreateLightingRSandPSO();

    CreatePerObjectCB(1024);
    CreateTerrainCB();
    CreateUploadCB<CBLighting>(g_cbLighting, g_cbLightingPtr);

    if (g_lightsAuthor.empty()) {
        g_lightsAuthor.push_back(LightAuthor{
            LT_Dir, {1,1,1}, 1.0f,
            {}, 0.0f,
            {-0.4f,-1.0f,-0.2f}, 0, 0
            });
    }

    DX_CreateImGuiHeap();
    InitImGui(g_hWnd);

    DX_CreateFrameCmdObjects();

    DX_LoadTerrain();
    DX_LoadAssets();    
    DX_CreateRootSigAndPSO();
    DX_InitCamera(w, h);

    g_dxReady = true;

    if (g_pendingW && g_pendingH &&
        (g_pendingW != g_width || g_pendingH != g_height))
    {
        DX_Resize(g_pendingW, g_pendingH);
        g_pendingW = g_pendingH = 0;
    }
}

void DX_Resize(UINT w, UINT h)
{
    if (!g_device || !g_swapChain) return; 
    if (w == 0 || h == 0) return;
    if (w == g_width && h == g_height) return;

    g_width = w; g_height = h;

    WaitForGPU();

    for (UINT i = 0; i < kFrameCount; ++i) g_backBuffers[i].Reset();
    g_rtvHeap.Reset();

    HR(g_swapChain->ResizeBuffers(kFrameCount, w, h, g_backBufferFormat, 0));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    HR(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; ++i) {
        HR(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])));
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtvStart, i, g_rtvInc);
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, h);
    }

    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    g_depthState = initial;

    g_depthBuffer.Reset();

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DEPTH_DSV_FMT;                 
    clear.DepthStencil = { 1.0f, 0 };

    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DEPTH_RES_FMT,                       
        w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    HR(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
        IID_PPV_ARGS(&g_depthBuffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DEPTH_DSV_FMT;                 
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_depthBuffer.Get(), &dsv,
        g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    DX_DestroyGBuffer();
    DX_CreateGBuffer(w, h);

    g_viewport = { 0.f, 0.f, float(w), float(h), 0.f, 1.f };
    g_scissor = { 0, 0, (LONG)w, (LONG)h };
    g_cam.SetLens(g_cam.fovY, float(w) / float(h), g_cam.zn, g_cam.zf);
}


void DX_Shutdown()
{
    WaitForGPU();

    ShutdownImGui();              
    g_imguiHeap.Reset();

    g_cb.Reset();
    g_tex.Reset();
    for (auto& m : g_meshes) { }
    g_meshes.clear(); g_textures.clear(); g_entities.clear();

    for (auto& bb : g_backBuffers) bb.Reset();
    g_depthBuffer.Reset();
    g_rtvHeap.Reset();
    g_dsvHeap.Reset();
    g_srvHeap.Reset();

    g_cmdList.Reset();
    for (auto& a : g_alloc) a.Reset();
    g_uploadList.Reset();
    g_uploadAlloc.Reset();

    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    g_fence.Reset();
    g_cmdQueue.Reset();
    g_swapChain.Reset();
    g_device.Reset();

#if defined(_DEBUG)
    if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
#endif
}

static D3D12_FILTER ToFilter(int uiFilter)
{
    switch (uiFilter) {
    case 0: return D3D12_FILTER_MIN_MAG_MIP_POINT;
    case 1: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    case 2: return D3D12_FILTER_ANISOTROPIC;
    default: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    }
}

inline D3D12_TEXTURE_ADDRESS_MODE ToAddress(int a)
{
    switch (a) {
    default:
    case ADDR_WRAP:        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case ADDR_MIRROR:      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case ADDR_CLAMP:       return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case ADDR_BORDER:      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case ADDR_MIRROR_ONCE: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    }
}

void DX_CreateSamplerHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    d.NumDescriptors = ADDR_COUNT * FILT_COUNT;
    d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_sampHeap)));
    g_sampInc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void DX_FillSamplers()
{
    auto cpu0 = g_sampHeap->GetCPUDescriptorHandleForHeapStart();

    for (int addr = 0; addr < ADDR_COUNT; ++addr)
    {
        for (int fil = 0; fil < FILT_COUNT; ++fil)
        {
            D3D12_SAMPLER_DESC s{};
            if (fil == FILT_POINT) {
                s.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                s.MaxAnisotropy = 1;
            }
            else if (fil == FILT_LINEAR) {
                s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                s.MaxAnisotropy = 1;
            }
            else { 
                s.Filter = D3D12_FILTER_ANISOTROPIC;
                s.MaxAnisotropy = std::clamp(g_uiAniso, 1, 16);
            }

            D3D12_TEXTURE_ADDRESS_MODE am = ToAddress(addr);
            s.AddressU = s.AddressV = s.AddressW = am;

            s.MipLODBias = 0.0f;
            s.MinLOD = 0.0f;
            s.MaxLOD = D3D12_FLOAT32_MAX;

            if (am == D3D12_TEXTURE_ADDRESS_MODE_BORDER) {
                s.BorderColor[0] = 0.0f;
                s.BorderColor[1] = 0.0f;
                s.BorderColor[2] = 0.0f;
                s.BorderColor[3] = 1.0f;
            }

            int idx = addr * FILT_COUNT + fil;
            auto dst = CD3DX12_CPU_DESCRIPTOR_HANDLE(cpu0, idx, g_sampInc);
            g_device->CreateSampler(&s, dst);
        }
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE DX_GetSamplerHandle(int addrMode, int filterMode)
{
    UINT idx = (UINT)(addrMode * 3 + filterMode);
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        g_sampHeap->GetGPUDescriptorHandleForHeapStart(), idx, g_sampInc);
}

UINT SRV_Alloc()
{
    if (g_srvNext >= g_srvCapacity)
    {
        MessageBoxA(nullptr, "SRV heap exhausted! Increase heap size.", "SRV_Alloc Error", MB_ICONERROR);
        throw std::runtime_error("SRV heap exhausted");
    }
    return g_srvNext++;
}

D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPU(UINT index)
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        index, g_srvInc
    );
}

D3D12_GPU_DESCRIPTOR_HANDLE SRV_GPU(UINT index)
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        g_srvHeap->GetGPUDescriptorHandleForHeapStart(),
        index, g_srvInc
    );
}

void GBuffer_GetRTVs(D3D12_CPU_DESCRIPTOR_HANDLE out[3])
{
    auto start = g_gbufRTVHeap->GetCPUDescriptorHandleForHeapStart();
    out[0] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 0, g_gbufRTVInc);
    out[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 1, g_gbufRTVInc);
    out[2] = CD3DX12_CPU_DESCRIPTOR_HANDLE(start, 2, g_gbufRTVInc);
}

void DX_DestroyGBuffer()
{
    g_gbufAlbedo.Reset();
    g_gbufNormal.Reset();
    g_gbufRTVHeap.Reset();
    g_gbufAlbedoSRV = g_gbufNormalSRV = g_gbufDepthSRV = UINT_MAX;
}

void Transition(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES& stateVar,
    D3D12_RESOURCE_STATES newState)
{
    if (stateVar == newState) return;
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(res, stateVar, newState);
    cmd->ResourceBarrier(1, &b);
    stateVar = newState;
}
