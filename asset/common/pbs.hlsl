#ifndef SHADER_PBS_HLSL
#define SHADER_PBS_HLSL

#define PBS_PI 3.1415926

struct PBSLight
{
    float3 position;  float maxDistance;
    float3 intensity; float pad0;
    float3 ambient;   float pad1;
};

// BRDF for simple PBS:
//    (1 - metallic) * diffuse + lerp(F0_dielectric, F0_metal, metallic) * D * G / (4 * cos<I, N> * cos<O, N>)

float3 PBSSchlick(float3 f0, float cosTheta)
{
    float t  = 1 - cosTheta;
    float t2 = t * t;
    return f0 + (1 - f0) * t2 * t2 * t;
}

float PBSGGX(float3 n, float3 h, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float nh = dot(n, h);
    float s  = nh * nh * (a2 - 1) + 1;
    return a2 / (PBS_PI * s * s);
}

float PBSSmithGGX(float3 n, float3 w, float roughness)
{
    float k = roughness * roughness / 2;
    float nw = dot(n, w);
    return nw / (nw * (1 - k) + k);
}

float3 PBSShade(
    float3 wi,
    float3 wo,
    float3 normal,
    float3 albedo,
    float  metallic,
    float  roughness,
    float  f0)
{
    float cosThetaI = dot(wi, normal);
    float cosThetaO = dot(wo, normal);

    float3 result = float3(0, 0, 0);

    if(cosThetaI > 0 && cosThetaO > 0)
    {
        float3 wh = normalize(wi + wo);
        float  cosThetaD = dot(wi, wh);

        float3 diffuse = albedo / PBS_PI;

        float3 dielectricF = PBSSchlick(float3(f0, f0, f0), cosThetaD);
        float3 metallicF = PBSSchlick(albedo, cosThetaD);

        float3 F = lerp(dielectricF, metallicF, metallic);

        float D = PBSGGX(normal, wh, roughness);

        float G = PBSSmithGGX(normal, wi, roughness)
                * PBSSmithGGX(normal, wo, roughness);

        float3 specular = F * D * G / (4 * dot(normal, wo) * dot(normal, wi));

        result = (1 - metallic) * diffuse + specular;
    }

    return result;
}

float3 PBSWithSingleLight(
    float3   wo,
    float3   position,
    float3   normal,
    float3   albedo,
    float    metallic,
    float    roughness,
    PBSLight light)
{
    float dis = distance(light.position, position);
    float3 wi = normalize(light.position - position);

    float3 brdf = PBSShade(
        wi, wo, normal,
        albedo, metallic, roughness, 0.04);

    float lightFactor = 1 - smoothstep(
        light.maxDistance * 0.1, light.maxDistance, dis);

    float3 result =
        light.intensity * max(0, dot(wi, normal)) * brdf +
        light.ambient * albedo;

    return lightFactor * result;
}

#endif // #ifndef SHADER_PBS_HLSL
