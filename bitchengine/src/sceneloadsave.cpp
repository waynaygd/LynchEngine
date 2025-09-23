#include "sceneloadsave.h"
#include "someshit.h"

bool SaveScene(const std::wstring& path)
{
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    ofs << "# bitchengine-scene v1\n";
    ofs << std::setprecision(6) << std::fixed;

    for (const Entity& e : g_entities)
    {
        ofs << "entity "
            << "mesh=" << e.meshId << " "
            << "tex=" << e.texId << " "
            << "pos=" << e.pos.x << "," << e.pos.y << "," << e.pos.z << " "
            << "rot=" << e.rotDeg.x << "," << e.rotDeg.y << "," << e.rotDeg.z << " "
            << "scale=" << e.scale.x << "," << e.scale.y << "," << e.scale.z
            << "\n";
    }
    return true;
}

static bool ParseKV(const std::string& token, std::string& key, std::string& val)
{
    auto eq = token.find('=');
    if (eq == std::string::npos) return false;
    key = token.substr(0, eq);
    val = token.substr(eq + 1);
    return true;
}

static bool ParseFloat3(const std::string& s, DirectX::XMFLOAT3& out)
{
    std::stringstream ss(s);
    char comma1 = 0, comma2 = 0;
    if (!(ss >> out.x)) return false;
    if (!(ss >> comma1) || comma1 != ',') return false;
    if (!(ss >> out.y)) return false;
    if (!(ss >> comma2) || comma2 != ',') return false;
    if (!(ss >> out.z)) return false;
    return true;
}

bool LoadScene(const std::wstring& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    std::string line;
    g_entities.clear();

    int lineNo = 0;
    while (std::getline(ifs, line))
    {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string word;
        ss >> word;
        if (word != "entity") continue; 

        Entity e{};
        e.meshId = 0; e.texId = 0;
        e.pos = { 0,0,0 }; e.rotDeg = { 0,0,0 }; e.scale = { 1,1,1 };

        std::vector<std::string> tokens;
        while (ss >> word) tokens.push_back(word);

        for (const auto& t : tokens)
        {
            std::string k, v;
            if (!ParseKV(t, k, v)) continue;

            if (k == "mesh") {
                e.meshId = (UINT)((std::max)(0, std::stoi(v)));
            }
            else if (k == "tex") {
                e.texId = (UINT)((std::max)(0, std::stoi(v)));
            }
            else if (k == "pos") {
                ParseFloat3(v, e.pos);
            }
            else if (k == "rot") {
                ParseFloat3(v, e.rotDeg);
            }
            else if (k == "scale") {
                ParseFloat3(v, e.scale);
            }
        }

        if (e.meshId >= g_meshes.size()) {
            OutputDebugStringA(("LoadScene: skip entity, mesh out of range at line " + std::to_string(lineNo) + "\n").c_str());
            continue;
        }
        if (e.texId >= g_textures.size()) {
            OutputDebugStringA(("LoadScene: skip entity, tex out of range at line " + std::to_string(lineNo) + "\n").c_str());
            continue;
        }

        g_entities.push_back(e);
    }
    return true;
}
