#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 0) out vec4 outColor;

struct LegacyPointLight { vec4 position; vec4 color; };
struct PointLight { vec4 positionRadius; vec4 colorIntensity; };

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor;
  LegacyPointLight pointLights[10];
  int numLights;
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
  PointLight lights[];
} lightBuffer;

vec3 evaluateDiffuse(PointLight light, vec3 normalWorld) {
  vec3 directionToLight = light.positionRadius.xyz - fragPosWorld;
  float distanceSquared = dot(directionToLight, directionToLight);
  float radius = max(light.positionRadius.w, 0.0001);
  float radiusFactor = max(1.0 - distanceSquared / (radius * radius), 0.0);
  float attenuation = radiusFactor * radiusFactor / max(distanceSquared, 0.0001);
  float lambert = max(dot(normalWorld, normalize(directionToLight)), 0.0);
  return light.colorIntensity.rgb * light.colorIntensity.w * attenuation * lambert;
}

void main() {
  vec3 diffuse = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
  vec3 normalWorld = normalize(fragNormalWorld);
  for (uint lightId = 0; lightId < uint(ubo.numLights); ++lightId) {
    diffuse += evaluateDiffuse(lightBuffer.lights[lightId], normalWorld);
  }
  outColor = vec4(diffuse * fragColor, 1.0);
}
