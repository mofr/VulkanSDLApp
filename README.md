Coordinate systems
==================

fragPosition is in the world coordinates and calculated by applying model transform matrix.
Then the fragPosition is transformed to a clip space by applying view and projection transformations.

https://johannesugb.github.io/gpu-programming/setting-up-a-proper-vulkan-projection-matrix/

World space, logical coordinates (project-specific)
---------------------------------------------------
Right-handed coordinate system (RHS).

X goes right.
Y goes up.
Forward direction is -Z.

Vulkan Normalized Device Coordinates (NDC)
------------------------------------------
Right-handed coordinate system (RHS).

X = -1..1
Y = -1..1
Z = 0..1

X goes right.
Y goes down.
Forward direction is Z.

Everything else is clipped.

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

glm assumes right-handed coordinates.

glm does a handedness swap due to a historical reasons to act as a drop-in replacement for opengl functions glOrtho and GlFrustum.

The usual orthographic or perspective projection matrix generated with glm::ortho or glm::perspective (or glOrtho, glFrustum) inverts the z-axis leading to a handedness swap.

Since I don't want to follow the hidden rule of the handedness swap, I use my own functions to build view and projection matrices.

Front face
----------
During rasterization vulkan needs to know if we are looking at the front face of at the back face.

Vertex winding order in the framebuffer space is used to decide the face orientation.

Textures
--------
**obj file vt**

The origin at the lower-left corner, meaning the Y-axis (or 't' coordinate) increases from bottom to top

**stb_image**

The pixel data consists of *y scanlines of *x pixels, with each pixel consisting of N interleaved 8-bit components; the first pixel pointed to is top-left-most in the image.

**VkImage**

Has the same image layout as stb_image by default, but Y axis orientation is unclear.

**glsl**

The origin of texture coordinates for a sampler2D is at the bottom-left corner, with the Y-axis increasing upward.
