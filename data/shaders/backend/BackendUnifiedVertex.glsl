#version 410 core

const int COIN_MAX_LIGHTS = 8;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec2 a_texcoord;
layout(location = 4) in float a_lineDistance;

uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform vec4 u_color;
uniform float u_useVertexColor;
uniform float u_renderMode;
uniform int u_shadingModel;
uniform vec3 u_emissiveColor;
uniform vec3 u_ambientLight;
uniform vec3 u_materialAmbient;
uniform vec3 u_materialSpecular;
uniform float u_materialShininess;
uniform float u_twoSidedLighting;
uniform int u_lightCount;
uniform int u_lightType[COIN_MAX_LIGHTS];
uniform vec3 u_lightColor[COIN_MAX_LIGHTS];
uniform vec3 u_lightDirection[COIN_MAX_LIGHTS];
uniform vec3 u_lightPosition[COIN_MAX_LIGHTS];
uniform vec3 u_lightAttenuation[COIN_MAX_LIGHTS];
uniform vec2 u_lightSpotParams[COIN_MAX_LIGHTS];
uniform vec3 u_quadCenter;
uniform vec2 u_texSize;
uniform vec2 u_vpSize;
uniform vec2 u_pixelOrigin;

out vec3 v_litColor;
out vec4 v_color;
// Keep vertex alpha separate so the fragment stage can compose it with
// material opacity exactly once.
out float v_vertexAlpha;
out vec2 v_texcoord;
out float v_lineDistance;

vec3 computeGouraudColor(vec3 eyePos, vec3 eyeNormal, vec3 baseColor)
{
  vec3 N = normalize(eyeNormal);
  if (u_twoSidedLighting != 0 && dot(N, vec3(0.0, 0.0, 1.0)) < 0.0) {
    N = -N;
  }
  vec3 V = normalize(-eyePos);
  vec3 litColor = u_ambientLight * u_materialAmbient;

  for (int i = 0; i < COIN_MAX_LIGHTS; ++i) {
    if (i >= u_lightCount) break;

    vec3 L = u_lightDirection[i];
    float attenuation = 1.0;
    float spotFactor = 1.0;

    if (u_lightType[i] != 0) {
      vec3 lightVector = u_lightPosition[i] - eyePos;
      float distanceToLight = length(lightVector);
      if (distanceToLight <= 0.0001) continue;
      L = lightVector / distanceToLight;

      vec3 att = u_lightAttenuation[i];
      attenuation = 1.0 / max(att.z + att.y * distanceToLight +
                               att.x * distanceToLight * distanceToLight,
                               0.0001);

      if (u_lightType[i] == 2) {
        vec3 coneDir = normalize(u_lightDirection[i]);
        vec3 fromLight = normalize(eyePos - u_lightPosition[i]);
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
    float shininess = max(u_materialShininess * 128.0, 0.0);
    float specularFactor = shininess > 0.0
      ? pow(NdotH, shininess) : 0.0;
    vec3 specular = u_materialSpecular * specularFactor;
    vec3 diffuse = baseColor * NdotL;
    litColor += u_lightColor[i] * attenuation * spotFactor *
                (diffuse + specular);
  }

  return clamp(litColor + u_emissiveColor, 0.0, 1.0);
}

void main()
{
  v_color = (u_useVertexColor > 0.5) ? a_color : u_color;
  v_vertexAlpha = (u_useVertexColor > 0.5) ? a_color.a : 1.0;
  v_litColor = vec3(0.0);
  v_texcoord = a_texcoord;

  if (u_renderMode > 3.5) {
    // SoText2 is rasterized into a CPU glyph texture.  Place that texture at
    // the exact integer framebuffer origin recorded by the producer while
    // retaining the projected depth of the text origin.
    vec2 pixelPosition = u_pixelOrigin + a_texcoord * u_texSize;
    vec2 ndcPosition = 2.0 * pixelPosition / u_vpSize - 1.0;
    vec4 centerClip = u_proj * u_view * u_model * vec4(u_quadCenter, 1.0);
    gl_Position = vec4(ndcPosition * centerClip.w,
                       centerClip.z, centerClip.w);
    v_litColor = v_color.rgb;
    v_lineDistance = 0.0;
  }
  else if (u_renderMode > 1.5 && u_renderMode < 2.5) {
    vec4 centerClip = u_proj * u_view * u_model * vec4(u_quadCenter, 1.0);
    vec2 pixelOffset = (a_texcoord - vec2(0.5)) * u_texSize;
    vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;
    gl_Position = centerClip + vec4(ndcOffset * centerClip.w, 0.0, 0.0);
    v_lineDistance = 0.0;
  }
  else {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    vec4 eyePos = u_view * worldPos;
    // Normals transform with the inverse-transpose of the complete
    // model-view matrix. Multiplying the two upper-left 3x3 matrices fails
    // for non-uniform scale (and for transforms with shear).
    vec3 eyeNormal = mat3(transpose(inverse(u_view * u_model))) * a_normal;
    v_litColor = u_shadingModel == 0
      ? v_color.rgb
      : computeGouraudColor(eyePos.xyz, eyeNormal, v_color.rgb);
    gl_Position = u_proj * eyePos;
    v_lineDistance = a_lineDistance;
  }
}
