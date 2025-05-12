project('VulkanSDLApp', 'cpp', 'c',
        version : '1.0.0',
        default_options : ['cpp_std=c++20', 'warning_level=3', 'werror=true'])

# Define your shaders with their types
shader_list = [
    'shader.vertex.glsl',
    'shader.fragment.glsl',
    'CubemapBackground.vertex.glsl',
    'CubemapBackground.fragment.glsl',
    'FullscreenTriangle.vertex.glsl',
    'Tonemap.fragment.glsl',
]

# Empty array to collect compiled shader targets
compiled_shaders = []

# Process each shader in the list
# Mapping of filename patterns to shader stages
shader_stages = {
    '.vertex.glsl': 'vertex',
    '.fragment.glsl': 'fragment',
    '.compute.glsl': 'compute',
    '.geometry.glsl': 'geometry',
    '.tess_control.glsl': 'tesscontrol',
    '.tess_eval.glsl': 'tesseval',
}
foreach shader_file : shader_list
    # Determine shader type from filename
    shader_stage = ''
    foreach suffix, stage : shader_stages
        if shader_file.endswith(suffix)
            shader_stage = stage
            break
        endif
    endforeach
    
    if shader_stage == ''
        error('Could not determine shader stage for: ' + shader_file)
    endif
    output_file = shader_file.replace('.glsl', '.spv')
    compiled_shader = custom_target(
        shader_file,
        input : shader_file,
        output : output_file,
        command : ['glslc', '-fshader-stage=' + shader_stage, '@INPUT@', '-o', '@OUTPUT@'],
        build_by_default : true,
    )
    compiled_shaders += compiled_shader
endforeach

shaders_dep = declare_dependency(sources: compiled_shaders)

ProcessEnvmap  = executable(
        'ProcessEnvmap',
        [
                'ProcessEnvmap.cpp',
                'CubemapFunctions.h',
                '3rdparty/CLI11.hpp',
                '3rdparty/tinyexr.h',
                '3rdparty/tinyexr.cc',
                '3rdparty/miniz.c',
                '3rdparty/stb_image.cpp',
                ],
        include_directories: ['3rdparty'],
        dependencies: [
                dependency('ktx'),
                dependency('glm'),
        ],
)

ProcessAssets  = executable(
        'ProcessAssets',
        [
                'ProcessAssets.cpp',
                'CubemapFunctions.h',
                'SunExtraction.h',
                '3rdparty/CLI11.hpp',
                '3rdparty/tinyexr.h',
                '3rdparty/tinyexr.cc',
                '3rdparty/miniz.c',
                '3rdparty/stb_image.cpp',
                ],
        include_directories: ['3rdparty'],
        dependencies: [
                dependency('ktx'),
                dependency('glm'),
        ],
)

app = executable(
        'VulkanSDLApp',
        [
                'VulkanSDLApp.cpp',
                'Pipeline.h',
                'CubemapBackgroundPipeline.h',
                'VulkanContext.h',
                'VulkanFunctions.h',
                'ImageFunctions.h',
                'MeshFunctions.h',
                'ObjFile.h',
                'Material.h',
                'Model.h',
                'Environment.h',
                'Camera.h',
                'CameraController.h',
                'OrbitCameraController.h',
                'FlyingCameraController.h',
                'FileFunctions.h',
                'MeshObject.h',
                'Swapchain.h',
                'RenderSurface.h',
                'RenderingConfig.h',
                'UniformBuffer.h',
                'ColorTemperature.h',
                'Tonemapper.h',
                '3rdparty/stb_image.cpp',
                '3rdparty/tiny_obj_loader.cpp',
                '3rdparty/tinyexr.h',
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
        dependencies: [
                dependency('sdl2'),
                dependency('vulkan'),
                dependency('glm'),
                dependency('ktx'),
                shaders_dep,
        ],
        include_directories: ['3rdparty'],
)
