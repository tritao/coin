#version 410 core

layout(location = 0) out uint outPickId;

uniform int u_pickId;

void main()
{
  outPickId = uint(u_pickId);
}
