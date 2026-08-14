// Shared helpers for the core OpenGL vertex/color pipeline.

vec4 coin_visual_color(vec4 vertexColor, vec4 uniformColor,
                       float useVertexColor)
{
  return useVertexColor > 0.5 ? vertexColor : uniformColor;
}
