#pragma once

#include <glm/glm.hpp>

class Camera
{
    glm::vec3 position;
    glm::quat orientation;

    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;

public:
    Camera(
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),
        glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),  // Identity quaternion
        float fov = 45.0f,
        float aspectRatio = 16.0f/9.0f,
        float nearPlane = 0.1f,
        float farPlane = 100.0f
    ): position(position), orientation(orientation), fov(fov), aspectRatio(aspectRatio), nearPlane(nearPlane), farPlane(farPlane) {
    }

    /*
    Returns view matrix which transforms world coordinates to view space.
    Assumes right-handed coordinates (RHS).
    Camera forward vector is -Z (0, 0, -1).
    Camera right vector is X (1, 0, 0).
    Camera up vector can be passed as a parameter, by default it's Y (0, 1, 0).
    */
    glm::mat4 getViewMatrix() {
        glm::mat4 rotMatrix = glm::mat4_cast(glm::conjugate(orientation));
        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), -position);
        return rotMatrix * translationMatrix;
    }

    /*
    Return projection matrix.
    The projection matrix is used to transform coordinates from camera/eye/view space to clip space.
    It's built for RHS coordinates.
    Input: view space, X right, Y up, -Z away.
    Output: clip space, X right, Y down, Z away. Defined by Vulkan.
    Z=-zFar is projected to Z=1
    Z=-zNear is projected to Z=0
    */
    glm::mat4 getProjectionMatrix() {
        float tanHalfFovy = tan(glm::radians(fov) / 2.0f);

        glm::mat4 proj(0.0f);
        proj[0][0] = 1.0f / (aspectRatio * tanHalfFovy);
        proj[1][1] = -1.0f / tanHalfFovy;  // Note the negative sign for Vulkan's Y-flip
        proj[2][2] = farPlane / (nearPlane - farPlane);
        proj[2][3] = -1.0f;
        proj[3][2] = (nearPlane * farPlane) / (nearPlane - farPlane);
        
        return proj;
    }

    void lookAt(const glm::vec3& target, glm::vec3 const& up = glm::vec3(0, 1, 0)) {
        orientation = glm::quatLookAt(normalize(target - position), up);
    }

    void setPosition(const glm::vec3& newPosition) {
        position = newPosition;
    }

    void setAspectRatio(float newAspectRatio) {
        aspectRatio = newAspectRatio;
    }
};
