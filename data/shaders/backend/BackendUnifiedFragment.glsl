#version 410 core

const int COIN_MAX_LIGHTS = 8;

uniform float u_renderMode;
uniform vec3 u_emissiveColor;
uniform vec3 u_ambientLight;
uniform int u_lightCount;
uniform int u_lightType[COIN_MAX_LIGHTS];
uniform vec3 u_lightColor[COIN_MAX_LIGHTS];
uniform vec3 u_lightDirection[COIN_MAX_LIGHTS];
uniform vec3 u_lightPosition[COIN_MAX_LIGHTS];
uniform vec3 u_lightAttenuation[COIN_MAX_LIGHTS];
uniform vec2 u_lightSpotParams[COIN_MAX_LIGHTS];
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

void main()
{
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
    // World-space textured annotations (e.g. SoDatumLabel) use normal
    // blending, so preserve their antialiased glyph edge pixels. Billboard
    // text above retains the legacy alpha-test threshold.
    if (c.a <= 0.0) discard;
    fragColor = c * u_texModColor;
    return;
  }

  vec3 N = normalize(v_eyeNormal);
  if (dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) N = -N;
  vec3 V = normalize(-v_eyePos);

  vec3 baseColor = v_color.rgb;
  float roughSq = max(u_roughness * u_roughness, 0.001);
  vec3 F0 = mix(vec3(0.04), baseColor, u_metalness);
  vec3 litColor = u_ambientLight * baseColor;

  for (int i = 0; i < COIN_MAX_LIGHTS; ++i) {
    if (i >= u_lightCount) break;

    vec3 L = u_lightDirection[i];
    float attenuation = 1.0;
    float spotFactor = 1.0;

    if (u_lightType[i] != 0) {
      vec3 lightVector = u_lightPosition[i] - v_eyePos;
      float distanceToLight = length(lightVector);
      if (distanceToLight <= 0.0001) continue;
      L = lightVector / distanceToLight;

      vec3 att = u_lightAttenuation[i];
      attenuation = 1.0 / max(att.z + att.y * distanceToLight +
                               att.x * distanceToLight * distanceToLight,
                               0.0001);

      if (u_lightType[i] == 2) {
        vec3 coneDir = normalize(u_lightDirection[i]);
        vec3 fromLight = normalize(v_eyePos - u_lightPosition[i]);
        float spotCos = dot(coneDir, fromLight);
        if (spotCos < u_lightSpotParams[i].x) continue;
        spotFactor = pow(max(spotCos, 0.0), u_lightSpotParams[i].y);
      }
    }

    vec3 Ln = normalize(L);
    float NdotL = max(dot(N, Ln), 0.0);
    if (NdotL <= 0.0) continue;

    vec3 H = normalize(Ln + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float d = NdotH * NdotH * (roughSq - 1.0) + 1.0;
    float D = roughSq / (3.14159 * d * d);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    vec3 specular = D * F * 0.25 * NdotL;
    vec3 diffuse = (1.0 - u_metalness) * baseColor * NdotL;
    litColor += u_lightColor[i] * attenuation * spotFactor *
                (diffuse + specular);
  }

  fragColor = vec4(litColor + u_emissiveColor, v_color.a);
}
