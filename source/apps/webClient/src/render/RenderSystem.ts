// Port of source/libs/render/RenderSystem.{h,cpp}.
//
// Walks the replicated scene each frame and submits models, lights and collider gizmos to the
// renderer. Editor-only behaviour is not ported: there is no highlightUUID parameter (the outline is
// the editor's selection feedback) and no mouse-pick flag, so nothing here submits a PickFlag.
//
// setReflectivity has no counterpart either - the WebGPU port drops it deliberately, since upstream
// it only fed the ray tracer.

import type { Vec3 } from "../net/Protocol";
import { ComponentType } from "../data/objects/components/Component";
import { Camera } from "../data/objects/components/Camera";
import { LightRenderer } from "../data/objects/components/LightRenderer";
import { ModelRenderer } from "../data/objects/components/ModelRenderer";
import { Transform } from "../data/objects/components/Transform";
import { BoxCollider } from "../data/objects/components/collisions/BoxCollider";
import { SphereCollider } from "../data/objects/components/collisions/SphereCollider";
import type { ObjectManager } from "../data/objects/ObjectManager";
import type { GpuAssetCache } from "./GpuAssetCache";
import type { WebGPUEngine } from "../renderer/WebGPUEngine";
import { PipelineType } from "../renderer/components/pipelines/implementations/common/PipelineTypes";
import type { PointLight } from "../renderer/components/lighting/lights/PointLight";
import type { SpotLight } from "../renderer/components/lighting/lights/SpotLight";
import { mat4LookAt } from "../renderer/utilities/Math";

interface CachedLights {
  pointLight: PointLight;
  spotLight: SpotLight;
}

export class RenderSystem {
  private readonly lights = new Map<string, CachedLights>();

  variableUpdate(objectManager: ObjectManager, assetCache: GpuAssetCache): void {
    const renderer = assetCache.getRenderer();
    const lightingManager = renderer.getLightingManager();
    const renderer3D = renderer.getRenderingManager().getRenderer3D();

    for (const object of objectManager.getAllObjects()) {
      const transform = object.getComponent<Transform>(ComponentType.transform);

      if (!transform) {
        continue;
      }

      const uuid = object.getUUID();

      const modelRenderer = object.getComponent<ModelRenderer>(ComponentType.modelRenderer);
      if (modelRenderer && modelRenderer.getShouldRender() && modelRenderer.canRender()) {
        const renderObject = assetCache.getRenderObject(
          uuid,
          modelRenderer.getModelUUID(),
          modelRenderer.getTextureUUID(),
          modelRenderer.getSpecularMapUUID(),
        );

        // Null while the mesh/textures are still loading - the object appears once they land.
        if (renderObject) {
          renderObject.setPosition(transform.getPosition());
          renderObject.setScale(transform.getScale());
          renderObject.setOrientationEuler(transform.getRotation());

          renderer3D.renderObject(
            renderObject,
            modelRenderer.getUseStandardPipeline()
              ? PipelineType.object
              : PipelineType.ellipticalDots,
          );
        }
      }

      const lightRenderer = object.getComponent<LightRenderer>(ComponentType.lightRenderer);
      if (lightRenderer) {
        let light = this.lights.get(uuid);

        if (!light) {
          light = {
            pointLight: lightingManager.createPointLight(
              [0, 0, 0],
              lightRenderer.getColor(),
              lightRenderer.getAmbient(),
              lightRenderer.getDiffuse(),
              lightRenderer.getSpecular(),
            ),
            spotLight: lightingManager.createSpotLight(
              [0, 0, 0],
              lightRenderer.getColor(),
              lightRenderer.getAmbient(),
              lightRenderer.getDiffuse(),
              lightRenderer.getSpecular(),
            ),
          };
          this.lights.set(uuid, light);
        }

        // Push the data values into the engine light each frame (data is the source of truth), then
        // position it from the transform and submit the active one.
        if (lightRenderer.isSpotLight()) {
          light.spotLight.setColor(lightRenderer.getColor());
          light.spotLight.setAmbient(lightRenderer.getAmbient());
          light.spotLight.setDiffuse(lightRenderer.getDiffuse());
          light.spotLight.setSpecular(lightRenderer.getSpecular());
          light.spotLight.setDirection(lightRenderer.getDirection());
          light.spotLight.setConeAngle(lightRenderer.getConeAngle());
          light.spotLight.setPosition(transform.getPosition());

          lightingManager.renderLight(light.spotLight);
        } else {
          light.pointLight.setColor(lightRenderer.getColor());
          light.pointLight.setAmbient(lightRenderer.getAmbient());
          light.pointLight.setDiffuse(lightRenderer.getDiffuse());
          light.pointLight.setSpecular(lightRenderer.getSpecular());
          light.pointLight.setPosition(transform.getPosition());

          lightingManager.renderLight(light.pointLight);
        }
      }

      // Collider debug gizmo: the collider's shape offset by its local transform, drawn with the
      // highlight pipeline when its render flag is on.
      const collider = object.getComponent(ComponentType.collider);

      if (collider instanceof BoxCollider && collider.getRenderCollider()) {
        const gizmo = assetCache.getColliderGizmo(uuid, "assets/models/cube_1x1x1.glb");

        if (gizmo) {
          gizmo.setPosition(add(transform.getPosition(), collider.getLocalPosition()));
          gizmo.setScale(multiply(transform.getScale(), collider.getLocalScale()));
          gizmo.setOrientationEuler(add(transform.getRotation(), collider.getLocalRotation()));

          renderer3D.renderObject(gizmo, PipelineType.objectHighlight);
        }
      } else if (collider instanceof SphereCollider && collider.getRenderCollider()) {
        const gizmo = assetCache.getColliderGizmo(uuid, "assets/models/sphere_3.glb");

        if (gizmo) {
          gizmo.setPosition(add(transform.getPosition(), collider.getLocalPosition()));
          gizmo.setScale(scaleBy(transform.getScale(), collider.getLocalRadius()));

          renderer3D.renderObject(gizmo, PipelineType.objectHighlight);
        }
      }
    }
  }

