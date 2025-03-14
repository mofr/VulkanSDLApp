#pragma once

#include <glm/glm.hpp>

glm::mat4 cameraLookAt(glm::vec3 const& eye, glm::vec3 const& center, glm::vec3 const& up)
{
    glm::vec3 f(normalize(center - eye));
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
