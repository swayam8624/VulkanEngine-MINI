#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 0) out vec4 outColor;

struct LegacyPointLight { vec4 position; vec4 color; };
struct PointLight { vec4 positionRadius; vec4 colorIntensity; };
struct ClusterHeader { uint dataOffset; uint storedCount; uint totalCandidateCount; uint flags; };
struct ClusterNode { vec4 boundsMin; vec4 boundsMax; };

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection; mat4 view; mat4 invView; vec4 ambientLightColor;
  LegacyPointLight pointLights[10]; int numLights;
} ubo;
layout(std430, set = 0, binding = 1) readonly buffer Lights { PointLight lights[]; } lightBuffer;
layout(set = 0, binding = 3) uniform ClusterConfig {
  vec4 worldMin; vec4 worldMax; uvec4 gridSize;
  vec4 viewportNearFar;
  uint clusterCount; uint stride; uint lightIndexCapacity; uint flags;
} config;
layout(std430, set = 0, binding = 4) readonly buffer Headers { ClusterHeader headers[]; } clusterHeaders;
layout(std430, set = 0, binding = 5) readonly buffer LightData { uint data[]; } clusterData;
layout(std430, set = 0, binding = 6) readonly buffer Nodes { ClusterNode nodes[]; } clusterNodes;

const uint ACTIVE = 2u;
const uint BITSET = 4u;

vec3 evaluateLight(uint id, vec3 normalWorld) {
  PointLight light = lightBuffer.lights[id];
  vec3 toLight = light.positionRadius.xyz - fragPosWorld;
  float d2 = dot(toLight, toLight);
  float radius = max(light.positionRadius.w, 0.0001);
  float radiusFactor = max(1.0 - d2 / (radius * radius), 0.0);
  float attenuation = radiusFactor * radiusFactor / max(d2, 0.0001);
  float lambert = max(dot(normalWorld, normalize(toLight)), 0.0);
  return light.colorIntensity.rgb * light.colorIntensity.w * attenuation * lambert;
}

void main() {
  vec3 diffuse = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
  vec3 normalWorld = normalize(fragNormalWorld);
  vec3 extent = max(config.worldMax.xyz - config.worldMin.xyz, vec3(0.0001));
  vec3 p = (fragPosWorld - config.worldMin.xyz) / extent;
  bool inside = all(greaterThanEqual(p, vec3(0.0))) && all(lessThan(p, vec3(1.0)));
  bool found = false;
  if (inside) {
    uvec2 tile = min(uvec2(floor(p.xy * vec2(config.gridSize.xy))), config.gridSize.xy - uvec2(1));
    uint base = (tile.y * config.gridSize.x + tile.x) * config.gridSize.z;
    for (uint leaf = 0; leaf < config.gridSize.z; ++leaf) {
      uint id = base + leaf;
      ClusterHeader header = clusterHeaders.headers[id];
      if ((header.flags & ACTIVE) == 0u) continue;
      ClusterNode node = clusterNodes.nodes[id];
      if (fragPosWorld.z < node.boundsMin.z || fragPosWorld.z >= node.boundsMax.z) continue;
      found = true;
      if ((header.flags & BITSET) != 0u) {
        uint wordCount = (uint(ubo.numLights) + 31u) / 32u;
        for (uint wordId = 0; wordId < wordCount; ++wordId) {
          uint word = clusterData.data[header.dataOffset + wordId];
          while (word != 0u) {
            uint bit = uint(findLSB(word));
            uint lightId = wordId * 32u + bit;
            if (lightId < uint(ubo.numLights)) diffuse += evaluateLight(lightId, normalWorld);
            word &= word - 1u;
          }
        }
      } else {
        for (uint i = 0; i < header.storedCount; ++i) {
          uint lightId = clusterData.data[header.dataOffset + i];
          if (lightId < uint(ubo.numLights)) diffuse += evaluateLight(lightId, normalWorld);
        }
      }
      break;
    }
  }
  if (!found) {
    for (uint i = 0; i < uint(ubo.numLights); ++i) diffuse += evaluateLight(i, normalWorld);
  }
  outColor = vec4(diffuse * fragColor, 1.0);
}