  // Points the viewport at the given object's Camera (this client's own player camera), or at the
  // scene's first active Camera when cameraObject is null. With none, the free-fly camera takes over.
  updateCamera(
    objectManager: ObjectManager,
    assetCache: GpuAssetCache,
    cameraObject: string | null,
  ): void {
    const renderer = assetCache.getRenderer();

    for (const object of objectManager.getAllObjects()) {
      if (cameraObject !== null && object.getUUID() !== cameraObject) {
        continue;
      }

      const camera = object.getComponent<Camera>(ComponentType.camera);
      if (!camera || !camera.isActive()) {
        continue;
      }

      const transform = object.getComponent<Transform>(ComponentType.transform);
      if (!transform) {
        continue;
      }

      // Position comes from the Transform; facing is the Camera's own direction rotated by the
      // object's orientation, so the camera turns as the object turns. World-up (not an
      // orientation-derived up) keeps the horizon level.
      const position = transform.getPosition();
      const orientation = eulerToQuat(toRadians(transform.getRotation()));

      let forward = rotateByQuat(orientation, camera.getDirection());
      if (length(forward) < 1e-6) {
        forward = [0, 0, -1]; // guard an un-set (zero) direction
      }
      forward = normalize(forward);

      // lookAt degenerates when the view direction is parallel to up (looking straight up/down);
      // fall back to a different reference axis so the matrix stays finite.
      let up: Vec3 = [0, 1, 0];
      if (Math.abs(dot(forward, up)) > 0.9999) {
        up = [0, 0, 1];
      }

      const target: Vec3 = [
        position[0] + forward[0],
        position[1] + forward[1],
        position[2] + forward[2],
      ];

      // Take over from the built-in free-fly camera (render() skips it while disabled).
      renderer.getCamera().disable();
      renderer
        .getRenderingManager()
        .getRenderer3D()
        .setCameraParameters(position, mat4LookAt(position, target, up));
      return;
    }

    // No active component camera in the scene - hand control back to the free-fly camera.
    enableFreeFlyCamera(renderer);
  }

  useFreeFlyCamera(assetCache: GpuAssetCache): void {
    enableFreeFlyCamera(assetCache.getRenderer());
  }
}

// Hands the viewport back to the built-in free-fly camera. render() only pushes the free-fly pose
// while the camera is enabled, so push it once here too - otherwise the component camera's last pose
// would linger.
function enableFreeFlyCamera(renderer: WebGPUEngine): void {
  const camera = renderer.getCamera();

  if (camera.isEnabled()) {
    return;
  }

  camera.enable();
  renderer
    .getRenderingManager()
    .getRenderer3D()
    .setCameraParameters(camera.getPosition(), camera.getViewMatrix());
}

// --- glm stand-ins -----------------------------------------------------------------------------
// Math.ts has no quaternion type, and the camera needs exactly glm's euler->quat convention, so the
// two operations RenderSystem uses are reproduced here rather than approximated with matrices.

type Quat = [number, number, number, number]; // x, y, z, w

// glm::quat(vec3 eulerAngles) - angles in radians, applied as glm does.
function eulerToQuat(euler: Vec3): Quat {
  const [x, y, z] = euler;

  const cx = Math.cos(x * 0.5);
  const sx = Math.sin(x * 0.5);
  const cy = Math.cos(y * 0.5);
  const sy = Math.sin(y * 0.5);
  const cz = Math.cos(z * 0.5);
  const sz = Math.sin(z * 0.5);

  return [
    sx * cy * cz - cx * sy * sz,
    cx * sy * cz + sx * cy * sz,
    cx * cy * sz - sx * sy * cz,
    cx * cy * cz + sx * sy * sz,
  ];
}

// q * v * conjugate(q), the glm quat-vector product.
function rotateByQuat(q: Quat, v: Vec3): Vec3 {
  const [qx, qy, qz, qw] = q;

  // t = 2 * cross(q.xyz, v)
  const tx = 2 * (qy * v[2] - qz * v[1]);
  const ty = 2 * (qz * v[0] - qx * v[2]);
  const tz = 2 * (qx * v[1] - qy * v[0]);

  // v + qw * t + cross(q.xyz, t)
  return [
    v[0] + qw * tx + (qy * tz - qz * ty),
    v[1] + qw * ty + (qz * tx - qx * tz),
    v[2] + qw * tz + (qx * ty - qy * tx),
  ];
}

function toRadians(degrees: Vec3): Vec3 {
  const factor = Math.PI / 180;
  return [degrees[0] * factor, degrees[1] * factor, degrees[2] * factor];
}

function length(v: Vec3): number {
  return Math.hypot(v[0], v[1], v[2]);
}

function normalize(v: Vec3): Vec3 {
  const l = length(v) || 1;
  return [v[0] / l, v[1] / l, v[2] / l];
}

function dot(a: Vec3, b: Vec3): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function add(a: Vec3, b: Vec3): Vec3 {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

function multiply(a: Vec3, b: Vec3): Vec3 {
  return [a[0] * b[0], a[1] * b[1], a[2] * b[2]];
}

function scaleBy(v: Vec3, scalar: number): Vec3 {
  return [v[0] * scalar, v[1] * scalar, v[2] * scalar];
}
