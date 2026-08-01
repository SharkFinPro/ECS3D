// Port of Smoke.comp + Smoke.vert + Smoke.frag.
//
// Compute (cs_main): per-particle integration, ping-pong SSBO (particlesIn -> particlesOut),
// a direct translation of Smoke.comp (TTL cycling, wind, spread force, respawn).
//
// Render (vs_main/fs_main): the original draws GL_POINTS with gl_PointSize and reads
// gl_PointCoord in the fragment. WebGPU has neither, so each particle is expanded into a
// camera-facing billboard quad (6 vertices, instanced): the vertex shader sizes the quad in
// pixels exactly like the original's gl_PointSize clamp, and the quad's [0,1] local coord
// stands in for gl_PointCoord. Lighting matches Smoke.frag (SmokePointLightAffect /
// SmokeSpotLightAffect), using the port's unified light array (direction.w > 0 => spot).

const TTL : f32 = 8.0;
const PI : f32 = 3.14159265359;
const TWO_PI : f32 = 6.28318530718;

struct Particle {
  positionTtl : vec4<f32>,
  velocityColor : vec4<f32>,
};

// ---------------------------------------------------------------------------
// Compute
// ---------------------------------------------------------------------------

struct Params {
  deltaTime : f32,
};

struct Smoke {
  systemPosition : vec3<f32>,
  spreadFactor : f32,
  maxSpreadDistance : f32,
  windStrength : f32,
};

@group(0) @binding(0) var<uniform> params : Params;
@group(0) @binding(1) var<storage, read> particlesIn : array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut : array<Particle>;
@group(0) @binding(3) var<uniform> smoke : Smoke;

fn rand(seed : f32) -> f32 {
  return fract(sin(seed * 91.3458) * 47453.5453);
}

fn randRange(seed : f32, minVal : f32, maxVal : f32) -> f32 {
  return minVal + (maxVal - minVal) * rand(seed);
}

fn applyWind(position : vec3<f32>, ttl : f32) -> vec3<f32> {
  let windDir = vec3<f32>(sin(ttl * 0.1), 0.0, cos(ttl * 0.3));
  let turbulence = sin(position.y * 0.5 + ttl) * 0.15 +
                   cos(position.x * 0.3 + ttl * 0.7) * 0.1;
  return windDir * smoke.windStrength + vec3<f32>(turbulence, 0.0, turbulence);
}

fn generateSpreadDirection(position : vec3<f32>, ttl : f32) -> vec3<f32> {
  var direction = normalize(position - vec3<f32>(0.0));
  if (length(direction) < 0.01) {
    direction = normalize(vec3<f32>(sin(ttl * PI * 7.0), cos(ttl * PI * 11.0), sin(ttl * PI * 13.0)));
  }
  return direction;
}

fn calcSpreadForce(position : vec3<f32>, ttl : f32) -> vec3<f32> {
  let normalizedTime = ttl / TTL;
  let spreadStrength = min(smoke.spreadFactor * log(1.0 + 20.0 * normalizedTime), 0.5);
  let spreadDirection = generateSpreadDirection(position, ttl);
  var spreadForce = spreadDirection * spreadStrength;

  let distFromOrigin = length(position);
  if (distFromOrigin > smoke.maxSpreadDistance) {
    spreadForce -= normalize(position) * 0.05 * (distFromOrigin - smoke.maxSpreadDistance);
  }
  return spreadForce;
}

fn regenerateInitialPosition(seed : f32) -> vec3<f32> {
  let r = sqrt(rand(seed)) * 0.25;
  let theta = rand(seed + 1.0) * TWO_PI;
  let x = r * cos(theta);
  let z = r * sin(theta);
  let scaleX = randRange(seed + 2.0, -4.0, 4.0);
  let scaleZ = randRange(seed + 3.0, -4.0, 4.0);
  return vec3<f32>(x * scaleX, 0.0, z * scaleZ) + smoke.systemPosition;
}

fn regenerateInitialVelocity(seed : f32) -> vec3<f32> {
  let vx = randRange(seed + 4.0, -0.25, 0.25);
  let vz = randRange(seed + 5.0, -0.25, 0.25);
  let vy = randRange(seed + 6.0, 0.75, 2.5);
  return vec3<f32>(vx, vy, vz);
}

@compute @workgroup_size(256)
fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {
  let index = gid.x;
  if (index >= arrayLength(&particlesOut)) {
    return;
  }

  let particle = particlesIn[index];
  var position = particle.positionTtl.xyz;
  var velocity = particle.velocityColor.xyz;
  var ttl = particle.positionTtl.w + params.deltaTime;

  // Preserve the particle's colour (velocityColor.w) across the write.
  particlesOut[index].velocityColor.w = particle.velocityColor.w;

  if (ttl < 0.0) {
    particlesOut[index].positionTtl = vec4<f32>(position, ttl);
    particlesOut[index].velocityColor = vec4<f32>(velocity, particle.velocityColor.w);
    return;
  }

  if (ttl > TTL) {
    particlesOut[index].positionTtl = vec4<f32>(position, TTL - ttl);
    particlesOut[index].velocityColor = vec4<f32>(velocity, particle.velocityColor.w);
    return;
  }

  if (ttl - params.deltaTime < 0.0) {
    let seed = (position.x + position.y + position.z) / 3.0;
    velocity = regenerateInitialVelocity(seed);
    position = regenerateInitialPosition(seed);
  }

  let wind = applyWind(position, ttl);
  let spreadForce = calcSpreadForce(position, ttl);

  velocity += (wind + spreadForce) * params.deltaTime;
  position += velocity * params.deltaTime;

  particlesOut[index].positionTtl = vec4<f32>(position, ttl);
  particlesOut[index].velocityColor = vec4<f32>(velocity, particle.velocityColor.w);
}

