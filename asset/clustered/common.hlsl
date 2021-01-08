#ifndef COMMON_HLSL
#define COMMON_HLSL

struct Light
{
    float3 position;  float maxDistance;
    float3 intensity; float pad0;
    float3 ambient;   float pad1;
};

struct AABB
{
    float3 lower;
    float3 upper;
};

struct ClusterRange
{
    int rangeBeg;
    int rangeEnd;
};

float square(float x)
{
    return x * x;
}

bool isLightInAABB(Light light, AABB aabb)
{
    float3 closest_pnt = max(aabb.lower, min(light.position, aabb.upper));
    float3 diff = closest_pnt - light.position;
    float dist2 = dot(diff, diff);
    return dist2 < square(light.maxDistance);
}

#endif // #ifndef COMMON_HLSL
