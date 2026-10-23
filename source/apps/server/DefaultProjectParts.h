#ifndef DEFAULTPROJECTPARTS_H
#define DEFAULTPROJECTPARTS_H

#include "DefaultShapes.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

// The built-in project's shared pieces: the stable asset and prefab uuids and the builders the scenes are
// made from, so a scene in its own file builds objects the same way DefaultProject.cpp does.
namespace defaultproject {

// Stable asset UUIDs for the shared models/textures/script. Object UUIDs are freshly generated each launch (newUUID).
inline const std::string cubeModel = "58034695-0495-4cfb-8f75-90cf24a9073b";
inline const std::string playerModel = "bcf01ddf-e82a-4ed1-90a7-b93b4184f647";
inline const std::string sphereModel = "a4c4a30b-735b-4b1c-9140-19f62ca3c720";
inline const std::string whiteTexture = "a3628d76-f753-4e27-be83-35744d1ac44d";
inline const std::string earthTexture = "2cfcc1b3-86f7-487b-bf37-6255111ab0ca";
inline const std::string earthSpecularTexture = "9e4b3267-a29c-43ac-aca4-ccd06f078f5c";
inline const std::string playerScript = "de114693-8cf3-4123-a308-70dbb2af120f";
inline const std::string capsuleModel = "953f4317-65aa-4eb0-b186-c0b94bec185e";
inline const std::string rampModel = "e77194e0-e50e-4070-aacd-2f5d917b2da7";
inline const std::string planeTileModel = "6a742837-aa60-43fe-a652-32a3d385cde0";
inline const std::string torusModel = "7a1cd753-3775-44a0-858f-27239f8740f5";
inline const std::string cylinderModel = "a383cc8f-ee6e-4373-ae06-f10f85ef159b";
inline const std::string d4Model = "4e4e2499-ff78-43d8-998e-c7cfc149337e";
inline const std::string d8Model = "6539110c-cfb2-4d1e-8888-6dcefc76a66b";
inline const std::string d12Model = "5f4873d6-d2c7-4313-b232-728b949f1dd6";
inline const std::string d20Model = "f810cee9-5c0b-47f6-9943-ae185c0105bd";
inline const std::string hexPrismModel = "dc62625e-9c71-4de3-9a1d-56e6d7ba47c0";
inline const std::string drumModel = "75fac0ef-08ca-4c20-81fa-de5aa9269d50";
inline const std::string gemModel = "ad754062-1662-4446-8aac-aefa926d49b6";
inline const std::string boulderModel = "9d0e47fd-8a6f-4ff9-9614-ddb0c5dddd5a";
inline const std::string gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
inline const std::string checkerOrangeTexture = "425f3784-74eb-4e09-bfbb-88facfb014d0";
inline const std::string checkerBlueTexture = "05325ffd-75bd-4d76-a50d-7b80e183b081";
inline const std::string paletteRedTexture = "7c8c01db-771f-4048-94c2-004b8d19e7aa";
inline const std::string paletteGreenTexture = "c5eee5e6-9bd0-4f9b-977f-b9220a21a630";
inline const std::string paletteBlueTexture = "66a8a6fc-d151-4567-9ce5-2e17cc675fe0";
inline const std::string paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
inline const std::string paletteOrangeTexture = "ea184ad8-ca64-48cd-b69e-92bcb4649158";
inline const std::string palettePurpleTexture = "8925b024-c923-4839-9e6b-fa67e69e6752";

// Stable prefab UUIDs, so a script can spawn one by a uuid it can hardcode (World.spawnPrefab).
inline const std::string blockPrefab = "0a1e5c2d-7b3f-4a91-8c60-1d4e2f6a8b03";
inline const std::string rigidBlockPrefab = "3f8c1a47-92d5-4e08-b1a6-5c7e09f4d2b1";
inline const std::string spherePrefab = "6d2b4f19-08ac-4d73-9e52-b83a1c6f0e47";
inline const std::string playerPrefab = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
inline const std::string capsulePrefab = "39e415e0-ff7a-475c-b677-72062ce2954a";
inline const std::string cylinderPrefab = "62f97ec2-a58e-4e6d-b9e4-a466dc04669a";
inline const std::string rampPrefab = "1b112469-4331-402e-bc4a-94cd0368ef52";
inline const std::string d4Prefab = "d973cf42-3ef2-4ea6-a4a4-e505fa2d0c17";
inline const std::string d6Prefab = "f02de30b-fa89-400f-b2ba-fbabcd219775";
inline const std::string d8Prefab = "fab33a9c-0c55-488f-b5ab-516cc7558075";
inline const std::string d12Prefab = "c901eb04-d6b1-480b-a556-19f9fba037c7";
inline const std::string d20Prefab = "32cf047d-3fa4-40a5-9dbd-5377aa5bbaa1";
inline const std::string hexPrismPrefab = "cb56959b-90d6-4b14-b2bb-64bc5e3b08f1";
inline const std::string drumPrefab = "41980fde-c3d9-4672-87a3-71d80acdc734";
inline const std::string gemPrefab = "98f4b839-f256-4562-b8ff-266094c98725";
inline const std::string boulderPrefab = "dca2e8d6-7dab-4706-84f2-61da7ad076f1";

// A library shape and the hull its prefab, model and collider are built from.
struct LibraryShape
{
  std::string name;
  ConvexHull (*hull)();
};

[[nodiscard]] const std::vector<LibraryShape>& libraryShapes();

// The prefab body of a library shape, by name.
[[nodiscard]] const nlohmann::json& libraryBody(const std::string& name);

[[nodiscard]] const nlohmann::json& capsuleBody();
[[nodiscard]] const nlohmann::json& rampBody();

[[nodiscard]] std::string newUUID();

[[nodiscard]] nlohmann::json vec(const glm::vec3& v);

[[nodiscard]] nlohmann::json transform(const glm::vec3& position,
                                       const glm::vec3& scale = glm::vec3(1),
                                       const glm::vec3& rotation = glm::vec3(0));

[[nodiscard]] nlohmann::json modelRenderer(const std::string& model, const std::string& texture,
                                           const std::string& specular);

[[nodiscard]] nlohmann::json makeObject(const std::string& name, nlohmann::json components,
                                        nlohmann::json scripts = nlohmann::json::array());

// A copy of a prefab body with a fresh uuid and the given root transform.
[[nodiscard]] nlohmann::json instanceOf(nlohmann::json body, const glm::vec3& position,
                                        const glm::vec3& scale = glm::vec3(1),
                                        const glm::vec3& rotation = glm::vec3(0));

[[nodiscard]] nlohmann::json withMass(nlohmann::json object, float mass);

[[nodiscard]] nlohmann::json block(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1),
                                   const glm::vec3& rotation = glm::vec3(0));

[[nodiscard]] nlohmann::json rigidBlock(const glm::vec3& position, const glm::vec3& scale,
                                        const glm::vec3& rotation = glm::vec3(0));

[[nodiscard]] nlohmann::json sphere(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1));

[[nodiscard]] nlohmann::json player(const glm::vec3& position, int slot = 0,
                                    const glm::vec3& cameraDirection = glm::vec3(0, 0, -1));

[[nodiscard]] nlohmann::json light(const glm::vec3& position, const glm::vec3& color, float ambient,
                                   float diffuse, float specular);

// A scene asset: its name, uuid and objects. The builders below return the objects of the built-in scenes.
[[nodiscard]] nlohmann::json scene(const std::string& name, const std::string& uuid, nlohmann::json objects);

[[nodiscard]] nlohmann::json buildScene1();
[[nodiscard]] nlohmann::json buildScene2();
[[nodiscard]] nlohmann::json buildScene3();
[[nodiscard]] nlohmann::json buildFallingBalls();

}

#endif //DEFAULTPROJECTPARTS_H