// ---------------------------------------------------------------------------
// Render (billboard)
// ---------------------------------------------------------------------------

struct Transform {
  view : mat4x4<f32>,
  proj : mat4x4<f32>,
  // x,y = viewport size in pixels (to size the billboard the way gl_PointSize did)
  viewport : vec2<f32>,
  pad : vec2<f32>,
};

struct PointLight {
  position : vec3<f32>,
  color : vec3<f32>,
  // x = ambient, y = diffuse, z = specular
  lparams : vec3<f32>,
  // xyz = spot direction, w = cone angle radians (w <= 0 -> point light)
  direction : vec4<f32>,
};

struct LightMeta {
  numLights : u32,
};

@group(0) @binding(0) var<uniform> transform : Transform;
@group(0) @binding(1) var<storage, read> renderParticles : array<Particle>;

@group(1) @binding(0) var<uniform> lightMeta : LightMeta;
@group(1) @binding(1) var<storage, read> lights : array<PointLight>;

struct VertexOut {
  @builtin(position) clipPosition : vec4<f32>,
  @location(0) fragPos : vec3<f32>,
  @location(1) fragColor : vec4<f32>,
  @location(2) pointCoord : vec2<f32>,
};

// Two-triangle quad corners in [0,1] (stands in for gl_PointCoord).
const CORNERS = array<vec2<f32>, 6>(
  vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
  vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vid : u32, @builtin(instance_index) iid : u32) -> VertexOut {
  var out : VertexOut;
  let particle = renderParticles[iid];

  // Dead particles (negative TTL) are pushed off-screen, matching Smoke.vert.
  if (particle.positionTtl.w < 0.0) {
    out.clipPosition = vec4<f32>(2.0, 2.0, 2.0, 1.0);
    out.fragColor = vec4<f32>(0.0);
    out.pointCoord = vec2<f32>(0.0);
    out.fragPos = vec3<f32>(0.0);
    return out;
  }

  let viewPos = transform.view * vec4<f32>(particle.positionTtl.xyz, 1.0);

  let basePointSize = 7.0;
  let distanceScale = 1.0 / -viewPos.z;
  let pointSize = clamp(basePointSize * distanceScale, 1.0, 20.0);

  var clip = transform.proj * viewPos;

  // Offset the clip-space position by the corner, sized in pixels like gl_PointSize.
  let corner = CORNERS[vid];
  let local = corner - vec2<f32>(0.5); // [-0.5, 0.5]
  let ndcOffset = (local * pointSize / transform.viewport) * 2.0;
  clip = vec4<f32>(clip.xy + ndcOffset * clip.w, clip.zw);

  out.clipPosition = clip;
  out.pointCoord = corner;
  out.fragColor = vec4<f32>(
    particle.velocityColor.w,
    particle.velocityColor.w,
    particle.velocityColor.w,
    sqrt((TTL - particle.positionTtl.w) / TTL),
  );
  out.fragPos = particle.positionTtl.xyz;
  return out;
}

fn noise2D(st : vec2<f32>) -> f32 {
  let i = floor(st);
  let f = fract(st);

  let a = fract(sin(dot(i, vec2<f32>(12.9898, 78.233))) * 43758.5453);
  let b = fract(sin(dot(i + vec2<f32>(1.0, 0.0), vec2<f32>(12.9898, 78.233))) * 43758.5453);
  let c = fract(sin(dot(i + vec2<f32>(0.0, 1.0), vec2<f32>(12.9898, 78.233))) * 43758.5453);
  let d = fract(sin(dot(i + vec2<f32>(1.0, 1.0), vec2<f32>(12.9898, 78.233))) * 43758.5453);

  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn isInSpotlight(light : PointLight, fragPos : vec3<f32>) -> bool {
  let lightToFrag = normalize(fragPos - light.position);
  return dot(lightToFrag, normalize(light.direction.xyz)) >= cos(light.direction.w);
}

fn smokePointLightAffect(light : PointLight, color : vec3<f32>, fragPos : vec3<f32>) -> vec3<f32> {
  let dist = length(light.position - fragPos);
  let attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
  return (light.lparams.x + light.lparams.y) * color * light.color * attenuation;
}

fn smokeSpotLightAffect(light : PointLight, color : vec3<f32>, fragPos : vec3<f32>) -> vec3<f32> {
  let dist = length(light.position - fragPos);
  let attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
  if (!isInSpotlight(light, fragPos)) {
    return light.lparams.x * color * light.color * attenuation;
  }
  return (light.lparams.x + light.lparams.y) * color * light.color * attenuation;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let coord = in.pointCoord - vec2<f32>(0.5);
  let r = length(coord);
  if (r > 0.5) {
    discard;
  }

  let noiseCoord = in.pointCoord * 2.0;
  let noise = noise2D(noiseCoord);
  let secondNoise = noise2D(noiseCoord * 1.5 + vec2<f32>(0.2, 0.7));
  let swirl = noise2D(vec2<f32>(r * 4.0, atan2(coord.y, coord.x) * 2.0));

  var finalMask = (0.7 + 0.3 * noise) * (0.8 + 0.2 * swirl);
  finalMask *= 0.9 + 0.1 * secondNoise;

  var result = vec3<f32>(0.0);
  for (var i = 0u; i < lightMeta.numLights; i++) {
    let light = lights[i];
    if (light.direction.w > 0.0) {
      result += smokeSpotLightAffect(light, in.fragColor.rgb, in.fragPos);
    } else {
      result += smokePointLightAffect(light, in.fragColor.rgb, in.fragPos);
    }
  }

  return vec4<f32>(result, finalMask * in.fragColor.a);
}
