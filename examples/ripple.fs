#version 330

// Built-in TurtleFX inputs available to every custom shader:
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;   // Rendered TurtleFX scene (after Retro / 2D / 3D + Post-FX)
uniform vec2 uResolution;     // Screen size in pixels
uniform float uTime;          // Live time in seconds
uniform vec4 uParams;         // (p0/amount, p1/speed, p2/scale, p3/mix) from Logo!

void main() {
    float amount = (uParams.x > 0.0) ? uParams.x : 0.012;
    float speed  = (uParams.y > 0.0) ? uParams.y : 3.0;
    float scale  = (uParams.z > 0.0) ? uParams.z : 14.0;

    vec2 uv = fragTexCoord;
    vec2 center = uv - 0.5;
    float dist = length(center);

    // Radial ripple wave distortion
    float wave = sin(dist * scale - uTime * speed) * amount;
    vec2 dir = (dist > 0.001) ? (center / dist) : vec2(0.0);
    vec2 warped = uv + dir * wave;

    // Subtle RGB chromatic split along the wave
    float shift = abs(wave) * 0.9;
    float r = texture(texture0, warped + dir * shift).r;
    float g = texture(texture0, warped).g;
    float b = texture(texture0, warped - dir * shift).b;

    finalColor = vec4(r, g, b, 1.0);
}
