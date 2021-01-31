#pragma once

#include "./common.h"

namespace common
{

    struct PBSLight
    {
        Float3 lightPosition;  float maxLightDistance = 0;
        Float3 lightIntensity; float pad0 = 0;
        Float3 lightAmbient;   float pad1 = 0;
    };

} // namespace common
