How to build and run
====================
Setup build dir: `meson setup build` (`meson setup build --reconfigure` to reconfigure)

Build: `meson compile -C build`

Process assets: `./build/ProcessAssets`

Run: `./build/VulkanSDLApp`

Coordinate systems
==================

fragPosition is in the world coordinates and calculated by applying model transform matrix.
Then the fragPosition is transformed to a clip space by applying view and projection transformations.


m = model space coordinates

w = world space coordinates

v = view space coordinates

c = clip space coordinates


M - model matrix to transform model space coordinates to world space. M = TRS (from right to left: scale, rotate, translate).

V - view matrix to transform world space coordinates to view space. Calculated as inverse of the camera TRS matrix.

P - projection matrix to transform view space coordinates to clip space. Perspective is applied here.


w = M * m

v = V * w

c = P * v

https://johannesugb.github.io/gpu-programming/setting-up-a-proper-vulkan-projection-matrix/
https://www.vincentparizet.com/blog/posts/vulkan_perspective_matrix/

World space, logical coordinates (project-specific)
---------------------------------------------------
Right-handed coordinate system (RHS).

View/camera space
-----------------
Right-handed coordinate system (RHS).

Camera view is aligned with clip space, which means:
- Camera right vector is X (1, 0, 0)
- Camera up vector is Y (0, 1, 0)
- Camera forward vector is -Z (0, 0, -1)

Vulkan Normalized Device Coordinates (NDC)
------------------------------------------
Right-handed coordinate system (RHS).

X = -1..1
Y = -1..1
Z = 0..1

Everything else is clipped.

X goes right.
Y goes down.
Forward direction is Z.

GLM_FORCE_DEPTH_ZERO_TO_ONE is used to follow the convention.

Obj file coordinates
--------------------
Right-handed coordinate system (RHS).

X goes right.
Y goes up.
Forward direction is -Z.

I haven't found any specification which would confirm it, but most files I've seen follow this convention.

glm
---
Extremely useful explanation on glm logic: https://community.khronos.org/t/confused-when-using-glm-for-projection/108548/4

glm assumes right-handed coordinates by default.

glm does a handedness swap due to a historical reasons to act as a drop-in replacement for opengl functions glOrtho and glFrustum.

The usual orthographic or perspective projection matrix generated with glm::ortho or glm::perspective (or glOrtho, glFrustum) inverts the z-axis leading to a handedness swap.

Since I don't want to follow the "hidden" rule of the handedness swap, I use my own functions to build view and projection matrices.

Front face
----------
During rasterization vulkan needs to know if we are looking at the front face or at the back face.

Vertex winding order in the framebuffer space is used to decide the face orientation.

Textures
--------

**obj file vt**

The origin at the lower-left corner, meaning the Y-axis (or 't' coordinate) increases from bottom to top.

I inverse V value during loading to match with images top-to-bottom layout.

**stb_image**

The pixel data consists of *y scanlines of *x pixels, with each pixel consisting of N interleaved 8-bit components; the first pixel pointed to is top-left-most in the image.

**VkImage**

I haven't found any specification regarding this, I think that it works in tandem with shaders, where increasing vertical coordinate (V/S/Y) leads to increasing memory access address.

**glsl**

I haven't found any specification regarding this, I think that it works in tandem with image layout, where increasing vertical coordinate (V/S/Y) leads to increasing memory access address.

**gltf**

In gltf texture coordinates assume the top-left corner as the origin.

Environment cubemaps
====================

Equirectangular panoramas are converted to cubemaps at compile time (ProcessAssets.cpp).
