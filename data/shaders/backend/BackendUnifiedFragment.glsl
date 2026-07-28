#version 410 core

uniform float u_renderMode;
uniform sampler2D u_texture;
uniform vec4 u_texModColor;
uniform vec2 u_pixelTextOrigin;
uniform float u_stipplePeriod;

in vec3 v_litColor;
in vec4 v_color;
in vec2 v_texcoord;
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

  if (u_renderMode > 3.5) {
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy - u_pixelTextOrigin));
    ivec2 textureSizePixels = textureSize(u_texture, 0);
    if (pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= textureSizePixels.x || pixel.y >= textureSizePixels.y) {
      discard;
    }
    vec4 c = texelFetch(u_texture, pixel, 0);
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

  fragColor = vec4(v_litColor, v_color.a);
}
