#pragma once
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include "DirectXMath.h"

bool SaveScene(const std::wstring& path);
static bool ParseKV(const std::string& token, std::string& key, std::string& val);
static bool ParseFloat3(const std::string& s, DirectX::XMFLOAT3& out);
bool LoadScene(const std::wstring& path);
bool FileExistsW(const wchar_t* path);