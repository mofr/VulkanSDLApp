#version 450

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inputRadianceAttachment;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    int tonemapOperator; // 0 = No tone mapping, 1 = Reinhard, 2 = Uncharted2, 3 = ACES, 4 = Hejl
    float exposure;
    float reinhardWhitePoint;
} pc;

vec3 TonemapReinhard(vec3 color, float whitePoint)
{
    return color / (color + vec3(whitePoint));
}

vec3 TonemapFilmicUncharted2(vec3 color)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float W = 11.2; // white point for normalization

    vec3 curr = ((color * (A * color + C * B) + D * E) / 
                (color * (A * color + B) + D * F)) - E / F;

    vec3 whiteScale = ((vec3(W) * (A * vec3(W) + C * B) + D * E) / 
                      (vec3(W) * (A * vec3(W) + B) + D * F)) - E / F;

    return curr / whiteScale;
}

vec3 TonemapACES(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 TonemapHejl(vec3 color)
{
    color = max(vec3(0.0), color - 0.004);
    return (color * (6.2 * color + 0.5)) / 
           (color * (6.2 * color + 1.7) + 0.06);
}

void main() {
    vec3 radiance = subpassLoad(inputRadianceAttachment).rgb;
    radiance *= pc.exposure;

    vec3 toneMapped;

    switch (pc.tonemapOperator) {
        case 1:
            toneMapped = TonemapReinhard(radiance, pc.reinhardWhitePoint);
            break;
        case 2:
            toneMapped = TonemapFilmicUncharted2(radiance);
            break;
        case 3:
            toneMapped = TonemapACES(radiance);
            break;
        case 4:
            toneMapped = TonemapHejl(radiance);
            break;
        case 0:
        default:
            toneMapped = radiance; // No tone mapping
    }

    outColor = vec4(toneMapped, 1.0);
}
