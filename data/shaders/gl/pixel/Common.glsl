/*
 * Pixel-raster coordinate and source-sampling helpers.
 *
 * Pixel producers provide final RGBA. The destination raster footprint and
 * source texture size are independent, so scaled images map destination
 * pixels back to source texels here. Inherited material and scene-texture
 * state must not modulate the sampled pixel.
 */

vec2 coin_pixel_raster_coordinate(vec2 fragCoord,
                                  vec2 viewportOrigin,
                                  vec2 pixelOrigin)
{
  return floor(fragCoord - viewportOrigin - pixelOrigin);
}

bool coin_pixel_in_raster(vec2 rasterPixel, vec2 rasterSize)
{
  return rasterPixel.x >= 0.0 && rasterPixel.y >= 0.0 &&
         rasterPixel.x < rasterSize.x && rasterPixel.y < rasterSize.y;
}

ivec2 coin_pixel_source_coordinate(vec2 rasterPixel,
                                   vec2 sourceSize,
                                   vec2 rasterSize,
                                   ivec2 textureSizePixels)
{
  ivec2 sourcePixel = ivec2(floor(rasterPixel * sourceSize /
                                  max(rasterSize, vec2(1.0))));
  return clamp(sourcePixel, ivec2(0), textureSizePixels - ivec2(1));
}

vec4 coin_pixel_fetch(sampler2D textureSampler, ivec2 pixel)
{
  return texelFetch(textureSampler, pixel, 0);
}
