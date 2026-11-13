#ifndef LUX_BRDF_H
#define LUX_BRDF_H

// Shared by the lit mesh and HDR scene shaders (port of Shaders/Basic.frag.hlsl).
float3 SafeDirection(float3 v, float3 fallback)
{
    return dot(v, v) > 1e-10f ? v * rsqrt(dot(v, v)) : fallback;
}

float3 Fresnel(float cosine, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosine), 5.0f);
}

float3 Brdf(float3 base, float metal, float rough, float3 n, float3 v, float3 l)
{
    const float3 h = SafeDirection(v + l, n);
    const float nl = saturate(dot(n, l));
    const float nv = max(saturate(dot(n, v)), 1e-4f);
    const float nh = saturate(dot(n, h));
    const float vh = saturate(dot(v, h));
    const float a = rough * rough;
    const float a2 = a * a;
    const float d = nh * nh * (a2 - 1.0f) + 1.0f;
    const float distribution = a2 / max(3.14159265f * d * d, 1e-10f);
    const float visibility =
        0.5f / max(nl * sqrt(nv * nv * (1.0f - a2) + a2) + nv * sqrt(nl * nl * (1.0f - a2) + a2), 1e-5f);
    const float3 f = Fresnel(vh, lerp(float3(0.04f, 0.04f, 0.04f), base, metal));
    return ((1.0f - f) * (1.0f - metal) * base / 3.14159265f + distribution * visibility * f) * nl;
}

#endif
