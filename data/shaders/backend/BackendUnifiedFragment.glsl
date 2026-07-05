#version 410 core
uniform float u_renderMode;
uniform vec3 u_emissiveColor;
uniform sampler2D u_texture;
uniform vec4 u_texModColor;
uniform float u_stipplePeriod;
uniform float u_metalness;
uniform float u_roughness;
in vec3 v_eyePos;
in vec3 v_eyeNormal;
in vec4 v_color;
in vec2 v_texcoord;
in vec2 v_winPos;
in float v_lineDistance;
out vec4 fragColor;
void main() {
  if (u_renderMode > 0.5 && u_renderMode < 1.8) {
    if (u_renderMode > 1.2) {
      vec2 pc = gl_PointCoord - vec2(0.5);
      if (dot(pc, pc) > 0.25) discard;
    }
    if (u_stipplePeriod > 0.0) {
      if (mod(v_lineDistance, u_stipplePeriod) > u_stipplePeriod * 0.5) discard;
    }
    fragColor = v_color;
    return;
  }
  if (u_renderMode > 1.8 && u_renderMode < 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    if (c.a < 0.3) discard;
    fragColor = c * u_texModColor;
    return;
  }
  if (u_renderMode > 2.5) {
    vec4 c = texture(u_texture, v_texcoord);
    if (c.a < 0.3) discard;
    fragColor = c * u_texModColor;
    return;
  }
  vec3 N = normalize(v_eyeNormal);
  if (dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) N = -N;
  vec3 L = vec3(0.0, 0.0, 1.0);
  float NdotL = max(dot(N, L), 0.0);
  vec3 V = normalize(-v_eyePos);
  vec3 H = normalize(L + V);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);

  vec3 baseColor = v_color.rgb;
  float roughSq = u_roughness * u_roughness;

  float d = NdotH * NdotH * (roughSq - 1.0) + 1.0;
  float D = roughSq / (3.14159 * d * d);

  vec3 F0 = mix(vec3(0.04), baseColor, u_metalness);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

  vec3 specular = D * F * 0.25;
  vec3 diffuse = (1.0 - u_metalness) * NdotL * baseColor;
  vec3 ambient = 0.2 * baseColor;

  fragColor = vec4(ambient + diffuse + specular + u_emissiveColor, v_color.a);
}
