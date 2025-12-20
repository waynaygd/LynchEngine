#include "d3d_init.h"
#include "uploader.h"

static float Halton(UINT index, UINT base)
{
	float f = 1.0f;
	float r = 0.0f;
	while (index > 0)
	{
		f /= float(base);
		r += f * float(index % base);
		index /= base;
	}
	return r;
}

static UINT     g_taaSampleIndex = 0;
static XMFLOAT2 g_taaJitterNDC = { 0.0f, 0.0f };

void RenderFrame()
{
	HR(g_alloc[g_frameIndex]->Reset());
	HR(g_cmdList->Reset(g_alloc[g_frameIndex].Get(), nullptr));

	const D3D12_GPU_VIRTUAL_ADDRESS cbBaseGPU =
		g_cbPerObject->GetGPUVirtualAddress() + (UINT64)g_cbStride * g_cbMaxPerFrame * g_frameIndex;
	uint8_t* cbBaseCPU =
		g_cbPerObjectPtr + (size_t)g_cbStride * g_cbMaxPerFrame * g_frameIndex;

	UINT drawIdx = 0;
	leaves_count = 0;

	static LARGE_INTEGER s_freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	static LARGE_INTEGER s_prev = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t; }();
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	float dt = float(double(now.QuadPart - s_prev.QuadPart) / double(s_freq.QuadPart));
	s_prev = now;

	UpdateInput(dt);
	g_cam.UpdateView();

	XMMATRIX V = g_cam.View();
	XMMATRIX P = g_cam.Proj();	

	XMMATRIX VP = XMMatrixMultiply(V, P);

	XMFLOAT2 jitterNDC = { 0.0f, 0.0f };
	if (g_taaEnabled) 
	{
		float h2 = Halton(g_taaSampleIndex & 1023u, 2);
		float h3 = Halton(g_taaSampleIndex & 1023u, 3);
		++g_taaSampleIndex;

		const float jitterScale = 0.5f; 

		float jitterPxX = (h2 - 0.5f) * jitterScale;
		float jitterPxY = (h3 - 0.5f) * jitterScale;

		float w = g_viewport.Width;
		float h = g_viewport.Height;

		jitterNDC.x = (2.0f * jitterPxX) / w;
		jitterNDC.y = (-2.0f * jitterPxY) / h;
	}
	else
	{
		jitterNDC = { 0.0f, 0.0f };
	}

	g_taaJitterNDC = jitterNDC;

	XMFLOAT4X4 currVP;
	XMStoreFloat4x4(&currVP, XMMatrixTranspose(VP));

	XMFLOAT3 dirF{ -0.4f,-1.0f,-0.2f };
	for (auto& A : g_lightsAuthor) if (A.type == LT_Dir) { dirF = A.dirW; break; }
	XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&dirF));
	XMMATRIX lightVP = MakeDirLightVP(lightDir, { 0,0,0 }, 50.0f);

	{
		if (g_lightsAuthor.empty())
			g_lightsAuthor.push_back(LightAuthor{ LT_Dir,{1,1,1},1.0f,{},0.0f,{-0.4f,-1.0f,-0.2f},0,0 });
	}

	static float sunTime = 0.0f;
	sunTime += dt;

	const float dayLength = 30.0f;         
	float tCycle = fmodf(sunTime, dayLength);
	float phase = tCycle / dayLength; 
	float angle = phase * XM_2PI;   

	XMFLOAT3 sunPos = { 0.0f, sinf(angle), cosf(angle) };
	XMVECTOR sunDirV = XMVector3Normalize(XMVectorSet(-sunPos.x, -sunPos.y, -sunPos.z, 0.0f));

	XMFLOAT3 sunDir;
	XMStoreFloat3(&sunDir, sunDirV);
	

	for (auto& A : g_lightsAuthor)
	{
		if (A.type == LT_Dir) {
			A.dirW = sunDir;
			break;
		}
	}


	for (int i = 0; i < GBUF_COUNT; ++i)
		Transition(g_cmdList.Get(), g_gbuf[i].Get(), g_gbufState[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
	Transition(g_cmdList.Get(), g_depthBuffer.Get(), g_depthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	D3D12_CPU_DESCRIPTOR_HANDLE mrt[2] = { g_gbufRTV[0], g_gbufRTV[1] };
	auto dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	g_cmdList->OMSetRenderTargets(2, mrt, FALSE, &dsv);

	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	const float c0[4] = { 0, 0, 0, 1 };
	const float c1[4] = { 0, 0, 1, 1 };
	g_cmdList->ClearRenderTargetView(g_gbufRTV[0], c0, 0, nullptr);
	g_cmdList->ClearRenderTargetView(g_gbufRTV[1], c1, 0, nullptr);
	g_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	g_cmdList->SetGraphicsRootSignature(g_rsGBuffer.Get());
	g_cmdList->SetPipelineState(g_psoGBuffer.Get());

	{
		ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
		g_cmdList->SetDescriptorHeaps(2, heaps);
	}

	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto ResolveTexId = [&](const MeshGPU& m, const Submesh* psm, UINT entityFallback)->UINT {
		if (psm && psm->materialId != UINT(-1) && psm->materialId < m.materialsTexId.size()) {
			UINT t = m.materialsTexId[psm->materialId];
			if (t != UINT(-1) && t < g_textures.size()) return t;
		}
		if (entityFallback != UINT(-1) && entityFallback < g_textures.size()) return entityFallback;
		return (g_texFallbackId < g_textures.size()) ? g_texFallbackId : 0;
		};

	auto ResolveTexIdByMaterial = [&](const MeshGPU& m, UINT materialId, UINT entityFallback)->UINT {
		if (materialId != UINT(-1) && materialId < m.materialsTexId.size()) {
			UINT t = m.materialsTexId[materialId];
			if (t != UINT(-1) && t < g_textures.size()) return t;
		}
		if (entityFallback != UINT(-1) && entityFallback < g_textures.size()) return entityFallback;
		return (g_texFallbackId < g_textures.size()) ? g_texFallbackId : 0;
		};

	for (const Entity& e : g_entities)
	{
		if (e.meshId >= g_meshes.size()) continue;
		MeshGPU& m = g_meshes[e.meshId];

		XMMATRIX S = XMMatrixScaling(e.scale.x, e.scale.y, e.scale.z);
		XMMATRIX Rx = XMMatrixRotationX(XMConvertToRadians(e.rotDeg.x));
		XMMATRIX Ry = XMMatrixRotationY(XMConvertToRadians(e.rotDeg.y));
		XMMATRIX Rz = XMMatrixRotationZ(XMConvertToRadians(e.rotDeg.z));
		XMMATRIX T = XMMatrixTranslation(e.pos.x, e.pos.y, e.pos.z);
		XMMATRIX M = S * Rx * Ry * Rz * T;
		XMMATRIX MIT = XMMatrixTranspose(XMMatrixInverse(nullptr, M));

		CBPerObject c{};
		XMStoreFloat4x4(&c.M, XMMatrixTranspose(M));
		XMStoreFloat4x4(&c.V, XMMatrixTranspose(V));
		XMStoreFloat4x4(&c.P, XMMatrixTranspose(P));
		XMStoreFloat4x4(&c.MIT, XMMatrixTranspose(MIT));
		c.uvMul = e.uvMul;
		c.jitter = g_taaJitterNDC;

		const bool canMeshlet =
			g_useMeshlets &&
			g_meshShadersSupported &&
			(m.srvMeshlets != UINT(-1)) &&
			(!m.meshletRanges.empty());

		if (canMeshlet)
		{
			g_cmdList->SetGraphicsRootSignature(g_rsMeshletGBuffer.Get());
			g_cmdList->SetPipelineState(g_psoMeshletGBuffer.Get());

			for (const auto& r : m.meshletRanges)
			{
				if (drawIdx >= g_cbMaxPerFrame) break;
				std::memcpy(cbBaseCPU + (size_t)drawIdx * g_cbStride, &c, sizeof(c));
				D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = cbBaseGPU + (UINT64)drawIdx * g_cbStride;

				g_cmdList->SetGraphicsRootConstantBufferView(0, gpuAddr);

				Transition(g_cmdList.Get(), m.vb.Get(), m.vbState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

				UINT texId = ResolveTexIdByMaterial(m, r.materialId, e.texId);
				UINT first = r.first;
				UINT dbg = g_debugMeshlets ? 1u : 0u;

				g_cmdList->SetGraphicsRootDescriptorTable(1, SRV_GPU(m.srvVertices));
				g_cmdList->SetGraphicsRootDescriptorTable(2, g_textures[texId].gpu);
				g_cmdList->SetGraphicsRoot32BitConstants(3, 1, &first, 0);
				g_cmdList->SetGraphicsRoot32BitConstants(4, 1, &dbg, 0);

				g_cmdList->DispatchMesh(r.count, 1, 1);

				++drawIdx;
			}
		}
		else
		{
			g_cmdList->SetGraphicsRootSignature(g_rsGBuffer.Get());
			g_cmdList->SetPipelineState(g_psoGBuffer.Get());

			if (drawIdx >= g_cbMaxPerFrame) break;
			std::memcpy(cbBaseCPU + (size_t)drawIdx * g_cbStride, &c, sizeof(c));
			D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = cbBaseGPU + (UINT64)drawIdx * g_cbStride;

			g_cmdList->SetGraphicsRootConstantBufferView(0, gpuAddr);

			Transition(g_cmdList.Get(), m.vb.Get(), m.vbState, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			Transition(g_cmdList.Get(), m.ib.Get(), m.ibState, D3D12_RESOURCE_STATE_INDEX_BUFFER);

			g_cmdList->IASetVertexBuffers(0, 1, &m.vbv);
			g_cmdList->IASetIndexBuffer(&m.ibv);

			if (m.subsets.empty()) {
				UINT texId = ResolveTexId(m, nullptr, e.texId);
				g_cmdList->SetGraphicsRootDescriptorTable(1, g_textures[texId].gpu);
				g_cmdList->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
			}
			else {
				for (const Submesh& sm : m.subsets) {
					UINT texId = ResolveTexId(m, &sm, e.texId);
					g_cmdList->SetGraphicsRootDescriptorTable(1, g_textures[texId].gpu);
					g_cmdList->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
				}
			}

			++drawIdx;
		}
	}

	if (!g_terrainonetile) {
		if (!g_nodes.empty() && g_root < g_nodes.size())
		{
			InitFrustum(P);
			BoundingFrustum frWorld;
			g_frustumProj.Transform(frWorld, XMMatrixInverse(nullptr, V));

			{
				XMFLOAT3 cp = g_cam.pos;
				BoundingBox camBox({ cp.x,cp.y,cp.z }, { 1,1,1 });
				if (frWorld.Contains(camBox) == DirectX::DISJOINT)
					OutputDebugStringA("Warn: camera is NOT inside frustum\n");
			}


			float projScale = ProjScaleFrom(P, g_viewport.Height);
			std::vector<uint32_t> drawNodes; drawNodes.reserve((UINT)g_nodes.size());

			SelectNodes(g_root, g_cam.pos, frWorld,
				projScale, (float)g_lodThresholdPx,
				drawNodes);

			g_cmdList->SetGraphicsRootSignature(g_rsTerrain.Get());

			if (!g_terrainshow_wireframe) {
				g_cmdList->SetPipelineState(g_psoTerrain.Get());
			}
			else {
				g_cmdList->SetPipelineState(g_psoTerrainWF.Get());
			}
			{
				ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
				g_cmdList->SetDescriptorHeaps(2, heaps);
			}

			CBScene sc{};
			XMStoreFloat4x4(&sc.viewProj, XMMatrixTranspose(V * P));
			XMStoreFloat4x4(&sc.view, XMMatrixTranspose(V));
			memcpy(g_cbScenePtr, &sc, sizeof(sc));
			g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());

			UpdateTilesHeight(g_heightMap);

			g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			g_cmdList->IASetVertexBuffers(0, 1, &g_terrainGrid.vbv);
			g_cmdList->IASetIndexBuffer(&g_terrainGrid.ibv);

			int minL = 99, maxL = -1;

			for (uint32_t nid : drawNodes)
			{
				const QNode& q = g_nodes[nid];
				const TileRes& tr = g_tiles[q.tileIndex];

				minL = std::min<int>(minL, g_nodes[nid].level);
				maxL = std::max<int>(maxL, g_nodes[nid].level);

				size_t offset = (size_t)q.tileIndex * g_cbTerrainStride;
				uint8_t* dst = g_cbTerrainTilesPtr + offset;
				memcpy(dst, &tr.cb, sizeof(tr.cb));

				g_cmdList->SetGraphicsRootConstantBufferView(
					0, g_cbTerrainTiles->GetGPUVirtualAddress() + offset);

				g_cmdList->SetGraphicsRootDescriptorTable(2, tr.heightSrv); 
				g_cmdList->SetGraphicsRootDescriptorTable(3, tr.diffuseSrv);

				g_cmdList->DrawIndexedInstanced(g_terrainGrid.indexCount, 1, 0, 0, 0);
				++leaves_count;
			}

			::minL = minL;
			::maxL = maxL;
			drawnodes_size = (int)drawNodes.size();

			if (!g_terrainshow_wireframe) {
				g_cmdList->SetPipelineState(g_psoTerrainSkirt.Get());
				g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				g_cmdList->IASetVertexBuffers(0, 1, &g_terrainSkirt.vbv);
				g_cmdList->IASetIndexBuffer(&g_terrainSkirt.ibv);

				for (uint32_t nid : drawNodes)
				{
					const QNode& q = g_nodes[nid];
					const TileRes& tr = g_tiles[q.tileIndex];

					const size_t off = size_t(q.tileIndex) * g_cbTerrainStride;
					std::memcpy(g_cbTerrainTilesPtr + off, &tr.cb, sizeof(tr.cb));

					g_cmdList->SetGraphicsRootConstantBufferView(0, g_cbTerrainTiles->GetGPUVirtualAddress() + off);
					g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());
					g_cmdList->SetGraphicsRootDescriptorTable(2, tr.heightSrv);
					g_cmdList->SetGraphicsRootDescriptorTable(3, tr.diffuseSrv);

					g_cmdList->DrawIndexedInstanced(g_terrainSkirt.indexCount, 1, 0, 0, 0);
				}
			}			
		}
	}
	else {
		g_cmdList->SetGraphicsRootSignature(g_rsTerrain.Get());
		g_cmdList->SetPipelineState(g_psoTerrain.Get());

		{
			ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
			g_cmdList->SetDescriptorHeaps(2, heaps);
		}

		CBScene sc{};
		XMStoreFloat4x4(&sc.viewProj, XMMatrixTranspose(V * P));
		XMStoreFloat4x4(&sc.view, XMMatrixTranspose(V));
		memcpy(g_cbScenePtr, &sc, sizeof(sc));

		CBTerrainTile cb{};
		cb.tileOrigin = { 0, 0 };
		cb.tileSize = 25.0f;
		cb.heightScale = g_heightMap;        
		memcpy(g_cbTerrainPtr, &cb, sizeof(cb));

		g_cmdList->SetGraphicsRootConstantBufferView(0, g_cbTerrain->GetGPUVirtualAddress()); 
		g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());   
		g_cmdList->SetGraphicsRootDescriptorTable(2, g_textures[terrain_height].gpu);         
		g_cmdList->SetGraphicsRootDescriptorTable(3, g_textures[terrain_diffuse].gpu);       

		g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_cmdList->IASetVertexBuffers(0, 1, &g_terrainGrid.vbv);
		g_cmdList->IASetIndexBuffer(&g_terrainGrid.ibv);
		g_cmdList->DrawIndexedInstanced(g_terrainGrid.indexCount, 1, 0, 0, 0);
	}

	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	for (int i = 0; i < GBUF_COUNT; ++i)
		Transition(g_cmdList.Get(), g_gbuf[i].Get(), g_gbufState[i],
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	Transition(g_cmdList.Get(), g_depthBuffer.Get(), g_depthState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	Transition(g_cmdList.Get(), g_lightingColor.Get(), g_lightingColorState,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_lightingColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	g_cmdList->OMSetRenderTargets(1, &g_lightingColorRTV, FALSE, nullptr);

	const float clearLighting[4] = { 0.06f, 0.06f, 0.08f, 1.0f };

	g_cmdList->ClearRenderTargetView(g_lightingColorRTV, clearLighting, 0, nullptr);

	if (g_hasDXR && g_tlasDirty)
	{
		DX_BuildTLAS_FromEntities();
		g_tlasDirty = false;
	}

	g_cmdList->SetGraphicsRootSignature(g_rsLighting.Get());
	g_cmdList->SetPipelineState(g_psoLighting.Get());
	{
		ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
		g_cmdList->SetDescriptorHeaps(2, heaps);
	}

	g_cmdList->SetGraphicsRootDescriptorTable(0, SRV_GPU(g_gbufAlbedoSRV));

	CBLighting L{};
	L.debugMode = float(g_gbufDebugMode);
	XMStoreFloat4x4(&L.dirLightVP, XMMatrixTranspose(lightVP));

	uint32_t n = (uint32_t)std::min<size_t>(g_lightsAuthor.size(), MAX_LIGHTS);
	for (auto& A : g_lightsAuthor) if (A.type != LT_Point)
		XMStoreFloat3(&A.dirW, XMVector3Normalize(XMLoadFloat3(&A.dirW)));

	for (uint32_t i = 0; i < n; ++i)
	{
		const auto& A = g_lightsAuthor[i];
		LightGPU G{};
		G.type = (uint32_t)A.type;
		G.color = A.color;
		G.intensity = A.intensity;
		XMStoreFloat3(&G.posW, XMLoadFloat3(&A.posW));
		XMStoreFloat3(&G.dirW, XMVector3Normalize(XMLoadFloat3(&A.dirW)));
		G.radius = A.radius;
		G.cosInner = cosf(XMConvertToRadians(A.innerDeg));
		G.cosOuter = cosf(XMConvertToRadians(A.outerDeg));
		L.lights[i] = G;
	}
	L.lightCount = n;
	L.camPosWS = g_cam.pos;
	L.zNearFar = { g_cam.zn, g_cam.zf };

	XMMATRIX invVP = XMMatrixInverse(nullptr, V * P);
	XMStoreFloat4x4(&L.invViewProj, invVP);

	L.fogColor = g_fogColor;
	L.fogDensity = g_fogDensity;
	L.fogStartDistance = g_fogStartDistance;
	L.fogHeightFalloff = g_fogHeightFalloff;
	L.fogAnisotropy = g_fogAnisotropy;
	L.atmosphereCleanliness = g_atmosphereCleanliness;
	L.skyCleanColor = g_skyCleanColor;
	L.skyDirtyColor = g_skyDirtyColor;

	std::memcpy(g_cbLightingPtr, &L, sizeof(L));
	g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbLighting->GetGPUVirtualAddress());

	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_cmdList->DrawInstanced(3, 1, 0, 0);

	Transition(g_cmdList.Get(), g_lightingColor.Get(), g_lightingColorState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	g_lightingColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	Transition(g_cmdList.Get(), g_taaHistory.Get(), g_taaHistoryState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	g_taaHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	auto bbToRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &bbToRT);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		g_rtvHeap->GetCPUDescriptorHandleForHeapStart(), g_frameIndex, g_rtvInc);
	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	const float clearBB[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	g_cmdList->ClearRenderTargetView(rtv, clearBB, 0, nullptr);

	g_cmdList->SetGraphicsRootSignature(g_rsTAA.Get());
	g_cmdList->SetPipelineState(g_psoTAA.Get());

	{
		ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
		g_cmdList->SetDescriptorHeaps(2, heaps);
	}

	CBTAA_CPU cb{};

	XMMATRIX currVP_T = XMMatrixTranspose(VP);

	XMStoreFloat4x4(&cb.currViewProj, currVP_T);

	XMMATRIX invCurrVP = XMMatrixInverse(nullptr, VP);

	XMStoreFloat4x4(&cb.invCurrViewProj, XMMatrixTranspose(invCurrVP));

	if (g_prevViewProjValid)
		cb.prevViewProj = g_prevViewProj;
	else
		cb.prevViewProj = cb.currViewProj;

	cb.jitter = g_taaJitterNDC;
	cb.alpha = g_taaAlpha;

	cb.enableTAA = (g_taaEnabled && g_prevViewProjValid) ? 1.0f : 0.0f;

	std::memcpy(g_cbTAAPtr, &cb, sizeof(cb));

	g_cmdList->SetGraphicsRootConstantBufferView(0, g_cbTAA->GetGPUVirtualAddress());

	g_cmdList->SetGraphicsRootDescriptorTable(1, SRV_GPU(g_lightingColorSRV));

	g_cmdList->SetGraphicsRootDescriptorTable(2, SRV_GPU(g_gbufDepthSRV));

	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_cmdList->DrawInstanced(3, 1, 0, 0);

	auto bbToCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	g_cmdList->ResourceBarrier(1, &bbToCopySrc);

	Transition(g_cmdList.Get(), g_taaHistory.Get(), g_taaHistoryState,
		D3D12_RESOURCE_STATE_COPY_DEST);

	g_cmdList->CopyResource(g_taaHistory.Get(), g_backBuffers[g_frameIndex].Get());

	Transition(g_cmdList.Get(), g_taaHistory.Get(), g_taaHistoryState,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	auto bbBackToRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &bbBackToRT);
	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX12_NewFrame();
	ImGui::NewFrame();

	BuildEditorUI();

	ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		SaveScene(L"assets\\scenes\\autosave.scene");
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
		LoadScene(L"assets\\scenes\\autosave.scene");

	ImGui::Render();

	{
		ID3D12DescriptorHeap* imguiHeaps[] = { g_imguiHeap.Get() };
		g_cmdList->SetDescriptorHeaps(1, imguiHeaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList.Get());
	}

	auto bbToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &bbToPresent);

	{
		g_prevViewProj = currVP;
		g_prevViewProjValid = true;
	}

	HR(g_cmdList->Close());
	ID3D12CommandList* lists[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, lists);

	HR(g_swapChain->Present(0, 0));

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));
	if (g_fence->GetCompletedValue() < fenceToWait) {
		HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

void WaitForGPU() {
	if (!g_fence || !g_cmdQueue) return;

	const UINT64 fenceToWait = g_fenceValue++;
	HR(g_cmdQueue->Signal(g_fence.Get(), fenceToWait));

	if (!g_fenceEvent) {
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!g_fenceEvent) {
			HR(HRESULT_FROM_WIN32(GetLastError()));
		}
	}

	HR(g_fence->SetEventOnCompletion(fenceToWait, g_fenceEvent));
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

void UpdateInput(float dt)
{
	if (!g_appActive) return;

	float speed = g_cam.moveSpeed * ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4.0f : 1.0f);
	if (GetAsyncKeyState('W') & 0x8000) g_cam.Walk(+speed * dt);
	if (GetAsyncKeyState('S') & 0x8000) g_cam.Walk(-speed * dt);
	if (GetAsyncKeyState('D') & 0x8000) g_cam.Strafe(+speed * dt);
	if (GetAsyncKeyState('A') & 0x8000) g_cam.Strafe(-speed * dt);
	if (GetAsyncKeyState('Q') & 0x8000) g_cam.UpDown(-speed * dt);
	if (GetAsyncKeyState('E') & 0x8000) g_cam.UpDown(+speed * dt);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return 0;

	switch (msg) {
	case WM_DESTROY: 
		DX_Shutdown();
		PostQuitMessage(0); 
		return 0;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) { DestroyWindow(hWnd); }
		break;
	case WM_SIZE:
	{
		if (wParam == SIZE_MINIMIZED) return 0;
		UINT w = LOWORD(lParam), h = HIWORD(lParam);
		if (w == 0 || h == 0) return 0;

		if (g_dxReady) {
			DX_Resize(w, h);
		}
		else {
			g_pendingW = w; g_pendingH = h;
		}
		return 0;
	}
	case WM_ACTIVATE:
	{
		const WORD code = LOWORD(wParam);
		if (code == WA_INACTIVE) {
			g_appActive = false;
			g_mouseLook = false;
			ReleaseCapture();
			ClipCursor(nullptr);
			ShowCursor(TRUE);
			g_mouseHasPrev = false; 
		}
		else {
			g_appActive = true;
			ShowCursor(TRUE);
		}
		return 0;
	}
	case WM_SETFOCUS:
		g_appActive = true;
		return 0;

	case WM_KILLFOCUS:
		g_appActive = false;
		g_mouseLook = false;
		ReleaseCapture();
		ClipCursor(nullptr);
		ShowCursor(TRUE);
		g_mouseHasPrev = false;
		return 0;

	case WM_LBUTTONDOWN:
		g_mouseLook = true;
		SetCapture(hWnd);

		g_lastMouse.x = GET_X_LPARAM(lParam);
		g_lastMouse.y = GET_Y_LPARAM(lParam);
		g_mouseHasPrev = true;
		return 0;
	case WM_LBUTTONUP:
		g_mouseLook = false;
		ReleaseCapture();
		g_mouseHasPrev = false;
		return 0;
	case WM_MOUSEMOVE:
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureMouse) break;

		if (g_mouseLook)
		{
			POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			if (!g_mouseHasPrev) {
				g_lastMouse = p;
				g_mouseHasPrev = true;
				return 0;
			}

			int dx = p.x - g_lastMouse.x;
			int dy = p.y - g_lastMouse.y;
			g_lastMouse = p; 

			g_cam.AddYawPitch(dx * g_cam.mouseSens, -dy * g_cam.mouseSens);
			g_cam.UpdateView();
			return 0;
		}
		break;
	}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
	const wchar_t* kClassName = L"LynchEngine";
	wchar_t window_name[25] = L"LynchEngine FPS: ";

	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClassName;
	RegisterClassExW(&wc);

	RECT rc{ 0,0,1280,720 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

	g_hWnd = CreateWindowExW(
		0, kClassName, window_name,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInst, nullptr);

	ShowWindow(g_hWnd, nCmdShow);
	InitD3D12(g_hWnd, 1280, 720);

	LARGE_INTEGER freq{};
	QueryPerformanceFrequency(&freq);

	LARGE_INTEGER prev{};
	QueryPerformanceCounter(&prev);

	double accTitle = 0.0;
	int frameCounter = 0;
	double fpsShown = 0.0;

	CoUninitialize();

	MSG msg{};
	for (;;) {
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) return (int)msg.wParam;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		double dt = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
		prev = now;

		accTitle += dt;
		frameCounter++;

		if (accTitle >= 0.5) {
			fpsShown = frameCounter / accTitle;
			frameCounter = 0;
			accTitle = 0.0;

			std::wstring title = std::format(L"LynchEngine FPS: {}", (int)std::round(fpsShown));
			SetWindowTextW(g_hWnd, title.c_str());
		}
		RenderFrame();
		Sleep(1);
	}
}