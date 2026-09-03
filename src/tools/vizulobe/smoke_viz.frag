#version 330
uniform float uTime;
uniform vec2 uResolution;
uniform vec2 uAudio;
uniform sampler2D uWaveform;
out vec4 FragColor;
void main() {
	vec2 uv = gl_FragCoord.xy / uResolution;
	float w = texture(uWaveform, vec2(uv.x, 0.0)).r;
	vec3 col = vec3(0.0);
	col.r = 0.5 + 0.5 * sin(uv.x * 10.0 + uTime * 3.0);
	col.g = w * 2.0;
	col.b = uAudio.x * 2.0;
	FragColor = vec4(col, 1.0);
}
