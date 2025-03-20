#pragma once

#include "Camera.h"

class CameraController
{
public:
    virtual void update(Camera& camera, SDL_Event const& event, float deltaTime) = 0;
    virtual void update(Camera& camera, float deltaTime) = 0;
};
