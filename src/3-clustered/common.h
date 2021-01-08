#pragma once

#include "../common/mesh.h"

using Mesh = common::MeshWithViewTransform;

struct Light
{
    Float3 lightPosition;  float maxLightDistance = 0;
    Float3 lightIntensity; float pad0 = 0;
    Float3 lightAmbient;   float pad1 = 0;
};
