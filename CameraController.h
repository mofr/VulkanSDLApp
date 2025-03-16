#pragma once

#include "CameraFunctions.h"

class CameraController
{
public:
    static constexpr glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    CameraController(int windowWidth, int windowHeight, glm::vec3 initialPos): 
        windowWidth(windowWidth), windowHeight(windowHeight), initialPos(initialPos) {}

    void lookAt(glm::vec3 lookAtPos) {
        this->lookAtPos = lookAtPos;
    }

    void update(const SDL_Event & event) {
        if (event.type == SDL_MOUSEMOTION) {
            float normalizedX = static_cast<float>(event.motion.x) / windowWidth - 0.5f;
            float cameraAngle = -normalizedX * 360.0f * 4;

            float normalizedY = static_cast<float>(event.motion.y) / windowHeight;
            float zoom = 1.0f - normalizedY;

            pos = glm::rotate(glm::mat4(1.0f), glm::radians(cameraAngle), up) * glm::vec4(initialPos * zoom, 1.0f);
        }
    }

    glm::mat4 getView() {
        return cameraLookAt(pos, lookAtPos);
    }

private:
    int windowWidth;
    int windowHeight;
    glm::vec3 initialPos;
    glm::vec3 pos;
    glm::vec3 lookAtPos;
};
