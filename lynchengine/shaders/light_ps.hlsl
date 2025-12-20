// light_ps.hlsl
#define MAX_LIGHTS        16
#define LIGHT_TYPE_DIR     0
#define LIGHT_TYPE_POINT   1
#define LIGHT_TYPE_SPOT    2

#define NORMAL_IS_PACKED   1

struct Light
{
    float3 color;
    float intensity; 
    float3 posW;
    float radius; 
    float3 dirW;
    uint type; 
    float cosInner;
    float cosOuter;
    float _pad0;
    float _pad1; 
};

cbuffer CBLighting : register(b0)
{
    float3 camPosWS;
    float debugMode;
    float2 zNearFar;
    float2 _padA;
    uint lightCount;
    float3 _padB;
    float4x4 invViewProj;
    float4x4 dirLightVP;
    
    uint alphaShadowEntity; 
    uint alphaShadowTexId;
    float alphaShadowCutoff;
    float alphaShadowUvScale;
    
    float3 gFogColor;
    float gFogDensity;
    float gFogStartDistance;
    float gFogHeightFalloff;
    float gFogAnisotropy;
    float gAtmosphereCleanliness;
    float3 gSkyCleanColor;
    float _pad3;
    float3 gSkyDirtyColor;
    float _pad4;
    
    Light lights[MAX_LIGHTS]; 
};

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);
RaytracingAccelerationStructure gScene : register(t3);

Texture2D gTex[] : register(t4);

SamplerState gSamp : register(s0);
SamplerState gSampZ : register(s1);

float2 Hash21(float2 p)
{
    float n = dot(p, float2(127.1, 311.7));
    float m = dot(p, float2(269.5, 183.3));
    return frac(sin(float2(n, m)) * 43758.5453);
}

void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.y) < 0.999) ? float3(0, 1, 0) : float3(1, 0, 0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float ShadowRay_DXR(float3 origin, float3 dir, float tMin, float tMax)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery <
        RAY_FLAG_FORCE_NON_OPAQUE |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
    > q;

    q.TraceRayInline(gScene, 0, 0xFF, ray);

    const bool alphaEnabled = (alphaShadowEntity != 0xFFFFFFFFu);

    while (q.Proceed())
    {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            bool isAlphaCaster = alphaEnabled && (q.CandidateInstanceID() == alphaShadowEntity);

            if (isAlphaCaster)
            {
                float t = q.CandidateTriangleRayT();
                float3 hitWS = origin + dir * t;

                float2 uv = hitWS.xz * alphaShadowUvScale;

                uint texSlot = (alphaShadowTexId >= 4u) ? (alphaShadowTexId - 4u) : 0u;

                float a = gTex[texSlot].SampleLevel(gSamp, uv, 0).a;

                if (a < alphaShadowCutoff)
                {
                    
                }
                else
                {
                    q.CommitNonOpaqueTriangleHit();
                    return 0.0; 
                }
            }
            else
            {
                q.CommitNonOpaqueTriangleHit();
                return 0.0;
            }
        }
    }

    return 1.0;
}

void MakeBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.z) < 0.999) ? float3(0, 0, 1) : float3(0, 1, 0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float ShadowDirectionalSoft_DXR(float3 Pws, float3 Nws, float3 LdirWs, float2 uv)
{
    const float tMin = 0.003;
    const float tMax = 200.0;
    const float normalBias = 0.006;

    float3 origin = Pws + Nws * normalBias + LdirWs * tMin;

    const float cone = 0.010;
    const int rays = 12;

    float3 t, b;
    BuildOrthonormalBasis(LdirWs, t, b);

    float sum = 0.0;
    [unroll]
    for (int i = 0; i < rays; ++i)
    {
        float2 r = Hash21(uv * 2048.0 + float2(37.0 * i, 17.0 * i));
        float a = 6.2831853 * r.x;
        float rad = sqrt(max(1e-6, r.y));
        float2 d = rad * float2(cos(a), sin(a));

        float3 dirVar = normalize(LdirWs + cone * (d.x * t + d.y * b));
        sum += ShadowRay_DXR(origin, dirVar, tMin, tMax);
    }
    return sum / rays;
}

float ShadowPointSoft_DXR(float3 Pws, float3 Nws, float3 lightPosWs, float2 uv)
{
    float3 V = lightPosWs - Pws;
    float dist = length(V);
    if (dist <= 1e-4)
        return 1.0;

    float3 Ldir = V / dist;

    const float tMin = 0.003;
    const float normalBias = 0.006;

    float3 origin = Pws + Nws * normalBias + Ldir * tMin;

    float3 T, B;
    MakeBasis(Ldir, T, B);

    const int rays = 12;
    float lightRadius = 0.02 * dist;
    lightRadius = min(lightRadius, 0.25);

    float sum = 0.0;

    [unroll]
    for (int i = 0; i < rays; ++i)
    {
        float2 r = Hash21(uv * 2048.0 + float2(37.0 * i, 17.0 * i));

        float a = 6.2831853 * r.x;
        float rad = sqrt(max(1e-6, r.y));
        float2 d2 = rad * float2(cos(a), sin(a));

        float3 lp = lightPosWs + lightRadius * (d2.x * T + d2.y * B);

        float3 VV = lp - Pws;
        float dd = length(VV);
        float3 dir2 = VV / max(dd, 1e-6);

        float tMax = max(0.0, dd - tMin);

        sum += ShadowRay_DXR(origin, dir2, tMin, tMax);
    }

    return sum / rays;
}

float3 ReconstructWS(float2 uv, float depth01, float4x4 invVP)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p = mul(invVP, float4(ndc, depth01, 1.0));
    return p.xyz / max(p.w, 1e-6);
}

float3 LoadNormalWS(float2 uv)
{
#if NORMAL_IS_PACKED
    float3 n = gNormal.Sample(gSamp, uv).xyz * 2.0 - 1.0;
#else
    float3 n = gNormal.Sample(gSamp, uv).xyz;
#endif
    return normalize(n);
}

float RayleighPhase(float mu)
{
    return 0.75f * (1.0f + mu * mu);
}

float MiePhase(float mu, float g)
{
    float g2 = g * g;
    float denom = pow(1.0f + g2 - 2.0f * g * mu, 1.5f);
    return (1.0f - g2) / max(1e-3f, denom);
}

float3 ComputeSkyColor(float3 viewDir)
{
    float3 sunDir = float3(0, 1, 0);
    if (lightCount > 0 && lights[0].type == LIGHT_TYPE_DIR)
        sunDir = normalize(-lights[0].dirW);

    float mu = dot(viewDir, sunDir);
    float muSat = saturate(mu);

    float tHeight = saturate(viewDir.y * 0.5f + 0.5f);

    float3 cleanBottom = float3(1.0, 0.9, 0.8);
    float3 cleanTop = gSkyCleanColor;
    float3 dirtyBottom = float3(0.9, 0.9, 0.9);
    float3 dirtyTop = gSkyDirtyColor;

    float3 cleanSky = lerp(cleanBottom, cleanTop, tHeight);
    float3 dirtySky = lerp(dirtyBottom, dirtyTop, tHeight);

    float atm = saturate(gAtmosphereCleanliness);
    float3 sky = lerp(cleanSky, dirtySky, atm);

    float3 rayleighColor = float3(0.5, 0.7, 1.0);
    float3 mieColor = float3(1.0, 0.9, 0.8);

    float g = clamp(gFogAnisotropy, -0.95f, 0.95f);

    float rayPhase = RayleighPhase(mu);
    float miePhase = MiePhase(mu, g);

    float rayleighStrength = 0.35f;
    float mieStrength = 0.20f;

    float3 scattering =
        rayleighStrength * rayPhase * rayleighColor +
        mieStrength * miePhase * mieColor;

    sky += scattering;

    float sunDiscExp = 256.0;
    float sunHaloExp = 8.0;
    float sunDisc = pow(muSat, sunDiscExp);
    float sunHalo = pow(muSat, sunHaloExp);

    float sunIntensity = 3.0;

    float3 sunColor = sunIntensity * (sunDisc + 0.2 * sunHalo) * float3(1.0, 1.0, 1.0);
    sky += sunColor;

    sky = 1.0f - exp(-sky * 0.8f);

    return sky;
}

float ComputeFogFactor(float3 worldPos)
{
    float3 camToPos = worldPos - camPosWS;
    float dist = length(camToPos);

    dist = max(0.0f, dist - gFogStartDistance);

    float height = worldPos.y;
    float heightAtten = exp(-max(0.0f, height) * gFogHeightFalloff);

    float opticalDepth = dist * gFogDensity * heightAtten;
    return saturate(1.0f - exp(-opticalDepth));
}

