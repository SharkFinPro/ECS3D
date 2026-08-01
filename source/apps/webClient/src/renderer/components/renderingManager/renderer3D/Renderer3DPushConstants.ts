// Port of source/components/renderingManager/renderer3D/Renderer3DPushConstants.h — the
// per-pipeline parameter blocks, with upstream's default values.
//
// WebGPU has no push constants, so each block is written into a small uniform buffer owned by its
// pipeline (Pipeline.ts). That means the byte layout matters here where upstream could rely on the
// C++ struct: every `pack()` below produces exactly the `Params` struct its .wgsl declares, padded
// to a multiple of 16 bytes.
//
// Not ported: GridPushConstant's viewProj/viewPosition are supplied through the grid pipeline's
// own uniform (see PipelineConfig.ts), and the ray-tracing blocks have no WebGPU counterpart.

export interface BumpyCurtainPushConstant {
  amplitude: number;
  period: number;
  shininess: number;
  noiseAmplitude: number;
  noiseFrequency: number;
}

export interface CrossesPushConstant {
  quantize: number;
  size: number;
  shininess: number;
  blueDepth: number;
  redDepth: number;
  level: number;
  useChromaDepth: boolean;
}

export interface CubeMapPushConstant {
  mix: number;
  refractionIndex: number;
  whiteMix: number;
  noiseAmplitude: number;
  noiseFrequency: number;
}

export interface CurtainPushConstant {
  amplitude: number;
  period: number;
  shininess: number;
}

export interface EllipticalDotsPushConstant {
  shininess: number;
  sDiameter: number;
  tDiameter: number;
  blendFactor: number;
}

export interface MagnifyWhirlMosaicPushConstant {
  lensS: number;
  lensT: number;
  lensRadius: number;
  magnification: number;
  whirl: number;
  mosaic: number;
}

export interface NoisyEllipticalDotsPushConstant {
  shininess: number;
  sDiameter: number;
  tDiameter: number;
  blendFactor: number;
  noiseAmplitude: number;
  noiseFrequency: number;
}

export interface SnakePushConstant {
  wiggle: number;
}

// Upstream's member initializers, one factory per block.
export const defaultPushConstants = {
  bumpyCurtain: (): BumpyCurtainPushConstant => ({
    amplitude: 0.1,
    period: 1.0,
    shininess: 10.0,
    noiseAmplitude: 0.5,
    noiseFrequency: 1.0,
  }),
  crosses: (): CrossesPushConstant => ({
    quantize: 50.0,
    size: 0.01,
    shininess: 10.0,
    blueDepth: 4.4,
    redDepth: 1.0,
    level: 1,
    useChromaDepth: false,
  }),
  cubeMap: (): CubeMapPushConstant => ({
    mix: 0.0,
    refractionIndex: 1.4,
    whiteMix: 0.2,
    noiseAmplitude: 0.0,
    noiseFrequency: 0.1,
  }),
  curtain: (): CurtainPushConstant => ({
    amplitude: 0.1,
    period: 1.0,
    shininess: 10.0,
  }),
  ellipticalDots: (): EllipticalDotsPushConstant => ({
    shininess: 10.0,
    sDiameter: 0.025,
    tDiameter: 0.025,
    blendFactor: 0.0,
  }),
  magnifyWhirlMosaic: (): MagnifyWhirlMosaicPushConstant => ({
    lensS: 0.5,
    lensT: 0.5,
    lensRadius: 0.25,
    magnification: 1.0,
    whirl: 0.0,
    mosaic: 0.001,
  }),
  noisyEllipticalDots: (): NoisyEllipticalDotsPushConstant => ({
    shininess: 10.0,
    sDiameter: 0.025,
    tDiameter: 0.025,
    blendFactor: 0.0,
    noiseAmplitude: 0.5,
    noiseFrequency: 1.0,
  }),
  snake: (): SnakePushConstant => ({ wiggle: 0.0 }),
};

// Byte size of each block once padded — what PipelineConfig declares as its pushConstantRange.
export const pushConstantSizes = {
  bumpyCurtain: 32,
  cubeMap: 32,
  curtain: 16,
  ellipticalDots: 16,
  magnifyWhirlMosaic: 32,
  noisyEllipticalDots: 32,
};

export const packPushConstants = {
  bumpyCurtain: (p: BumpyCurtainPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([p.amplitude, p.period, p.shininess, p.noiseAmplitude, p.noiseFrequency, 0, 0, 0]),

  cubeMap: (p: CubeMapPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([p.mix, p.refractionIndex, p.whiteMix, p.noiseAmplitude, p.noiseFrequency, 0, 0, 0]),

  curtain: (p: CurtainPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([p.amplitude, p.period, p.shininess, 0]),

  ellipticalDots: (p: EllipticalDotsPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([p.shininess, p.sDiameter, p.tDiameter, p.blendFactor]),

  magnifyWhirlMosaic: (p: MagnifyWhirlMosaicPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([p.lensS, p.lensT, p.lensRadius, p.magnification, p.whirl, p.mosaic, 0, 0]),

  noisyEllipticalDots: (p: NoisyEllipticalDotsPushConstant): Float32Array<ArrayBuffer> =>
    new Float32Array([
      p.shininess, p.sDiameter, p.tDiameter, p.blendFactor, p.noiseAmplitude, p.noiseFrequency, 0, 0,
    ]),
};
