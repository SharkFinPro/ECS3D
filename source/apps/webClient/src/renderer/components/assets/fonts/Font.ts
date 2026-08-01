// Port of source/components/assets/fonts/Font.{h,cpp} — glyph metrics plus the texture each glyph
// is sampled from, consumed by Renderer2D's `font` pipeline (Font.vert / Font.frag).
//
// DEVIATION: upstream rasterizes with FreeType. FreeType does not exist in the browser, so glyphs
// are rasterized with an OffscreenCanvas 2D context and packed into one atlas per pixel size
// (TextureGlyph.ts). Hinting and metrics are therefore not bit-identical to FreeType, but the
// glyph-quad model — bearing, advance, atlas UV rect, alpha-only sampling tinted by the fill
// colour — matches Primitives2D::Glyph exactly.
//
// Covers printable ASCII (32-126), which is every character the ported 2D test draws.

import { TextureGlyph } from "../textures/TextureGlyph";

const FONT_FAMILY = "vkeRoboto2D";
const FIRST_CHAR = 0x20; // ' '
const LAST_CHAR = 0x7e; // '~'
const ATLAS_COLUMNS = 16;

export interface GlyphMetrics {
  u0: number;
  v0: number;
  u1: number;
  v1: number;
  width: number;
  height: number;
  bearingX: number;
  bearingY: number;
  advance: number;
}

export interface FontAtlas {
  texture: TextureGlyph;
  glyphs: Map<string, GlyphMetrics>;
  maxGlyphHeight: number;
}

interface RawGlyph {
  ch: string;
  width: number;
  height: number;
  bearingX: number;
  bearingY: number;
  advance: number;
}

export class Font {
  private readonly atlases = new Map<number, FontAtlas>();
  private glyphSampler: GPUSampler | null = null;

  private constructor(private readonly fontFamily: string) {}

  static async load(url: string): Promise<Font> {
    const alreadyRegistered = Array.from(document.fonts).some((f) => f.family === FONT_FAMILY);
    if (!alreadyRegistered) {
      const face = new FontFace(FONT_FAMILY, `url(${url})`);
      await face.load();
      document.fonts.add(face);
    }
    return new Font(FONT_FAMILY);
  }

  // Builds (and caches) the atlas for one pixel size. Synchronous: the typeface is already loaded
  // by the time a test starts drawing text.
  getAtlas(device: GPUDevice, sizePx: number): FontAtlas {
    let atlas = this.atlases.get(sizePx);
    if (!atlas) {
      atlas = this.buildAtlas(device, sizePx);
      this.atlases.set(sizePx, atlas);
    }
    return atlas;
  }

  private buildAtlas(device: GPUDevice, sizePx: number): FontAtlas {
    const fontSpec = `${sizePx}px "${this.fontFamily}"`;

    const measureCanvas = new OffscreenCanvas(4, 4);
    const measureCtx = measureCanvas.getContext("2d")!;
    measureCtx.font = fontSpec;
    measureCtx.textBaseline = "alphabetic";
    measureCtx.textAlign = "left";

    const raws: RawGlyph[] = [];
    let maxAscent = 0;
    for (let code = FIRST_CHAR; code <= LAST_CHAR; code++) {
      const ch = String.fromCharCode(code);
      const m = measureCtx.measureText(ch);
      const left = m.actualBoundingBoxLeft || 0;
      const right = m.actualBoundingBoxRight || 0;
      const ascent = m.actualBoundingBoxAscent || 0;
      const descent = m.actualBoundingBoxDescent || 0;
      const width = Math.max(0, Math.ceil(left + right));
      const height = Math.max(0, Math.ceil(ascent + descent));
      raws.push({ ch, width, height, bearingX: left, bearingY: ascent, advance: m.width });
      if (ascent > maxAscent) maxAscent = ascent;
    }

    const cellW = Math.max(1, ...raws.map((r) => r.width)) + 2;
    const cellH = Math.max(1, ...raws.map((r) => r.height)) + 2;
    const rows = Math.ceil(raws.length / ATLAS_COLUMNS);
    const atlasWidth = ATLAS_COLUMNS * cellW;
    const atlasHeight = Math.max(1, rows * cellH);

    const drawCanvas = new OffscreenCanvas(atlasWidth, atlasHeight);
    const ctx = drawCanvas.getContext("2d", { willReadFrequently: true })!;
    ctx.font = fontSpec;
    ctx.textBaseline = "alphabetic";
    ctx.textAlign = "left";
    ctx.fillStyle = "#ffffff";
    ctx.clearRect(0, 0, atlasWidth, atlasHeight);

    const glyphs = new Map<string, GlyphMetrics>();
    raws.forEach((r, i) => {
      const col = i % ATLAS_COLUMNS;
      const row = Math.floor(i / ATLAS_COLUMNS);
      const cellX = col * cellW;
      const cellY = row * cellH;

      if (r.width > 0 && r.height > 0) {
        ctx.fillText(r.ch, cellX + r.bearingX, cellY + r.bearingY);
      }

      glyphs.set(r.ch, {
        u0: cellX / atlasWidth,
        v0: cellY / atlasHeight,
        u1: (cellX + r.width) / atlasWidth,
        v1: (cellY + r.height) / atlasHeight,
        width: r.width,
        height: r.height,
        bearingX: r.bearingX,
        bearingY: r.bearingY,
        advance: r.advance,
      });
    });

    const imageData = ctx.getImageData(0, 0, atlasWidth, atlasHeight);
    const alpha = new Uint8Array(atlasWidth * atlasHeight);
    for (let p = 0; p < alpha.length; p++) alpha[p] = imageData.data[p * 4 + 3];

    this.glyphSampler ??= TextureGlyph.createSampler(device);

    const texture = TextureGlyph.createAtlas(
      device,
      `vke.GlyphAtlas${sizePx}`,
      alpha,
      atlasWidth,
      atlasHeight,
      this.glyphSampler,
    );

    return { texture, glyphs, maxGlyphHeight: maxAscent };
  }
}