float3 ShadeDirectional(in Light L, float3 P, float3 N, float3 albedo, float2 uv)
{
    float3 Ldir = normalize(-L.dirW); 
    float ndl = saturate(dot(N, Ldir));

    float shadow = ShadowDirectionalSoft_DXR(P, N, Ldir, uv);

    return albedo * L.color * (L.intensity * ndl * shadow);
}

float3 ShadePoint(in Light L, float3 P, float3 N, float3 albedo, float2 uv)
{
    float3 V = L.posW - P;
    float d = length(V);
    if (d > L.radius)
        return 0.0;

    float3 Ldir = V / max(d, 1e-6);
    float ndl = saturate(dot(N, Ldir));

    float atten = saturate(1.0 - d / L.radius);
    atten = atten * atten;

    float shadow = ShadowPointSoft_DXR(P, N, L.posW, uv);

    return albedo * L.color * (L.intensity * ndl * atten * shadow);
}

float3 ShadeSpot(in Light L, float3 P, float3 N, float3 albedo, float2 uv)
{
    float3 V = L.posW - P;
    float d = length(V);
    if (d > L.radius)
        return 0.0;

    float3 Ldir = V / max(d, 1e-6);
    float ndl = saturate(dot(N, Ldir));

    float atten = saturate(1.0 - d / L.radius);
    atten = atten * atten;

    float c = dot(-Ldir, normalize(L.dirW));
    float spot = saturate((c - L.cosOuter) / max(L.cosInner - L.cosOuter, 1e-4));
    spot = spot * spot;

    if (spot <= 1e-4)
        return 0.0;

    float shadow = ShadowPointSoft_DXR(P, N, L.posW, uv);

    return albedo * L.color * (L.intensity * ndl * atten * spot * shadow);
}


struct PSIn
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 DebugView(float debugMode, float2 uv)
{
    if (debugMode >= 1.0 && debugMode < 2.0)
    { 
        return float4(gAlbedo.Sample(gSamp, uv).rgb, 1);
    }
    if (debugMode >= 2.0 && debugMode < 3.0)
    { 
        float3 n = LoadNormalWS(uv);
        return float4(n * 0.5 + 0.5, 1.0);  
    }
    if (debugMode >= 3.0 && debugMode < 4.0)
    { 
        float z = gDepth.Sample(gSampZ, uv).r;
        return float4(z.xxx, 1.0);
    }
    return -1; 
}

float4 main(PSIn i) : SV_Target
{
    
    if (debugMode >= 1u && debugMode <= 4u)
    {
        float4 dv = DebugView(debugMode, i.uv);
        if (dv.x >= 0.0)
            return dv;
    }

    float3 albedo = gAlbedo.Sample(gSamp, i.uv).rgb;
    float z = gDepth.Sample(gSampZ, i.uv).r;

    if (z >= 1.0 - 1e-6)
    {
        float3 wpFar = ReconstructWS(i.uv, 1.0, invViewProj);
        float3 viewDir = normalize(wpFar - camPosWS);
        float3 skyCol = ComputeSkyColor(viewDir);
        return float4(skyCol, 1.0);
    }

    float3 N = LoadNormalWS(i.uv);
    float3 P = ReconstructWS(i.uv, z, invViewProj);

    float3 Lsum = 0.0;
    [loop]
    for (uint k = 0; k < lightCount; ++k)
    {
        Light L = lights[k];
        if (L.type == LIGHT_TYPE_DIR)
        {
            Lsum += ShadeDirectional(L, P, N, albedo, i.uv);
        }
        else if (L.type == LIGHT_TYPE_POINT)
            Lsum += ShadePoint(L, P, N, albedo, i.uv);
        else
            Lsum += ShadeSpot(L, P, N, albedo, i.uv);
    }

    float3 ambient = albedo * 0.03;
    float3 litColor = Lsum + ambient;

    float fogFactor = ComputeFogFactor(P);
    float3 fogCol = gFogColor;

#if 0
float3 viewDir = normalize(P - camPosWS);
float3 skyCol = ComputeSkyColor(viewDir);
fogCol = lerp(fogCol, skyCol, 0.1f); 
#endif

    float3 finalColor = lerp(litColor, fogCol, fogFactor);
    return float4(finalColor, 1.0);
}