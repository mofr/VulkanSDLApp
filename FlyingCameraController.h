#pragma once

#include "Camera.h"
#include "CameraController.h"

class FlyingCameraController : public CameraController
{
    float movementSpeed;
    float mouseSensitivity;

public:
    FlyingCameraController(float movementSpeed = 1.0f, float mouseSensitivity = 0.3f): movementSpeed(movementSpeed), mouseSensitivity(mouseSensitivity) {}

    void update(Camera& camera, const SDL_Event& event, float deltaTime) {
        if (event.type == SDL_MOUSEMOTION) {
            float xOffset = -event.motion.xrel * mouseSensitivity;
            float yOffset = -event.motion.yrel * mouseSensitivity;
            
            camera.rotateAroundAxis(glm::vec3(0.0f, 1.0f, 0.0f), xOffset);
            camera.rotateAroundAxis(camera.getRight(), yOffset);
        }
    }

    void update(Camera& camera, float deltaTime) {
        const uint8_t* keyState = SDL_GetKeyboardState(nullptr);
        float speed = movementSpeed;
        if (keyState[SDL_SCANCODE_LSHIFT]) {
            speed *= 0.1;
        }
        if (keyState[SDL_SCANCODE_W]) {
            camera.moveForward(speed * deltaTime);
        }
        if (keyState[SDL_SCANCODE_S]) {
            camera.moveForward(-speed * deltaTime);
        }
        if (keyState[SDL_SCANCODE_A]) {
            camera.moveRight(-speed * deltaTime);
        }
        if (keyState[SDL_SCANCODE_D]) {
            camera.moveRight(speed * deltaTime);
        }
        if (keyState[SDL_SCANCODE_C]) {
            camera.moveUp(-speed * deltaTime);
        }
        if (keyState[SDL_SCANCODE_SPACE]) {
            camera.moveUp(speed * deltaTime);
        }
    }
};
