#pragma once

#include "Camera.h"
#include "CameraController.h"

class OrbitCameraController : public CameraController
{
public:
    static constexpr glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    OrbitCameraController(int windowWidth, int windowHeight, glm::vec3 initialPos): 
        windowWidth(windowWidth), windowHeight(windowHeight), initialPos(initialPos) {}

    void lookAt(glm::vec3 lookAtPos) {
        this->lookAtPos = lookAtPos;
    }

    void update(Camera & camera, const SDL_Event & event, [[maybe_unused]] float deltaTime) override {
        if (event.type == SDL_MOUSEMOTION) {
            float normalizedX = static_cast<float>(event.motion.x) / windowWidth - 0.5f;
            cameraAngle = -normalizedX * 360.0f * 4;

            float normalizedY = static_cast<float>(event.motion.y) / windowHeight;
            zoom = 1.0f - normalizedY;

            glm::vec3 pos = glm::rotate(glm::mat4(1.0f), glm::radians(cameraAngle), up) * glm::vec4(initialPos * zoom, 1.0f);
            camera.setPosition(pos);
        }
    }

    void update(Camera& camera, [[maybe_unused]] float deltaTime) override {
        camera.lookAt(lookAtPos);
    }

private:
    int windowWidth;
    int windowHeight;
    float cameraAngle = 0;
    float zoom = 1.0f;
    glm::vec3 initialPos;
    glm::vec3 lookAtPos;
};
