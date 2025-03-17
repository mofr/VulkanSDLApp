project('VulkanSDLApp', 'cpp',
        version : '1.0.0',
        default_options : ['warning_level=3', 'cpp_std=c++20'])

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
                'VulkanFunctions.h',
                'ObjFile.h',
                'Model.h',
                'CameraController.h',
                'CameraFunctions.h',
                'MeshObject.h',
                '3rdparty/stb_image.cpp',
                '3rdparty/tiny_obj_loader.cpp',
        ],
        install : true,
        dependencies: dependencies,
        include_directories: ['3rdparty'],
)

test('test', app)