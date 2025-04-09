project('VulkanSDLApp', 'cpp', 'c',
        version : '1.0.0',
        default_options : ['cpp_std=c++20', 'warning_level=3', 'werror=true'])

vertex_shader_src = 'shader.vertex.glsl'
fragment_shader_src = 'shader.fragment.glsl'

vertex_shader = custom_target(
        'vertex_shader',
        input : vertex_shader_src,
        output : 'shader.vertex.spv',
        command : ['glslc', '-fshader-stage=vertex', '@INPUT@', '-o', '@OUTPUT@'],
        build_by_default : true,
)

fragment_shader = custom_target(
        'fragment_shader',
        input : fragment_shader_src,
        output : 'shader.fragment.spv',
        command : ['glslc', '-fshader-stage=fragment', '@INPUT@', '-o', '@OUTPUT@'],
        build_by_default : true,
)

shaders_dep = declare_dependency(sources: [vertex_shader, fragment_shader])

dependencies = [
        dependency('sdl2'),
        dependency('vulkan'),
        dependency('glm'),
        shaders_dep,
]

app = executable(
        'VulkanSDLApp',
        [
                'main.cpp',
                'Pipeline.h',
                'VulkanContext.h',
                'VulkanFunctions.h',
                'ImageFunctions.h',
                'MeshFunctions.h',
                'ObjFile.h',
                'Material.h',
                'Model.h',
                'Camera.h',
                'CameraController.h',
                'OrbitCameraController.h',
                'FlyingCameraController.h',
                'MeshObject.h',
                'Swapchain.h',
                'RenderSurface.h',
                'RenderingConfig.h',
                '3rdparty/stb_image.cpp',
                '3rdparty/tiny_obj_loader.cpp',
                '3rdparty/tinyexr.cc',
                '3rdparty/miniz.c',
                '3rdparty/imgui.h',
                '3rdparty/imgui.cpp',
                '3rdparty/imgui_draw.cpp',
                '3rdparty/imgui_demo.cpp',
                '3rdparty/imgui_internal.h',
                '3rdparty/imgui_widgets.cpp',
                '3rdparty/imgui_tables.cpp',
                '3rdparty/imconfig.h',
                '3rdparty/imstb_rectpack.h',
                '3rdparty/imstb_truetype.h',
                '3rdparty/imstb_rectpack.h',
                '3rdparty/imgui_impl_sdl2.h',
                '3rdparty/imgui_impl_sdl2.cpp',
                '3rdparty/imgui_impl_vulkan.h',
                '3rdparty/imgui_impl_vulkan.cpp',
        ],
        install : true,
        dependencies: dependencies,
        include_directories: ['3rdparty'],
)

test('test', app)