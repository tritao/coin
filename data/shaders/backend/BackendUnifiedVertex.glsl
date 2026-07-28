#version 410 core
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
uniform vec3 u_quadCenter;
uniform vec2 u_texSize;
uniform vec2 u_vpSize;
uniform vec3 u_emissiveColor;
uniform vec3 u_ambientLight;
uniform int u_lightCount;
uniform vec3 u_lightColor[8];
uniform vec3 u_lightDirection[8];
uniform vec3 u_lightPosition[8];
uniform int u_lightType[8];
uniform vec3 u_lightAttenuation[8];
uniform vec2 u_lightSpotParams[8];
uniform vec3 u_materialAmbient;
uniform vec3 u_materialSpecular;
uniform float u_materialShininess;

out vec3 v_litColor;
out vec4 v_color;
out vec2 v_texcoord;
out float v_lineDistance;

vec3 computeLitColor(vec3 eyePos, vec3 eyeNormal, vec3 baseColor)
{
  vec3 N = normalize(eyeNormal);
  vec3 V = normalize(-eyePos);
  vec3 litColor = u_ambientLight * u_materialAmbient;
  float shininess = max(u_materialShininess * 128.0, 0.0);

  for (int i = 0; i < 8; ++i) {
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
    float specularFactor = shininess > 0.0 ? pow(NdotH, shininess) : 0.0;
    vec3 specular = u_materialSpecular * specularFactor;
    vec3 diffuse = baseColor * NdotL;
    litColor += u_lightColor[i] * attenuation * spotFactor *
                (diffuse + specular);
  }

  // Legacy fixed-function lighting clamps the lit vertex color before
  // Gouraud interpolation.
  return clamp(litColor + u_emissiveColor, 0.0, 1.0);
}

void main()
{
  v_color = (u_useVertexColor > 0.5) ? a_color : u_color;
  v_texcoord = a_texcoord;

  if (u_renderMode > 1.5 && u_renderMode < 2.5) {
    vec4 centerClip = u_proj * u_view * u_model * vec4(u_quadCenter, 1.0);
    vec2 pixelOffset = (a_texcoord - vec2(0.5)) * u_texSize;
    vec2 ndcOffset = 2.0 * pixelOffset / u_vpSize;
    gl_Position = centerClip + vec4(ndcOffset * centerClip.w, 0.0, 0.0);
    v_lineDistance = 0.0;
  }
  else {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    vec4 eyePos = u_view * worldPos;
    vec3 eyeNormal = mat3(u_view) * mat3(u_model) * a_normal;
    vec3 baseColor = (u_useVertexColor > 0.5) ? a_color.rgb : u_color.rgb;
    v_litColor = u_shadingModel == 0
      ? baseColor
      : computeLitColor(eyePos.xyz, eyeNormal, baseColor);
    gl_Position = u_proj * eyePos;
    v_lineDistance = a_lineDistance;
  }
}
