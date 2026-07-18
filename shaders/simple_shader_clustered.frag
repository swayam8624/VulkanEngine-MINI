#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;

layout(location = 0) out vec4 outColor;

struct LegacyPointLight {
  vec4 position;
  vec4 color;
};

struct SsboPointLight {
  vec4 positionRadius;
  vec4 colorIntensity;
};

struct ClusterHeader {
  uint dataOffset;
  uint storedCount;
  uint totalCandidateCount;
  uint flags;
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  vec4 ambientLightColor;
  LegacyPointLight pointLights[10];
  int numLights;
} ubo;

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer {
  SsboPointLight lights[];
} lightBuffer;

layout(set = 0, binding = 3) uniform ClusterRuntimeConfig {
  vec4 worldMin;
  vec4 worldMax;
  uvec4 gridSize;
  vec4 viewportNearFar;
  uint clusterCount;
  uint maxLightsPerCluster;
  uint lightIndexCapacity;
  uint flags;
} clusterConfig;

layout(std430, set = 0, binding = 4) readonly buffer ClusterHeaders {
  ClusterHeader headers[];
} clusterHeaders;

layout(std430, set = 0, binding = 5) readonly buffer ClusterLightIndices {
  uint indices[];
} clusterLightIndices;

vec3 evaluateLight(SsboPointLight light, vec3 normalWorld) {
  vec3 directionToLight = light.positionRadius.xyz - fragPosWorld;
  float distanceSquared = dot(directionToLight, directionToLight);
  float radius = max(light.positionRadius.w, 0.0001);
  float radiusFactor = max(1.0 - distanceSquared / (radius * radius), 0.0);
  float attenuation = (radiusFactor * radiusFactor) / max(distanceSquared, 0.0001);
  directionToLight = normalize(directionToLight);

  float cosAngIncidence = max(dot(normalWorld, directionToLight), 0);
  return light.colorIntensity.xyz * light.colorIntensity.w * attenuation * cosAngIncidence;
}

uint findClusterIndex() {
  if ((clusterConfig.flags & 1u) != 0u) {
    vec2 normalizedScreen = gl_FragCoord.xy / max(clusterConfig.viewportNearFar.xy, vec2(1.0));
    uvec2 xy = min(
        uvec2(floor(normalizedScreen * vec2(clusterConfig.gridSize.xy))),
        clusterConfig.gridSize.xy - uvec2(1));
    float viewZ = (ubo.view * vec4(fragPosWorld, 1.0)).z;
    float nearPlane = clusterConfig.viewportNearFar.z;
    float farPlane = clusterConfig.viewportNearFar.w;
    if (viewZ < nearPlane || viewZ > farPlane) return clusterConfig.clusterCount;
    float normalizedZ = log(viewZ / nearPlane) / log(farPlane / nearPlane);
    uint z = uint(clamp(
        floor(normalizedZ * float(clusterConfig.gridSize.z)),
        0.0,
        float(clusterConfig.gridSize.z - 1u)));
    return xy.x + xy.y * clusterConfig.gridSize.x +
           z * clusterConfig.gridSize.x * clusterConfig.gridSize.y;
  }
  vec3 extent = clusterConfig.worldMax.xyz - clusterConfig.worldMin.xyz;
  vec3 normalized = (fragPosWorld - clusterConfig.worldMin.xyz) / max(extent, vec3(0.0001));
  if (any(lessThan(normalized, vec3(0.0))) || any(greaterThanEqual(normalized, vec3(1.0)))) {
    return clusterConfig.clusterCount;
  }

  uvec3 grid = max(clusterConfig.gridSize.xyz, uvec3(1));
  uvec3 coord = min(uvec3(floor(normalized * vec3(grid))), grid - uvec3(1));
  return coord.x + coord.y * grid.x + coord.z * grid.x * grid.y;
}

void main() {
  vec3 diffuseLight = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w;
  vec3 surfaceNormal = normalize(fragNormalWorld);

  uint clusterId = findClusterIndex();
  if (clusterId < clusterConfig.clusterCount) {
    ClusterHeader header = clusterHeaders.headers[clusterId];
    if ((header.flags & 1u) != 0u) {
      for (int i = 0; i < ubo.numLights; ++i) {
        diffuseLight += evaluateLight(lightBuffer.lights[i], surfaceNormal);
      }
    } else {
      uint count = min(header.storedCount, clusterConfig.maxLightsPerCluster);
      for (uint i = 0; i < count; ++i) {
        uint indexOffset = header.dataOffset + i;
        if (indexOffset >= clusterConfig.lightIndexCapacity) {
          break;
        }
        uint lightIndex = clusterLightIndices.indices[indexOffset];
        if (lightIndex < uint(ubo.numLights)) {
          diffuseLight += evaluateLight(lightBuffer.lights[lightIndex], surfaceNormal);
        }
      }
    }
  } else {
    for (int i = 0; i < ubo.numLights; ++i) {
      diffuseLight += evaluateLight(lightBuffer.lights[i], surfaceNormal);
    }
  }

  outColor = vec4(diffuseLight * fragColor, 1.0);
}
