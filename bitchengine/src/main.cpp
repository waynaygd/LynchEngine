#include "d3d_init.h"
#include "uploader.h"

void RenderFrame()
{
	// ===== 0) Reset =====
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

	// ===== 1) GEOMETRY PASS -> GBUFFER (MRT) =====

	// ★ Переводим все GBuffer в RENDER_TARGET (и обновляем их текущие состояния)
	for (int i = 0; i < GBUF_COUNT; ++i)
		Transition(g_cmdList.Get(), g_gbuf[i].Get(), g_gbufState[i], D3D12_RESOURCE_STATE_RENDER_TARGET);

	Transition(g_cmdList.Get(), g_depthBuffer.Get(), depthStateB, D3D12_RESOURCE_STATE_DEPTH_WRITE);


	// MRT + DSV
	D3D12_CPU_DESCRIPTOR_HANDLE mrt[2] = { g_gbufRTV[0], g_gbufRTV[1] };
	auto dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	g_cmdList->OMSetRenderTargets(2, mrt, FALSE, &dsv);

	// viewport/scissor
	g_cmdList->RSSetViewports(1, &g_viewport);
	g_cmdList->RSSetScissorRects(1, &g_scissor);

	// clear MRT+depth
	const float c0[4] = { 0, 0, 0, 1 }; // Albedo  (как в CreateGBuffer)
	const float c1[4] = { 0, 0, 1, 1 }; // Normal  (как в CreateGBuffer: в+Z)
	g_cmdList->ClearRenderTargetView(g_gbufRTV[0], c0, 0, nullptr);
	g_cmdList->ClearRenderTargetView(g_gbufRTV[1], c1, 0, nullptr);
	g_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// RS/PSO (GBuffer)
	g_cmdList->SetGraphicsRootSignature(g_rsGBuffer.Get());
	g_cmdList->SetPipelineState(g_psoGBuffer.Get());

	// ★ В этом пассе мы сэмплируем текстуры материалов => нужна и SRV‑heap, и SAMP‑heap
	{
		ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
		g_cmdList->SetDescriptorHeaps(2, heaps);
	}

	using namespace DirectX;
	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const Entity& e : g_entities)
	{
		const MeshGPU& m = g_meshes[e.meshId];
		const TextureGPU& t = g_textures[e.texId];

		// World
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
		XMStoreFloat4x4(&c.MIT, XMMatrixTranspose(MIT));  // <- та же схема
		c.uvMul = e.uvMul;

		if (drawIdx >= g_cbMaxPerFrame) { /* опционально: assert или увеличь maxPerFrame */ }
		std::memcpy(cbBaseCPU + (size_t)drawIdx * g_cbStride, &c, sizeof(c));

		D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = cbBaseGPU + (UINT64)drawIdx * g_cbStride;
		g_cmdList->SetGraphicsRootConstantBufferView(0, gpuAddr);

		// t0 = текстура материала
		g_cmdList->SetGraphicsRootDescriptorTable(1, t.gpu);

		g_cmdList->IASetVertexBuffers(0, 1, &m.vbv);
		g_cmdList->IASetIndexBuffer(&m.ibv);
		g_cmdList->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);

		++drawIdx;
	}
	if (!g_terrainonetile) {
		if (!g_nodes.empty() && g_root < g_nodes.size())
		{
			// 1) Фрустум мира
			InitFrustum(P);
			BoundingFrustum frWorld;
			g_frustumProj.Transform(frWorld, XMMatrixInverse(nullptr, V));

			// sanity: камера должна быть ВНУТРИ фрустума
			{
				XMFLOAT3 cp = g_cam.pos;
				BoundingBox camBox({ cp.x,cp.y,cp.z }, { 1,1,1 });
				if (frWorld.Contains(camBox) == DirectX::DISJOINT)
					OutputDebugStringA("Warn: camera is NOT inside frustum\n");
			}

			const XMMATRIX VP = V * P;

			// 2) Выбор узлов
			float projScale = ProjScaleFrom(P, g_viewport.Height);
			std::vector<uint32_t> drawNodes; drawNodes.reserve((UINT)g_nodes.size());

			SelectNodes(g_root, g_cam.pos, frWorld,
				projScale, (float)g_lodThresholdPx,
				drawNodes);

			// 3) RS/PSO + heaps
			g_cmdList->SetGraphicsRootSignature(g_rsTerrain.Get());
			g_cmdList->SetPipelineState(g_psoTerrain.Get());
			{
				ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
				g_cmdList->SetDescriptorHeaps(2, heaps);
			}

			// 4) CBScene (НЕ транспонируем для фрустума; в CB — ТРАНСПОНИРУЕМ)
			CBScene sc{};
			XMStoreFloat4x4(&sc.viewProj, XMMatrixTranspose(V * P));
			XMStoreFloat4x4(&sc.view, XMMatrixTranspose(V));
			memcpy(g_cbScenePtr, &sc, sizeof(sc));
			g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());

			UpdateTilesHeight(g_heightMap);

			// 5) Общая сетка на все тайлы
			g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			g_cmdList->IASetVertexBuffers(0, 1, &g_terrainGrid.vbv);
			g_cmdList->IASetIndexBuffer(&g_terrainGrid.ibv);

			// 6) Рисуем выбранные листья
			for (uint32_t nid : drawNodes)
			{
				const QNode& q = g_nodes[nid];
				const TileRes& tr = g_tiles[q.tileIndex];

				// смещение для этого тайла
				size_t offset = (size_t)q.tileIndex * g_cbTerrainStride;
				uint8_t* dst = g_cbTerrainTilesPtr + offset;
				memcpy(dst, &tr.cb, sizeof(tr.cb));

				g_cmdList->SetGraphicsRootConstantBufferView(
					0, g_cbTerrainTiles->GetGPUVirtualAddress() + offset);

				g_cmdList->SetGraphicsRootDescriptorTable(2, tr.heightSrv);  // t0
				g_cmdList->SetGraphicsRootDescriptorTable(3, tr.diffuseSrv); // t1

				g_cmdList->DrawIndexedInstanced(g_terrainGrid.indexCount, 1, 0, 0, 0);
				++leaves_count;
			}
			/*
			g_cmdList->SetPipelineState(g_psoTerrainSkirt.Get());
			g_cmdList->IASetVertexBuffers(0, 1, &g_terrainSkirt.vbv);
			g_cmdList->IASetIndexBuffer(&g_terrainSkirt.ibv);

			for (uint32_t nid : drawNodes)
			{
				const QNode& q = g_nodes[nid];
				const TileRes& tr = g_tiles[q.tileIndex];

				const size_t off = size_t(q.tileIndex) * g_cbTerrainStride;
				// если выше уже писали CB — можно НЕ копировать ещё раз; оставлю для простоты:
				std::memcpy(g_cbTerrainTilesPtr + off, &tr.cb, sizeof(tr.cb));

				g_cmdList->SetGraphicsRootConstantBufferView(0, g_cbTerrainTiles->GetGPUVirtualAddress() + off); // b0 (CBTerrainTile)
				g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());              // b1 (CBScene)
				g_cmdList->SetGraphicsRootDescriptorTable(2, tr.heightSrv);  // t0 (height)
				g_cmdList->SetGraphicsRootDescriptorTable(3, tr.diffuseSrv); // t1 (diffuse)

				g_cmdList->DrawIndexedInstanced(g_terrainSkirt.indexCount, 1, 0, 0, 0);
			}
			*/
		}
	}
	else {
		g_cmdList->SetGraphicsRootSignature(g_rsTerrain.Get());
		g_cmdList->SetPipelineState(g_psoTerrain.Get());

		// после смены RS заново подключаем SRV/SAMP кучи
		{
			ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
			g_cmdList->SetDescriptorHeaps(2, heaps);
		}

		// b1: CBScene (если VS террейна использует viewProj [+ view])
		CBScene sc{};
		XMStoreFloat4x4(&sc.viewProj, XMMatrixTranspose(V * P));
		XMStoreFloat4x4(&sc.view, XMMatrixTranspose(V)); // если не нужен — убери эту строку
		memcpy(g_cbScenePtr, &sc, sizeof(sc));

		// b0: CBTerrainTile
		CBTerrainTile cb{};
		cb.tileOrigin = { 0, 0 };
		cb.tileSize = 25.0f;
		cb.heightScale = g_heightMap;          // VS делает y = (h-0.5)*heightScale
		memcpy(g_cbTerrainPtr, &cb, sizeof(cb));

		g_cmdList->SetGraphicsRootConstantBufferView(0, g_cbTerrain->GetGPUVirtualAddress()); // b0
		g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbScene->GetGPUVirtualAddress());   // b1
		g_cmdList->SetGraphicsRootDescriptorTable(2, g_textures[terrain_height].gpu);         // t0
		g_cmdList->SetGraphicsRootDescriptorTable(3, g_textures[terrain_diffuse].gpu);        // t1

		g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_cmdList->IASetVertexBuffers(0, 1, &g_terrainGrid.vbv);
		g_cmdList->IASetIndexBuffer(&g_terrainGrid.ibv);
		g_cmdList->DrawIndexedInstanced(g_terrainGrid.indexCount, 1, 0, 0, 0);
	}

	// ===== 2) GBuffer -> SRV для LIGHTING =====
	for (int i = 0; i < GBUF_COUNT; ++i)
		Transition(g_cmdList.Get(), g_gbuf[i].Get(), g_gbufState[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	Transition(g_cmdList.Get(), g_depthBuffer.Get(), depthState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);


	// ===== 3) LIGHTING PASS -> BACKBUFFER =====

	// backbuffer: PRESENT -> RT
	auto bbToRT = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_cmdList->ResourceBarrier(1, &bbToRT);

	// RTV backbuffer
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		g_rtvHeap->GetCPUDescriptorHandleForHeapStart(), g_frameIndex, g_rtvInc);
	g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	const float clearBB[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
	g_cmdList->ClearRenderTargetView(rtv, clearBB, 0, nullptr);

	// RS/PSO (Lighting)
	g_cmdList->SetGraphicsRootSignature(g_rsLighting.Get());
	g_cmdList->SetPipelineState(g_psoLighting.Get());

	// ★ В этом пассе читаем GBuffer (SRV) — сэмплер тут обычно не нужен,
	// но если шейдер ожидает s0 — тоже подключи обе кучи для простоты:
	{
		ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get(), g_sampHeap.Get() };
		g_cmdList->SetDescriptorHeaps(2, heaps);
	}

	// Таблица SRV t0..t2: albedo/normal/position — подряд в g_srvHeap
	g_cmdList->SetGraphicsRootDescriptorTable(0, SRV_GPU(g_gbufAlbedoSRV));

	if (g_lightsAuthor.empty()) { // стартовый directional
		g_lightsAuthor.push_back(LightAuthor{ LT_Dir,{1,1,1},1.0f,{},0.0f,{-0.4f,-1.0f,-0.2f},0,0 });
	}

	CBLightingGPU L{};
	L.debugMode = float(g_gbufDebugMode);
	
	uint32_t n = (uint32_t)std::min<size_t>(g_lightsAuthor.size(), MAX_LIGHTS);

	for (auto& A : g_lightsAuthor) {
		if (A.type != LT_Point) {
			XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&A.dirW));
			XMStoreFloat3(&A.dirW, d);
		}
	}

	for (uint32_t i = 0; i < n; ++i) {
		const auto& A = g_lightsAuthor[i];
		LightGPU G{};
		G.type = (uint32_t)A.type;
		G.color = A.color;
		G.intensity = A.intensity;

		// ВАЖНО: оставляем в WORLD
		G.posW = A.posW;
		G.dirW = A.dirW; // нормализуй при редактировании
		G.radius = A.radius;
		G.cosInner = cosf(XMConvertToRadians(A.innerDeg));
		G.cosOuter = cosf(XMConvertToRadians(A.outerDeg));

		L.lights[i] = G;
	}
	L.lightCount = n;
	L.camPosVS = { 0,0,0 };

	// матрицы оставляй как было
	XMMATRIX invP = XMMatrixInverse(nullptr, P);
	XMStoreFloat4x4(&L.invP, XMMatrixTranspose(invP));

	XMMATRIX invV = XMMatrixInverse(nullptr, V);
	XMStoreFloat4x4(&L.invV, XMMatrixTranspose(invV)); // пусть лежит, PS его не обязан трогать

	std::memcpy(g_cbLightingPtr, &L, sizeof(L));
	g_cmdList->SetGraphicsRootConstantBufferView(1, g_cbLighting->GetGPUVirtualAddress());


	// fullscreen triangle
	g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_cmdList->DrawInstanced(3, 1, 0, 0);
	

	// ===== 4) IMGUI поверх бэкбуфера =====
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

	// ===== 5) Backbuffer: RT -> PRESENT =====
	auto bbToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		g_backBuffers[g_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_cmdList->ResourceBarrier(1, &bbToPresent);

	// submit + present + sync
	HR(g_cmdList->Close());
	ID3D12CommandList* lists[] = { g_cmdList.Get() };
	g_cmdQueue->ExecuteCommandLists(1, lists);
	HR(g_swapChain->Present(1, 0));

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

	// Event должен существовать
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
			// запомним — применим после InitD3D12
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
			g_mouseHasPrev = false; // чтобы не было рывка при возврате
		}
		else {
			g_appActive = true;
			// курсор показываем; захватывать начнём только по ЛКМ
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
				// если по какой-то причине не было базовой точки — установим и выходим
				g_lastMouse = p;
				g_mouseHasPrev = true;
				return 0;
			}

			int dx = p.x - g_lastMouse.x;
			int dy = p.y - g_lastMouse.y;
			g_lastMouse = p; // обновляем «предыдущую» на текущую

			g_cam.AddYawPitch(dx * g_cam.mouseSens, -dy * g_cam.mouseSens);
			g_cam.UpdateView();
			return 0;
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		int delta = GET_WHEEL_DELTA_WPARAM(wParam); // 120 за «щелчок»
		g_cam.fovY = std::clamp(g_cam.fovY - float(delta) * 0.0005f, XM_PI / 12.0f, XM_PI / 1.2f);
		g_cam.SetLens(g_cam.fovY, g_cam.aspect, g_cam.zn, g_cam.zf);
		return 0;
	}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
	const wchar_t* kClassName = L"BitchEngine";
	wchar_t window_name[25] = L"bitchengine FPS: ";

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

			std::wstring title = std::format(L"bitchengine FPS: {}", (int)std::round(fpsShown));
			SetWindowTextW(g_hWnd, title.c_str());
		}
		RenderFrame();
		Sleep(1);
	}
}