#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

/*
Returns view matrix which transforms world coordinates to view space.
Assumes right-handed coordinates (RHS).
Camera forward vector is -Z (0, 0, -1).
Camera right vector is X (1, 0, 0).
Camera up vector can be passed as a parameter, by default it's Y (0, 1, 0).
*/
glm::mat4 cameraLookAt(glm::vec3 const& eye, glm::vec3 const& target, glm::vec3 const& up = glm::vec3(0, 1, 0))
{
    glm::vec3 f(normalize(target - eye));
    glm::vec3 r(normalize(cross(f, up)));
    glm::vec3 u(cross(f, r));
    glm::mat4 result(1);
    result[0][0] = r.x;
    result[1][0] = r.y;
    result[2][0] = r.z;
    result[0][1] = u.x;
    result[1][1] = u.y;
    result[2][1] = u.z;
    result[0][2] = f.x;
    result[1][2] = f.y;
    result[2][2] = f.z;
    result[3][0] = -dot(r, eye);
    result[3][1] = -dot(u, eye);
    result[3][2] = -dot(f, eye);
    return result;
}


/*
Return projection matrix.
The projection matrix is used to transform from camera/eye space to clip space.
It's built for RHS coordinates.
Input: view space, X right, Y up, -Z away.
Output: clip space, X right, Y down, Z away.
Z=-zFar is projected to Z=1
Z=-zNear is projected to Z=0
*/
glm::mat4 perspectiveProjection(float verticalFov, float aspectRatio, float zNear, float zFar) {
    float focalLength = 1.0f / std::tan(verticalFov / 2.0f);
    glm::mat4 result(0);
    result[0][0] = focalLength / aspectRatio;
    result[1][1] = focalLength;
    result[2][2] = zFar / (zFar - zNear);
    result[2][3] = 1;
    result[3][2] = -zFar * zNear / (zFar - zNear);
    return result;
}
