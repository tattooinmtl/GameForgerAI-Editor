#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Core/FrameProfiler.hpp"
#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"
#include "GameForger/Editor/AIChatResponse.hpp"
#include "GameForger/Editor/AICommand.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/AudioSourceEffects.hpp"
#include "GameForger/Editor/Json.hpp"
#include "GameForger/Editor/PrimitiveMeshes.hpp"
#include "GameForger/Editor/SceneSerializer.hpp"
#include "GameForger/Editor/Transform.hpp"
#include "GameForger/Runtime/GameCamera.hpp"
#include "GameForger/Editor/MindGraph/GraphCompiler.hpp"
#include "GameForger/Editor/MindGraph/GraphSerializer.hpp"
#include "GameForger/Editor/MindGraph/NodeCatalog.hpp"
#include "GameForger/Editor/Storyboard.hpp"

using namespace gameforger::editor;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(expr, msg) \
	do { \
		if (!(expr)) { \
			std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
			++g_testsFailed; \
			return; \
		} \
	} while (false)

#define RUN_TEST(fn) \
	do { \
		const int failedBefore = g_testsFailed; \
		std::cout << "[ RUN      ] " #fn "\n"; \
		fn(); \
		if (g_testsFailed == failedBefore) { \
			++g_testsPassed; \
			std::cout << "[       OK ] " #fn "\n"; \
		} \
	} while (false)

// ----------------------------------------------------------------------------
// Test: Primitive Meshes (Normals & CCW Winding Order - Q-1, Q-2)
// ----------------------------------------------------------------------------
void testPrimitiveMeshes()
{
	// 1. Capsule bottom hemisphere normals test (Q-1)
	const PrimitiveMeshData capsule = generatePrimitiveMesh(PrimitiveType::Capsule);
	TEST_ASSERT(!capsule.vertices.empty(), "Capsule mesh vertices must not be empty");
	TEST_ASSERT(capsule.vertices.size() % 6 == 0, "Vertex stride must be 6 floats (pos + norm)");

	const std::size_t vertexCount = capsule.vertices.size() / 6;
	for (std::size_t i = 0; i < vertexCount; ++i)
	{
		const glm::vec3 pos{capsule.vertices[i * 6 + 0], capsule.vertices[i * 6 + 1], capsule.vertices[i * 6 + 2]};
		const glm::vec3 norm{capsule.vertices[i * 6 + 3], capsule.vertices[i * 6 + 4], capsule.vertices[i * 6 + 5]};

		// For bottom hemisphere (y < -0.5), center is at (0, -0.5, 0)
		if (pos.y < -0.5001F)
		{
			const glm::vec3 bottomCenter{0.0F, -0.5F, 0.0F};
			const glm::vec3 toSurface = glm::normalize(pos - bottomCenter);
			const float dot = glm::dot(norm, toSurface);
			TEST_ASSERT(dot > 0.8F, "Capsule bottom hemisphere normal must point outwards from hemisphere center");
		}
	}

	// 2. Triangle normal direction vs computed geometric cross product (Winding order - Q-2)
	auto verifyWindingOrder = [](const PrimitiveType type, const char* name)
	{
		const PrimitiveMeshData mesh = generatePrimitiveMesh(type);
		const std::size_t triangles = mesh.vertices.size() / 18;
		for (std::size_t t = 0; t < triangles; ++t)
		{
			const std::size_t base = t * 18;
			const glm::vec3 p0{mesh.vertices[base + 0], mesh.vertices[base + 1], mesh.vertices[base + 2]};
			const glm::vec3 n0{mesh.vertices[base + 3], mesh.vertices[base + 4], mesh.vertices[base + 5]};

			const glm::vec3 p1{mesh.vertices[base + 6], mesh.vertices[base + 7], mesh.vertices[base + 8]};
			const glm::vec3 p2{mesh.vertices[base + 12], mesh.vertices[base + 13], mesh.vertices[base + 14]};

			const glm::vec3 edge1 = p1 - p0;
			const glm::vec3 edge2 = p2 - p0;
			const glm::vec3 geomNormal = glm::cross(edge1, edge2);

			if (glm::length(geomNormal) > 0.0001F)
			{
				const float alignment = glm::dot(glm::normalize(geomNormal), n0);
				TEST_ASSERT(alignment > 0.0F, std::string(name) + " triangle winding order must produce outward geometric normal");
			}
		}
	};

	verifyWindingOrder(PrimitiveType::Cube, "Cube");
	verifyWindingOrder(PrimitiveType::Sphere, "Sphere");
	verifyWindingOrder(PrimitiveType::Cylinder, "Cylinder");
	verifyWindingOrder(PrimitiveType::Cone, "Cone");
	verifyWindingOrder(PrimitiveType::Capsule, "Capsule");
}

// ----------------------------------------------------------------------------
// Test: JSON Parser Hardening (T1-4)
// ----------------------------------------------------------------------------
void testJsonParser()
{
	// 1. Basic parsing
	const std::string jsonDoc = R"({
		"name": "Player",
		"active": true,
		"health": 100.5,
		"tags": ["Hero", "Controllable"],
		"nested": {
			"score": 42
		}
	})";

	const std::optional<json::Value> parsed = json::parse(jsonDoc);
	TEST_ASSERT(parsed.has_value(), "Valid JSON should parse successfully");
	TEST_ASSERT(parsed->type == json::Value::Type::Object, "Root should be object");

	const json::Value* nameVal = parsed->find("name");
	TEST_ASSERT(nameVal != nullptr && nameVal->type == json::Value::Type::String && nameVal->stringValue == "Player", "name field check");

	const json::Value* activeVal = parsed->find("active");
	TEST_ASSERT(activeVal != nullptr && activeVal->type == json::Value::Type::Boolean && activeVal->boolValue == true, "active field check");

	const json::Value* healthVal = parsed->find("health");
	TEST_ASSERT(healthVal != nullptr && healthVal->type == json::Value::Type::Number && std::abs(healthVal->numberValue - 100.5) < 0.001, "health field check");

	// 2. Reject trailing garbage / invalid JSON
	const std::optional<json::Value> invalidDoc = json::parse("{\"name\": \"Player\"} extra_junk");
	TEST_ASSERT(!invalidDoc.has_value(), "JSON with trailing garbage must be rejected");

	// 3. RFC 8259: leading '+' is not a valid number (F-10 / DEF-05)
	const std::optional<json::Value> plusNumber = json::parse("{\"n\": +42}");
	TEST_ASSERT(!plusNumber.has_value(), "JSON numbers must not accept a leading '+'");

	// 4. UTF-16 surrogate pair \uD83D\uDE00 (😀) must decode to one code point
	const std::optional<json::Value> emoji = json::parse("{\"g\":\"\\uD83D\\uDE00\"}");
	TEST_ASSERT(emoji.has_value(), "Surrogate-pair JSON string must parse");
	const json::Value* g = emoji->find("g");
	TEST_ASSERT(g != nullptr && g->type == json::Value::Type::String, "emoji field must be a string");
	TEST_ASSERT(g->stringValue.size() == 4 &&
			static_cast<unsigned char>(g->stringValue[0]) == 0xF0 &&
			static_cast<unsigned char>(g->stringValue[1]) == 0x9F &&
			static_cast<unsigned char>(g->stringValue[2]) == 0x98 &&
			static_cast<unsigned char>(g->stringValue[3]) == 0x80,
		"\\uD83D\\uDE00 must decode to UTF-8 F0 9F 98 80");

	// 5. Nesting depth is capped. Without a cap this input recurses ~50k deep
	// and overflows the stack - a hard crash, reachable from any AI provider
	// response, Blender MCP reply or scene file the editor is asked to read.
	// Rejection must be graceful (nullopt), not a process death.
	{
		const std::size_t kNesting = 50000;
		std::string deep;
		deep.reserve(kNesting * 2);
		deep.append(kNesting, '[');
		deep.append(kNesting, ']');
		TEST_ASSERT(!json::parse(deep).has_value(),
			"Deeply nested JSON must be rejected rather than overflowing the stack");

		// The cap must not reject documents of a sane shape. Scenes nest ~6
		// levels; 40 is comfortably legal and must still parse.
		std::string legal;
		legal.append(40, '[');
		legal.append(40, ']');
		TEST_ASSERT(json::parse(legal).has_value(),
			"Moderately nested JSON (40 levels) must still parse");
	}
}

// ----------------------------------------------------------------------------
// Test: Scene Serialization & Format Versioning (T1-3, Q-11)
// ----------------------------------------------------------------------------
void testSceneSerialization()
{
	// 1. Format validation test
	const std::filesystem::path fakeSceneFile = "test_invalid_format.json";
	{
		std::ofstream out(fakeSceneFile);
		out << "{\"notAFormat\": 123, \"entities\": []}";
	}
	const SceneLoadResult invalidResult = loadScene(fakeSceneFile);
	TEST_ASSERT(!invalidResult.success, "Loading JSON without 'GameForgerScene' format header must fail");
	std::filesystem::remove(fakeSceneFile);

	// 2. Round-trip entity serialization including active flag (Q-11)
	const std::filesystem::path tempSceneFile = "test_roundtrip.scene";
	std::vector<SceneEntity> originalEntities;
	{
		SceneEntity ent1;
		ent1.name = "ActiveEntity";
		ent1.active = true;
		ent1.primitive = PrimitiveType::Cube;
		ent1.position = glm::vec3(1.0F, 2.0F, 3.0F);

		SceneEntity ent2;
		ent2.name = "InactiveEntity";
		ent2.active = false;
		ent2.primitive = PrimitiveType::Sphere;
		ent2.position = glm::vec3(-1.0F, 0.0F, 5.0F);

		originalEntities.push_back(ent1);
		originalEntities.push_back(ent2);
	}

	const SceneSaveResult saveResult = saveScene(tempSceneFile, originalEntities);
	TEST_ASSERT(saveResult.success, "Saving scene must succeed");

	const SceneLoadResult loadResult = loadScene(tempSceneFile);
	TEST_ASSERT(loadResult.success, "Loading scene must succeed");
	TEST_ASSERT(loadResult.entities.size() == 2, "Loaded entities count must be 2");

	TEST_ASSERT(loadResult.entities[0].name == "ActiveEntity", "Entity 1 name");
	TEST_ASSERT(loadResult.entities[0].active == true, "Entity 1 active flag");

	TEST_ASSERT(loadResult.entities[1].name == "InactiveEntity", "Entity 2 name");
	TEST_ASSERT(loadResult.entities[1].active == false, "Entity 2 active flag");

	std::filesystem::remove(tempSceneFile);
	std::filesystem::remove(tempSceneFile.string() + ".bak");

	// Collider type round-trip (Box / Mesh / Convex)
	const std::filesystem::path colliderSceneFile = "test_collider_type.scene";
	{
		SceneEntity boxEntity;
		boxEntity.name = "BoxCol";
		boxEntity.hasCollider = true;
		boxEntity.colliderType = ColliderType::Box;
		SceneEntity meshEntity;
		meshEntity.name = "MeshCol";
		meshEntity.hasCollider = true;
		meshEntity.colliderType = ColliderType::Mesh;
		meshEntity.isImportedMesh = true;
		SceneEntity convexEntity;
		convexEntity.name = "ConvexCol";
		convexEntity.hasCollider = true;
		convexEntity.colliderType = ColliderType::Convex;
		const SceneSaveResult typedSave =
			saveScene(colliderSceneFile, {boxEntity, meshEntity, convexEntity});
		TEST_ASSERT(typedSave.success, "Saving collider types must succeed");
		const SceneLoadResult typedLoad = loadScene(colliderSceneFile);
		TEST_ASSERT(typedLoad.success && typedLoad.entities.size() == 3, "Load collider types");
		TEST_ASSERT(typedLoad.entities[0].colliderType == ColliderType::Box, "Box type round-trip");
		TEST_ASSERT(typedLoad.entities[1].colliderType == ColliderType::Mesh, "Mesh type round-trip");
		TEST_ASSERT(typedLoad.entities[2].colliderType == ColliderType::Convex, "Convex type round-trip");
		std::filesystem::remove(colliderSceneFile);
		std::filesystem::remove(colliderSceneFile.string() + ".bak");
	}
}

// ----------------------------------------------------------------------------
// Test: EditorScene Commands (Creation, Finding, Unique Naming)
// ----------------------------------------------------------------------------
void testEditorScene()
{
	EditorScene scene(".");

	// 1. Create entities via commands
	const AICommandResult res1 = scene.execute(CreateEntityCommand{"Player", PrimitiveType::Cube, glm::vec3(0.0F)});
	TEST_ASSERT(res1.success, "Creating entity 'Player' must succeed");

	const SceneEntity* player = scene.findEntity("Player");
	TEST_ASSERT(player != nullptr, "findEntity('Player') must return pointer");
	TEST_ASSERT(player->name == "Player", "player->name check");

	// 2. Duplicate create with explicit duplicate name is rejected by command bus
	const AICommandResult res2 = scene.execute(CreateEntityCommand{"Player", PrimitiveType::Sphere, glm::vec3(1.0F)});
	TEST_ASSERT(!res2.success, "Creating second entity with already-in-use name 'Player' must be rejected");

	// 3. Duplicating entity generates unique name with (1) suffix
	const AICommandResult res3 = scene.execute(DuplicateEntityCommand{"Player"});
	TEST_ASSERT(res3.success, "DuplicateEntityCommand('Player') must succeed");

	const SceneEntity* player2 = scene.findEntity("Player (1)");
	TEST_ASSERT(player2 != nullptr, "findEntity('Player (1)') must return unique duplicated entity");
	TEST_ASSERT(scene.entities().size() == 2, "Total entities count must be 2");

	// 4. Rename rewrites children's parentName (F-02 / R-01)
	const AICommandResult childCreate = scene.execute(CreateEntityCommand{"Turret", PrimitiveType::Cube, glm::vec3(1.0F)});
	TEST_ASSERT(childCreate.success, "Creating child 'Turret' must succeed");
	const AICommandResult parented = scene.execute(
		SetPropertyCommand{"Turret", "Parent", "parentName", std::string("Player")});
	TEST_ASSERT(parented.success, "Parenting Turret under Player must succeed");
	TEST_ASSERT(scene.findEntity("Turret") != nullptr && scene.findEntity("Turret")->parentName == "Player",
		"Turret.parentName must be Player before rename");

	const AICommandResult renamed = scene.execute(RenameEntityCommand{"Player", "Hero"});
	TEST_ASSERT(renamed.success, "Renaming Player to Hero must succeed");
	TEST_ASSERT(scene.findEntity("Player") == nullptr, "Old name Player must be gone");
	TEST_ASSERT(scene.findEntity("Hero") != nullptr, "Hero must exist after rename");
	const SceneEntity* turretAfterRename = scene.findEntity("Turret");
	TEST_ASSERT(turretAfterRename != nullptr && turretAfterRename->parentName == "Hero",
		"Child parentName must follow parent rename");

	// 5. Delete parent promotes children to root (clears parentName) without deleting them
	const AICommandResult deleted = scene.execute(DeleteEntityCommand{"Hero"});
	TEST_ASSERT(deleted.success, "Deleting Hero must succeed");
	TEST_ASSERT(scene.findEntity("Hero") == nullptr, "Hero must be gone");
	const SceneEntity* turretAfterDelete = scene.findEntity("Turret");
	TEST_ASSERT(turretAfterDelete != nullptr, "Child must survive parent delete");
	TEST_ASSERT(turretAfterDelete->parentName.empty(), "Orphaned child must be promoted to root");
}

// ----------------------------------------------------------------------------
// Test: ScriptRuntime Sandboxing & _ENV Isolation (T1-6)
// ----------------------------------------------------------------------------
#include "GameForger/Editor/InputSource.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"

class MockInputSource : public InputSource
{
public:
	[[nodiscard]] bool isKeyDown(const std::string&) const override { return false; }
	[[nodiscard]] bool isKeyPressed(const std::string&) const override { return false; }
	[[nodiscard]] float getAxis(const std::string&, const std::string&) const override { return 0.0F; }
	[[nodiscard]] float getMouseDeltaX() const override { return 0.0F; }
	[[nodiscard]] float getMouseDeltaY() const override { return 0.0F; }
	[[nodiscard]] bool isMouseButtonDown(const std::string&) const override { return false; }
	[[nodiscard]] float getScrollDelta() const override { return 0.0F; }
};

void testScriptRuntimeSandboxing()
{
	const std::filesystem::path scriptsDir = "Game/Scripts";
	std::filesystem::create_directories(scriptsDir);

	const std::filesystem::path script1Path = scriptsDir / "test_env1.lua";
	const std::filesystem::path script2Path = scriptsDir / "test_env2.lua";

	{
		std::ofstream out1(script1Path);
		out1 << R"(
			globalPollution = 12345
			local Controller = { val = 10 }
			function Controller:on_start()
				self.val = 20
			end
			return Controller
		)";
	}

	{
		std::ofstream out2(script2Path);
		out2 << R"(
			local Controller = { hasLeak = false }
			function Controller:on_start()
				if globalPollution ~= nil then
					self.hasLeak = true
				end
			end
			return Controller
		)";
	}

	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
	MockInputSource input;
	ScriptRuntime runtime;

	std::vector<std::string> logs;
	ScriptRuntime::Config runtimeConfig;
	runtimeConfig.logCallback = [&logs](bool isError, const std::string& msg) {
		if (isError) logs.push_back(msg);
	};
	runtime.initialize(scene, bus, input, runtimeConfig);

	TEST_ASSERT(runtime.isRunning(), "ScriptRuntime must be running");

	// Start script 1 on entity 1
	const bool start1 = runtime.startScript(1, "Game/Scripts/test_env1.lua", ".");
	TEST_ASSERT(start1, "startScript for Game/Scripts/test_env1.lua must succeed");

	// Start script 2 on entity 2
	const bool start2 = runtime.startScript(2, "Game/Scripts/test_env2.lua", ".");
	TEST_ASSERT(start2, "startScript for Game/Scripts/test_env2.lua must succeed");

	// Verify that script 1's global assignment did not leak to script 2
	const float leaked = runtime.getScriptNumberField(2, "Game/Scripts/test_env2.lua", "hasLeak", 0.0F);
	TEST_ASSERT(leaked == 0.0F, "globalPollution must not leak from script 1 to script 2 (_ENV isolation)");

	runtime.shutdown();
	TEST_ASSERT(!runtime.isRunning(), "ScriptRuntime must shut down");

	std::filesystem::remove(script1Path);
	std::filesystem::remove(script2Path);
}

void testGetRightMatchesFpsCamera()
{
	// GLM lookAtRH screen-right is cross(forward, +Y). At yaw 0 the entity
	// looks +Z, so D-strafe (getRight) must be -X, matching the Game view.
	const std::filesystem::path scriptsDir = "Game/Scripts";
	std::filesystem::create_directories(scriptsDir);
	const std::filesystem::path scriptPath = scriptsDir / "test_get_right.lua";
	{
		std::ofstream out(scriptPath);
		out << R"(
			local Controller = { right_x = 0.0 }
			function Controller:on_start()
				self.right_x = self.entity:getRight().x
			end
			return Controller
		)";
	}

	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
	MockInputSource input;
	ScriptRuntime runtime;
	runtime.initialize(scene, bus, input, ScriptRuntime::Config{});

	TEST_ASSERT(runtime.startScript(1, "Game/Scripts/test_get_right.lua", "."),
		"getRight probe script must start");
	const float rightX = runtime.getScriptNumberField(1, "Game/Scripts/test_get_right.lua", "right_x", 0.0F);
	TEST_ASSERT(rightX < -0.5F,
		"getRight at yaw 0 must point -X (FPS camera screen-right), not +X");

	runtime.shutdown();
	std::filesystem::remove(scriptPath);
}

// ----------------------------------------------------------------------------
// Test: GameObject & Component Architecture (T2-1)
// ----------------------------------------------------------------------------
#include "GameForger/Core/GameObject.hpp"

void testGameObjectComponentModel()
{
	using namespace gameforger::core;

	GameObject go("Hero", 42);
	TEST_ASSERT(go.name() == "Hero", "GameObject name check");
	TEST_ASSERT(go.id() == 42, "GameObject id check");
	TEST_ASSERT(go.activeSelf() == true, "GameObject default active check");

	// 1. TransformComponent is automatically attached
	TransformComponent* transform = go.getComponent<TransformComponent>();
	TEST_ASSERT(transform != nullptr, "GameObject must have TransformComponent by default");
	TEST_ASSERT(&go.transform() == transform, "go.transform() returns default TransformComponent");

	go.transform().position = glm::vec3(5.0F, 10.0F, -2.0F);
	TEST_ASSERT(go.transform().position.y == 10.0F, "Transform position set");

	// 2. Add MeshRendererComponent and ColliderComponent
	auto* renderer = go.addComponent<MeshRendererComponent>();
	TEST_ASSERT(renderer != nullptr, "addComponent<MeshRendererComponent> returns pointer");
	renderer->color = glm::vec3(1.0F, 0.0F, 0.0F);

	auto* collider = go.addComponent<ColliderComponent>();
	TEST_ASSERT(collider != nullptr, "addComponent<ColliderComponent> returns pointer");
	collider->shape = ColliderShape::Box;
	collider->isTrigger = true;

	// 3. Query components
	TEST_ASSERT(go.getComponent<MeshRendererComponent>() == renderer, "getComponent<MeshRendererComponent>");
	TEST_ASSERT(go.getComponent<ColliderComponent>() == collider, "getComponent<ColliderComponent>");
	TEST_ASSERT(go.getComponent<LightComponent>() == nullptr, "Unattached component returns nullptr");

	// 4. Attach multiple components of same family (e.g. 2 colliders)
	auto* sphereCollider = go.addComponent<ColliderComponent>();
	sphereCollider->shape = ColliderShape::Sphere;
	sphereCollider->radius = 1.5F;

	const auto colliders = go.getComponents<ColliderComponent>();
	TEST_ASSERT(colliders.size() == 2, "getComponents returns both collider instances");
	TEST_ASSERT(colliders[0]->shape == ColliderShape::Box, "First collider is Box");
	TEST_ASSERT(colliders[1]->shape == ColliderShape::Sphere, "Second collider is Sphere");

	// 5. Remove component
	const bool removed = go.removeComponent<MeshRendererComponent>();
	TEST_ASSERT(removed, "removeComponent<MeshRendererComponent> returns true");
	TEST_ASSERT(go.getComponent<MeshRendererComponent>() == nullptr, "MeshRendererComponent removed");
}

// ----------------------------------------------------------------------------
// Test: GUID-Based Asset Database & .meta Pipeline (T2-3)
// ----------------------------------------------------------------------------
#include "GameForger/Core/AssetDatabase.hpp"

void testAssetDatabase()
{
	using namespace gameforger::core;

	const std::filesystem::path projRoot = "test_asset_proj";
	const std::filesystem::path texDir = projRoot / "Game" / "Textures";
	std::filesystem::create_directories(texDir);

	const std::filesystem::path sampleAsset = texDir / "rock.png";
	{
		std::ofstream out(sampleAsset, std::ios::binary);
		out << "PNG_FAKE_DATA";
	}

	AssetDatabase db(projRoot);

	// 1. .meta sidecar file must be automatically generated
	const std::filesystem::path metaFile = sampleAsset.string() + ".meta";
	TEST_ASSERT(std::filesystem::exists(metaFile), ".meta sidecar file must exist");

	// 2. GUID lookup from relative path
	const auto guid = db.getGuidFromPath("Game/Textures/rock.png");
	TEST_ASSERT(guid.has_value() && !guid->empty(), "getGuidFromPath must return valid GUID");
	TEST_ASSERT(guid->size() >= 16, "GUID length must be at least 16 chars");

	// 3. Path lookup from GUID
	const auto path = db.getPathFromGuid(*guid);
	TEST_ASSERT(path.has_value(), "getPathFromGuid must find path");
	TEST_ASSERT(path->generic_string() == "Game/Textures/rock.png", "Resolved path matches original");

	// 4. Clean up test files
	std::error_code ec;
	std::filesystem::remove_all(projRoot, ec);
}

// ----------------------------------------------------------------------------
// Test: Standalone Material Asset System (.gfmat) (T2-4)
// ----------------------------------------------------------------------------
#include "GameForger/Core/Material.hpp"

void testMaterialSerialization()
{
	using namespace gameforger::core;

	Material original;
	original.name = "GoldPBR";
	original.shader = "StandardPBR";
	original.blendMode = MaterialBlendMode::Opaque;
	original.albedoColor = glm::vec4(1.0F, 0.85F, 0.57F, 1.0F);
	original.metallic = 0.95F;
	original.roughness = 0.15F;
	original.normalScale = 1.2F;
	original.albedoMapGuid = "ab12cd34ef567890";
	original.uvScale = glm::vec2(2.5F, 2.5F);

	const std::filesystem::path matFile = "test_gold.gfmat";
	const bool saved = saveMaterialFile(matFile, original);
	TEST_ASSERT(saved, "Saving material file must succeed");

	const auto loaded = loadMaterialFile(matFile);
	TEST_ASSERT(loaded.has_value(), "Loading material file must succeed");

	TEST_ASSERT(loaded->name == "GoldPBR", "Material name check");
	TEST_ASSERT(loaded->shader == "StandardPBR", "Shader check");
	TEST_ASSERT(loaded->blendMode == MaterialBlendMode::Opaque, "BlendMode check");
	TEST_ASSERT(std::abs(loaded->metallic - 0.95F) < 0.001F, "Metallic check");
	TEST_ASSERT(std::abs(loaded->roughness - 0.15F) < 0.001F, "Roughness check");
	TEST_ASSERT(std::abs(loaded->albedoColor.r - 1.0F) < 0.001F, "Albedo r check");
	TEST_ASSERT(std::abs(loaded->albedoColor.g - 0.85F) < 0.001F, "Albedo g check");
	TEST_ASSERT(loaded->albedoMapGuid == "ab12cd34ef567890", "Albedo GUID check");
	TEST_ASSERT(std::abs(loaded->uvScale.x - 2.5F) < 0.001F, "UV Scale check");

	// Invalid format rejection
	const auto invalidMat = deserializeMaterial("{\"format\": \"NotAMat\", \"name\": \"Bad\"}");
	TEST_ASSERT(!invalidMat.has_value(), "Invalid material format must be rejected");

	std::filesystem::remove(matFile);
}

// ----------------------------------------------------------------------------
// Test: Inspector Script Parameter Reflection (T2-2)
// ----------------------------------------------------------------------------
void testScriptPropertyReflection()
{
	const std::filesystem::path scriptsDir = "Game/Scripts";
	std::filesystem::create_directories(scriptsDir);
	const std::filesystem::path scriptPath = scriptsDir / "test_reflected_props.lua";

	{
		std::ofstream out(scriptPath);
		out << R"(-- @property speed number 7.5
-- @property greeting string Hello Antigravity
-- @property isBoss bool true
-- @property spawnOffset vec3 1.0 2.0 3.0

local Controller = {
	speed = 7.5,
	greeting = "Hello Antigravity",
	isBoss = true
}

function Controller:on_start()
end

return Controller
)";
	}

	// 1. Static source property parsing
	const auto props = gameforger::editor::ScriptRuntime::parseScriptProperties(scriptPath);
	TEST_ASSERT(props.size() == 4, "Must parse exactly 4 annotated properties");
	TEST_ASSERT(props[0].name == "speed" && props[0].type == gameforger::editor::ScriptRuntime::ExposedScriptProperty::Type::Number, "Prop 0 is speed (Number)");
	TEST_ASSERT(std::abs(props[0].defaultNumber - 7.5F) < 0.001F, "Prop 0 default is 7.5");
	TEST_ASSERT(props[1].name == "greeting" && props[1].type == gameforger::editor::ScriptRuntime::ExposedScriptProperty::Type::String, "Prop 1 is greeting (String)");
	TEST_ASSERT(props[1].defaultString == "Hello Antigravity", "Prop 1 default is Hello Antigravity");
	TEST_ASSERT(props[2].name == "isBoss" && props[2].type == gameforger::editor::ScriptRuntime::ExposedScriptProperty::Type::Bool, "Prop 2 is isBoss (Bool)");
	TEST_ASSERT(props[2].defaultBool == true, "Prop 2 default is true");
	TEST_ASSERT(props[3].name == "spawnOffset" && props[3].type == gameforger::editor::ScriptRuntime::ExposedScriptProperty::Type::Vec3, "Prop 3 is spawnOffset (Vec3)");
	TEST_ASSERT(props[3].defaultVec3.y == 2.0F, "Prop 3 default y is 2.0");

	// 2. Dynamic runtime field manipulation
	gameforger::editor::EditorScene scene(".");
	gameforger::editor::AICommandBus bus;
	bus.setHandler([&scene](const gameforger::editor::AIEditorCommand& cmd) { return scene.execute(cmd); });
	MockInputSource input;
	gameforger::editor::ScriptRuntime runtime;

	runtime.initialize(scene, bus, input, ScriptRuntime::Config{});
	const bool started = runtime.startScript(1, "Game/Scripts/test_reflected_props.lua", ".");
	TEST_ASSERT(started, "Starting script must succeed");

	// Check default values
	TEST_ASSERT(std::abs(runtime.getScriptNumberField(1, "Game/Scripts/test_reflected_props.lua", "speed", 0.0F) - 7.5F) < 0.001F, "Speed initial value");
	TEST_ASSERT(runtime.getScriptStringField(1, "Game/Scripts/test_reflected_props.lua", "greeting", "") == "Hello Antigravity", "Greeting initial value");
	TEST_ASSERT(runtime.getScriptBoolField(1, "Game/Scripts/test_reflected_props.lua", "isBoss", false) == true, "isBoss initial value");

	// Set new values and verify
	runtime.setScriptNumberField(1, "Game/Scripts/test_reflected_props.lua", "speed", 18.0F);
	TEST_ASSERT(std::abs(runtime.getScriptNumberField(1, "Game/Scripts/test_reflected_props.lua", "speed", 0.0F) - 18.0F) < 0.001F, "Updated speed value");

	runtime.setScriptStringField(1, "Game/Scripts/test_reflected_props.lua", "greeting", "Custom Message");
	TEST_ASSERT(runtime.getScriptStringField(1, "Game/Scripts/test_reflected_props.lua", "greeting", "") == "Custom Message", "Updated greeting value");

	runtime.setScriptBoolField(1, "Game/Scripts/test_reflected_props.lua", "isBoss", false);
	TEST_ASSERT(runtime.getScriptBoolField(1, "Game/Scripts/test_reflected_props.lua", "isBoss", true) == false, "Updated isBoss value");

	runtime.shutdown();
	std::filesystem::remove(scriptPath);
}

// ----------------------------------------------------------------------------
// Test: Inspector Collider actually blocks (primitives, imported mesh, parent)
// ----------------------------------------------------------------------------
#include "GameForger/Editor/Collision.hpp"

void testColliderPrimitiveBlocksWhenChecked()
{
	EditorScene scene(".");
	TEST_ASSERT(scene.execute(CreateEntityCommand{"Wall", PrimitiveType::Cube, glm::vec3(0.0F, 1.0F, 0.0F)}).success,
		"create wall");
	TEST_ASSERT(scene.execute(SetPropertyCommand{"Wall", "Collider", "enabled", true}).success, "check Collider");
	const SceneEntity* wall = scene.findEntity("Wall");
	TEST_ASSERT(wall != nullptr && wall->hasCollider, "Inspector Collider must set hasCollider");

	const BoxCollisionResult blocked =
		resolveBoxCollision(scene, 999, glm::vec3(1.2F, 0.0F, 0.0F), 0.4F, 2.0F);
	TEST_ASSERT(blocked.position.x > 1.35F && blocked.position.x < 1.45F,
		"Cube collider at origin scale 1 must push a 0.4-radius mover out to x=1.4");

	TEST_ASSERT(scene.execute(SetPropertyCommand{"Wall", "Collider", "enabled", false}).success, "uncheck Collider");
	const BoxCollisionResult open =
		resolveBoxCollision(scene, 999, glm::vec3(1.2F, 0.0F, 0.0F), 0.4F, 2.0F);
	TEST_ASSERT(std::abs(open.position.x - 1.2F) < 0.001F, "Unchecked Collider must not block");
}

void testImportedMeshColliderUsesTrianglesNotScaleBox()
{
	EditorScene scene(".");
	TEST_ASSERT(scene.execute(CreateEntityCommand{"Castle", PrimitiveType::Cube, glm::vec3(0.0F)}).success,
		"create castle");
	const SceneEntity* castleFound = scene.findEntity("Castle");
	TEST_ASSERT(castleFound != nullptr, "castle entity exists");
	SceneEntity* castle = scene.findEntityMutable(castleFound->id);
	TEST_ASSERT(castle != nullptr, "castle entity");
	castle->isImportedMesh = true;
	castle->hasCollider = true;
	castle->colliderType = ColliderType::Mesh;
	castle->scale = glm::vec3(1.0F);

	// Thin wall at x=10 (local), taller/wider than the default 2x2x2 scale box.
	MeshCollisionGeometry wall;
	wall.triangleVertices = {
		{10.0F, 0.0F, -4.0F}, {10.0F, 4.0F, -4.0F}, {10.0F, 4.0F, 4.0F},
		{10.0F, 0.0F, -4.0F}, {10.0F, 4.0F, 4.0F}, {10.0F, 0.0F, 4.0F},
	};
	wall.localMin = {10.0F, 0.0F, -4.0F};
	wall.localMax = {10.0F, 4.0F, 4.0F};
	wall.valid = true;
	const ImportedMeshProvider provider = [&](const SceneEntity&) { return &wall; };

	const BoxCollisionResult intoWall =
		resolveBoxCollision(scene, 999, glm::vec3(9.7F, 0.0F, 0.0F), 0.4F, 2.0F, provider);
	TEST_ASSERT(intoWall.position.x < 9.65F,
		"Imported-mesh wall at x=10 must push the player back (not ignore the mesh)");

	const BoxCollisionResult courtyard =
		resolveBoxCollision(scene, 999, glm::vec3(0.0F, 0.0F, 0.0F), 0.4F, 2.0F, provider);
	TEST_ASSERT(std::abs(courtyard.position.x) < 0.01F && std::abs(courtyard.position.y) < 0.01F,
		"Courtyard inside a hollow castle must stay walkable (not a solid AABB)");

	const BoxCollisionResult nearOrigin =
		resolveBoxCollision(scene, 999, glm::vec3(0.5F, 0.0F, 0.0F), 0.4F, 2.0F, provider);
	TEST_ASSERT(std::abs(nearOrigin.position.x - 0.5F) < 0.01F,
		"Imported collider must not use the Transform Scale 2x2x2 box");
}

void testParentColliderSolidsChildren()
{
	EditorScene scene(".");
	TEST_ASSERT(scene.execute(CreateEntityCommand{"Castle", PrimitiveType::Cube, glm::vec3(0.0F)}).success,
		"create castle root");
	TEST_ASSERT(scene.execute(CreateEntityCommand{"Wall", PrimitiveType::Cube, glm::vec3(10.0F, 1.0F, 0.0F)}).success,
		"create wall child");
	TEST_ASSERT(scene.execute(SetPropertyCommand{"Wall", "Parent", "parentName", std::string("Castle")}).success,
		"parent wall");
	TEST_ASSERT(scene.execute(SetPropertyCommand{"Castle", "Collider", "enabled", true}).success,
		"check Collider on parent only");

	const BoxCollisionResult intoChild =
		resolveBoxCollision(scene, 999, glm::vec3(8.7F, 0.0F, 0.0F), 0.4F, 2.0F);
	TEST_ASSERT(intoChild.position.x < 8.65F,
		"Collider on castle parent must block against child wall cubes");

	const BoxCollisionResult courtyard =
		resolveBoxCollision(scene, 999, glm::vec3(0.0F, 0.0F, 0.0F), 0.4F, 2.0F);
	TEST_ASSERT(std::abs(courtyard.position.x) < 0.01F && std::abs(courtyard.position.y) < 0.01F,
		"Parent collider must not fill the courtyard with the root cube AABB");
}

// Light / Camera / UI-element round-trip, plus the two invariants that are
// easy to break silently:
//   - enums must survive as NAMES, so reordering LightType/UIElementKind
//     cannot reinterpret an existing saved scene as a different type;
//   - PrimitiveType::Empty must not come back as a Cube, which is exactly what
//     happens if a new enumerator is added without updating BOTH sides of the
//     name mapping in SceneSerializer.cpp.
void testLightCameraUiRoundTrip()
{
	const std::filesystem::path sceneFile = "test_light_camera_ui.scene";

	SceneEntity sun;
	sun.name = "Sun";
	sun.isLight = true;
	sun.light.type = LightType::Directional;
	sun.light.color = glm::vec3(1.0F, 0.5F, 0.25F);
	sun.light.intensity = 2.5F;
	sun.light.castShadows = true;
	sun.light.shadowBias = 0.0042F;

	SceneEntity spot;
	spot.name = "Spot";
	spot.isLight = true;
	spot.light.type = LightType::Spot;
	spot.light.range = 33.5F;
	spot.light.innerConeDegrees = 12.0F;
	spot.light.outerConeDegrees = 41.0F;
	spot.light.castShadows = false;

	SceneEntity camera;
	camera.name = "MainCam";
	camera.isCamera = true;
	camera.camera.fieldOfViewDegrees = 72.5F;
	camera.camera.nearClip = 0.25F;
	camera.camera.farClip = 900.0F;
	camera.camera.clearColor = glm::vec3(0.1F, 0.2F, 0.3F);
	camera.camera.isMainCamera = true;

	SceneEntity crosshair;
	crosshair.name = "Crosshair";
	crosshair.isUIElement = true;
	crosshair.parentName = "MainCam";
	crosshair.ui.kind = UIElementKind::Crosshair;
	crosshair.ui.anchor = UIAnchor::BottomRight;
	crosshair.ui.offsetPixels = glm::vec2(-18.0F, 24.0F);
	crosshair.ui.sizePixels = glm::vec2(11.0F, 13.0F);
	crosshair.ui.opacity = 0.5F;
	crosshair.ui.thicknessPixels = 3.0F;
	crosshair.ui.gapPixels = 7.0F;

	SceneEntity hudText;
	hudText.name = "Ammo";
	hudText.isUIElement = true;
	hudText.ui.kind = UIElementKind::Text;
	hudText.ui.text = "Ammo: 42";
	hudText.ui.fontSizePixels = 27.0F;

	SceneEntity empty;
	empty.name = "Group";
	empty.primitive = PrimitiveType::Empty;

	const SceneSaveResult saved =
		saveScene(sceneFile, {sun, spot, camera, crosshair, hudText, empty});
	TEST_ASSERT(saved.success, "Saving a scene with lights/camera/UI must succeed");

	const SceneLoadResult loaded = loadScene(sceneFile);
	TEST_ASSERT(loaded.success, "Loading a scene with lights/camera/UI must succeed");
	TEST_ASSERT(loaded.entities.size() == 6, "All six entities must round-trip");

	const SceneEntity& loadedSun = loaded.entities[0];
	TEST_ASSERT(loadedSun.isLight, "Sun must still be a light");
	TEST_ASSERT(loadedSun.light.type == LightType::Directional, "Sun light type");
	TEST_ASSERT(std::abs(loadedSun.light.intensity - 2.5F) < 0.001F, "Sun intensity");
	TEST_ASSERT(std::abs(loadedSun.light.color.g - 0.5F) < 0.001F, "Sun color");
	TEST_ASSERT(loadedSun.light.castShadows, "Sun cast shadows flag");
	TEST_ASSERT(std::abs(loadedSun.light.shadowBias - 0.0042F) < 0.0001F, "Sun shadow bias");

	const SceneEntity& loadedSpot = loaded.entities[1];
	TEST_ASSERT(loadedSpot.light.type == LightType::Spot, "Spot light type");
	TEST_ASSERT(std::abs(loadedSpot.light.range - 33.5F) < 0.001F, "Spot range");
	TEST_ASSERT(std::abs(loadedSpot.light.innerConeDegrees - 12.0F) < 0.001F, "Spot inner cone");
	TEST_ASSERT(std::abs(loadedSpot.light.outerConeDegrees - 41.0F) < 0.001F, "Spot outer cone");
	TEST_ASSERT(!loadedSpot.light.castShadows, "Spot cast shadows must stay off");

	const SceneEntity& loadedCamera = loaded.entities[2];
	TEST_ASSERT(loadedCamera.isCamera, "Camera flag");
	TEST_ASSERT(std::abs(loadedCamera.camera.fieldOfViewDegrees - 72.5F) < 0.001F, "Camera FOV");
	TEST_ASSERT(std::abs(loadedCamera.camera.nearClip - 0.25F) < 0.001F, "Camera near clip");
	TEST_ASSERT(std::abs(loadedCamera.camera.farClip - 900.0F) < 0.01F, "Camera far clip");
	TEST_ASSERT(loadedCamera.camera.isMainCamera, "Main camera flag");

	const SceneEntity& loadedCrosshair = loaded.entities[3];
	TEST_ASSERT(loadedCrosshair.isUIElement, "Crosshair UI flag");
	TEST_ASSERT(loadedCrosshair.ui.kind == UIElementKind::Crosshair, "Crosshair kind");
	TEST_ASSERT(loadedCrosshair.ui.anchor == UIAnchor::BottomRight, "Crosshair anchor");
	TEST_ASSERT(loadedCrosshair.parentName == "MainCam", "Crosshair parent must survive");
	TEST_ASSERT(std::abs(loadedCrosshair.ui.offsetPixels.x + 18.0F) < 0.001F, "Crosshair offset x");
	TEST_ASSERT(std::abs(loadedCrosshair.ui.sizePixels.y - 13.0F) < 0.001F, "Crosshair size y");
	TEST_ASSERT(std::abs(loadedCrosshair.ui.gapPixels - 7.0F) < 0.001F, "Crosshair gap");

	const SceneEntity& loadedText = loaded.entities[4];
	TEST_ASSERT(loadedText.ui.kind == UIElementKind::Text, "HUD text kind");
	TEST_ASSERT(loadedText.ui.text == "Ammo: 42", "HUD text content");
	TEST_ASSERT(std::abs(loadedText.ui.fontSizePixels - 27.0F) < 0.001F, "HUD text font size");

	// The regression this guards: a new PrimitiveType enumerator that the
	// serializer's name mapping does not know about falls through to "cube",
	// so an Empty silently becomes a solid Cube on reload.
	TEST_ASSERT(
		loaded.entities[5].primitive == PrimitiveType::Empty,
		"PrimitiveType::Empty must not round-trip as a Cube");

	// Entities that are none of these must not gain the flags by accident -
	// the writer emits the light/camera/ui blocks unconditionally, so a bad
	// default in the reader would turn every object in every scene into a light.
	TEST_ASSERT(!loaded.entities[5].isLight, "A plain entity must not become a light");
	TEST_ASSERT(!loaded.entities[5].isCamera, "A plain entity must not become a camera");
	TEST_ASSERT(!loaded.entities[5].isUIElement, "A plain entity must not become a UI element");

	std::filesystem::remove(sceneFile);
	std::filesystem::remove(sceneFile.string() + ".bak");
}

// Lights and cameras are aimed with the ordinary Rotate gizmo, so
// entityForward() IS the light direction - if it disagrees with the project's
// local-+Z forward convention, every light in every scene points the wrong way.
void testEntityForwardMatchesForwardConvention()
{
	SceneEntity unrotated;
	const glm::vec3 forward = entityForward(unrotated);
	TEST_ASSERT(std::abs(forward.z - 1.0F) < 0.001F, "Unrotated forward must be local +Z");
	TEST_ASSERT(std::abs(forward.x) < 0.001F, "Unrotated forward must have no X");

	// Yaw 90 degrees: +Z swings to +X, matching yawPitchForward's own
	// sin(yaw)/cos(yaw) convention that fps_controller.lua depends on.
	SceneEntity yawed;
	yawed.rotationEuler = glm::vec3(0.0F, 90.0F, 0.0F);
	const glm::vec3 yawedForward = entityForward(yawed);
	TEST_ASSERT(std::abs(yawedForward.x - 1.0F) < 0.001F, "Yaw 90 must point along +X");

	// Pitch must be honoured - this is the whole reason entityForward exists
	// separately from the script API's deliberately yaw-only getForward().
	SceneEntity pitched;
	pitched.rotationEuler = glm::vec3(-90.0F, 0.0F, 0.0F);
	const glm::vec3 pitchedForward = entityForward(pitched);
	TEST_ASSERT(std::abs(pitchedForward.y - 1.0F) < 0.001F, "Pitch -90 must point along +Y");

	// Scale must not leak into the direction: a light stretched on Z still
	// points the same way, and a zero scale must not produce a zero vector.
	SceneEntity scaled;
	scaled.scale = glm::vec3(1.0F, 1.0F, 7.0F);
	const glm::vec3 scaledForward = entityForward(scaled);
	TEST_ASSERT(std::abs(glm::length(scaledForward) - 1.0F) < 0.001F, "Forward must be normalized");

	SceneEntity zeroScaled;
	zeroScaled.scale = glm::vec3(0.0F);
	const glm::vec3 zeroForward = entityForward(zeroScaled);
	TEST_ASSERT(glm::length(zeroForward) > 0.5F, "Zero scale must still yield a usable direction");
}

// THE TEST THAT MATTERS MOST for Phase 2: does the generated Lua actually load
// and run in the real ScriptRuntime?
//
// Everything else here checks that the compiler emits the text we intended.
// This checks the only thing the player experiences - that the text is a valid
// script the engine accepts. Generated code that does not parse is worthless
// however well-formed it looks in a diff, and a syntax error would otherwise
// surface at Play time, long after the edit that caused it.
void testMindGraphGeneratedLuaRuns()
{
	using namespace gameforger::editor::mindgraph;
	namespace fs = std::filesystem;

	MindGraph graph;
	graph.name = "GeneratedProbe";

	// Deliberately exercises every emitter, not just the easy ones: an event,
	// an audio call, a world call, a branch (which nests), a delay (which
	// emits a closure), and a zone event (which emits a guarded block).
	GraphNode start{};
	start.id = 1;
	start.type = "event.game_start";
	GraphNode play{};
	play.id = 2;
	play.type = "audio.play";
	play.literals["clip"] = "Game/Audio/alarm.wav";
	play.literals["loop"] = "true";
	GraphNode branch{};
	branch.id = 3;
	branch.type = "flow.branch";
	GraphNode hide{};
	hide.id = 4;
	hide.type = "world.set_active";
	hide.literals["target"] = "Vault Door";
	hide.literals["active"] = "false";
	GraphNode delay{};
	delay.id = 5;
	delay.type = "flow.delay";
	delay.literals["seconds"] = "0.5";
	GraphNode light{};
	light.id = 6;
	light.type = "world.set_light";
	light.literals["target"] = "Alarm Light";
	light.literals["intensity"] = "4.0";
	GraphNode zone{};
	zone.id = 7;
	zone.type = "event.zone_enter";
	zone.literals["zone"] = "Vault";
	zone.literals["watch_tag"] = "Player";
	GraphNode update{};
	update.id = 8;
	update.type = "event.update";

	graph.nodes = {start, play, branch, hide, delay, light, zone, update};
	graph.links = {
		GraphLink{1, 1, "exec_out", 2, "exec_in"},
		GraphLink{2, 2, "exec_out", 3, "exec_in"},
		GraphLink{3, 3, "exec_true", 4, "exec_in"},
		GraphLink{4, 3, "exec_false", 5, "exec_in"},
		GraphLink{5, 5, "exec_out", 6, "exec_in"},
		GraphLink{6, 7, "exec_out", 6, "exec_in"},
	};

	const CompileResult compiled = compileGraph(graph);
	TEST_ASSERT(compiled.success, "The probe graph must compile");

	// Written under Game/Scripts because startScript confines paths there and
	// rejects anything outside. The "test_" prefix is what
	// testAllShippedScriptsLoad skips, so this probe cannot be mistaken for
	// shipped content by that test or by a reader browsing the folder.
	fs::path scriptsDir;
	{
		std::error_code walkError;
		fs::path candidate = fs::current_path(walkError);
		for (int depth = 0; depth < 6 && !candidate.empty(); ++depth)
		{
			const fs::path guess = candidate / "Game" / "Scripts";
			std::error_code ec;
			if (fs::is_directory(guess, ec))
			{
				scriptsDir = guess;
				break;
			}
			if (!candidate.has_parent_path() || candidate.parent_path() == candidate)
			{
				break;
			}
			candidate = candidate.parent_path();
		}
	}
	if (scriptsDir.empty())
	{
		std::error_code ec;
		scriptsDir = fs::current_path(ec) / "Game" / "Scripts";
		fs::create_directories(scriptsDir, ec);
	}

	std::cerr << "----- generated Lua -----\n" << compiled.lua << "-------------------------\n";
	const fs::path probePath = scriptsDir / "test_generated_graph.lua";
	{
		std::ofstream out(probePath, std::ios::binary);
		TEST_ASSERT(out.good(), "Must be able to write the probe script");
		out << compiled.lua;
	}

	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
	MockInputSource input;
	ScriptRuntime runtime;
	// A log callback, so a Lua error names itself instead of the test just
	// reporting "did not start".
	ScriptRuntime::Config probeConfig;
	probeConfig.logCallback = [](bool, const std::string& message)
	{ std::cerr << "       lua: " << message << "\n"; };
	runtime.initialize(scene, bus, input, std::move(probeConfig));

	const fs::path projectRoot = scriptsDir.parent_path().parent_path();
	std::cerr << "       probe at: " << probePath.string() << "\n";
	std::cerr << "       projectRoot: " << projectRoot.string() << "\n";
	std::cerr << "       exists: " << (fs::exists(probePath) ? "yes" : "no") << "\n";
	const bool started = runtime.startScript(1, "Game/Scripts/test_generated_graph.lua", projectRoot);
	TEST_ASSERT(started, "Generated Lua must load and run on_start in the real ScriptRuntime");

	// Several frames, because the Delay node's continuation only fires after
	// its timer elapses - a closure that throws would otherwise go unnoticed.
	for (int frame = 0; frame < 60; ++frame)
	{
		runtime.updateEntity(1, 0.016F);
	}
	runtime.shutdown();

	std::error_code removeError;
	fs::remove(probePath, removeError);
}

// The catalog is the published contract every saved graph names by string.
// A duplicate or empty id would make two node types indistinguishable to the
// loader; a duplicate pin id inside one type would make two pins
// indistinguishable to every link.
void testMindGraphCatalog()
{
	using namespace gameforger::editor::mindgraph;

	const std::vector<NodeType>& catalog = nodeCatalog();
	TEST_ASSERT(!catalog.empty(), "Catalog must not be empty");

	std::vector<std::string> seenTypes;
	for (const NodeType& type : catalog)
	{
		TEST_ASSERT(!type.id.empty(), "Every node type needs a stable id");
		TEST_ASSERT(!type.displayName.empty(), "Every node type needs a display name");
		TEST_ASSERT(!type.summary.empty(), "Every node type needs a one-line summary for the palette");
		TEST_ASSERT(
			std::find(seenTypes.begin(), seenTypes.end(), type.id) == seenTypes.end(),
			"Node type ids must be unique");
		seenTypes.push_back(type.id);

		std::vector<std::string> seenPins;
		for (const std::vector<PinSpec>* list : {&type.inputs, &type.outputs, &type.literals})
		{
			for (const PinSpec& pin : *list)
			{
				TEST_ASSERT(!pin.id.empty(), "Every pin needs a stable id");
				TEST_ASSERT(
					std::find(seenPins.begin(), seenPins.end(), pin.id) == seenPins.end(),
					"Pin ids must be unique within a node type");
				seenPins.push_back(pin.id);
			}
		}
		// An Event starts a chain, so it must offer somewhere to go next.
		if (type.category == NodeCategory::Event)
		{
			TEST_ASSERT(
				findPin(type, "exec_out") != nullptr,
				"Every Event node must have an exec_out to continue from");
		}
	}

	TEST_ASSERT(findNodeType("event.game_start") != nullptr, "The Game Start event must exist");
	TEST_ASSERT(findNodeType("no.such.type") == nullptr, "An unknown type must not resolve");
	TEST_ASSERT(
		findPin(*findNodeType("audio.play"), "clip") != nullptr, "Play Audio must expose a clip literal");
	TEST_ASSERT(findPin(*findNodeType("audio.play"), "nope") == nullptr, "An unknown pin must not resolve");
}

// The compiler produces the script the game actually runs, so what it emits is
// the whole feature. Checked by content rather than exact text: asserting a
// byte-for-byte golden would break on every harmless formatting change.
void testMindGraphCompiler()
{
	using namespace gameforger::editor::mindgraph;

	MindGraph graph;
	graph.name = "VaultAlarm";
	GraphNode start{};
	start.id = 1;
	start.type = "event.game_start";
	GraphNode play{};
	play.id = 2;
	play.type = "audio.play";
	play.literals["clip"] = "Game/Audio/alarm.wav";
	play.literals["loop"] = "true";
	GraphNode hide{};
	hide.id = 3;
	hide.type = "world.set_active";
	hide.literals["target"] = "Vault Door";
	hide.literals["active"] = "false";
	graph.nodes = {start, play, hide};
	graph.links = {
		GraphLink{1, 1, "exec_out", 2, "exec_in"},
		GraphLink{2, 2, "exec_out", 3, "exec_in"},
	};

	const CompileResult compiled = compileGraph(graph);
	TEST_ASSERT(compiled.success, "A well-formed graph must compile without errors");
	TEST_ASSERT(
		compiled.lua.find("return Graph") != std::string::npos,
		"Must emit this project's script contract");
	TEST_ASSERT(
		compiled.lua.find("function Graph:on_start()") != std::string::npos, "Must emit on_start");
	TEST_ASSERT(
		compiled.lua.find("function Graph:on_update(dt)") != std::string::npos, "Must emit on_update");
	TEST_ASSERT(
		compiled.lua.find("DO NOT EDIT BY HAND") != std::string::npos,
		"Generated Lua must say it is generated - it is overwritten on the next compile");

	// The chain must be emitted in execution order: the audio call before the
	// hide call, because that is the order the wires say.
	const std::size_t playAt = compiled.lua.find("Game/Audio/alarm.wav");
	const std::size_t hideAt = compiled.lua.find("Vault Door");
	TEST_ASSERT(playAt != std::string::npos, "Play Audio must emit its clip literal");
	TEST_ASSERT(hideAt != std::string::npos, "Show/Hide must emit its target");
	TEST_ASSERT(playAt < hideAt, "Nodes must be emitted in execution order, not graph order");
	TEST_ASSERT(
		compiled.lua.find("setEntityActive") != std::string::npos,
		"Show/Hide must call the world binding that actually exists");

	// A cycle must be reported, not hang the compiler.
	MindGraph looped = graph;
	looped.links.push_back(GraphLink{3, 3, "exec_out", 2, "exec_in"});
	const CompileResult loopResult = compileGraph(looped);
	TEST_ASSERT(!loopResult.success, "A cycle in the execution chain must be an error");
	TEST_ASSERT(!loopResult.diagnostics.empty(), "A cycle must produce a diagnostic naming the node");

	// A link to a deleted node is an error the user can act on, not a crash.
	MindGraph dangling = graph;
	dangling.nodes.pop_back();
	const CompileResult danglingResult = compileGraph(dangling);
	TEST_ASSERT(!danglingResult.success, "A link to a missing node must be an error");

	// An unknown node type is a WARNING, not an error: a graph from a newer
	// editor must still compile what it understands, and must keep the rest.
	MindGraph future = graph;
	GraphNode alien{};
	alien.id = 9;
	alien.type = "future.node.from.a.newer.editor";
	future.nodes.push_back(alien);
	const CompileResult futureResult = compileGraph(future);
	TEST_ASSERT(futureResult.success, "An unknown node type must not fail the whole compile");
	TEST_ASSERT(!futureResult.diagnostics.empty(), "An unknown node type must still be reported");

	// Malformed literals must not produce Lua that fails to load - that would
	// surface as a runtime error long after the edit that caused it.
	MindGraph junk;
	junk.name = "Junk";
	GraphNode junkStart{};
	junkStart.id = 1;
	junkStart.type = "event.game_start";
	GraphNode junkDelay{};
	junkDelay.id = 2;
	junkDelay.type = "flow.delay";
	junkDelay.literals["seconds"] = "not a number";
	junk.nodes = {junkStart, junkDelay};
	junk.links = {GraphLink{1, 1, "exec_out", 2, "exec_in"}};
	const CompileResult junkResult = compileGraph(junk);
	TEST_ASSERT(junkResult.success, "A malformed number literal must fall back, not fail the compile");
	TEST_ASSERT(
		junkResult.lua.find("not a number") == std::string::npos,
		"A malformed number must never reach the generated Lua");
}

// SECTION 15. Breadcrumb wrapping must be semantics-preserving: with it off
// and on, the generated Lua must differ ONLY by wrapper lines. If it changes
// anything else, live node highlighting is altering how the game behaves.
void testMindGraphBreadcrumbWrapper()
{
	using namespace gameforger::editor::mindgraph;

	MindGraph graph;
	graph.name = "Wrapped";
	GraphNode a{};
	a.id = 1;
	a.type = "event.game_start";
	GraphNode b{};
	b.id = 2;
	b.type = "audio.play";
	b.literals["clip"] = "a.wav";
	GraphNode c{};
	c.id = 3;
	c.type = "flow.branch";
	GraphNode d{};
	d.id = 4;
	d.type = "world.set_active";
	d.literals["target"] = "Door";
	graph.nodes = {a, b, c, d};
	graph.links = {
		GraphLink{1, 1, "exec_out", 2, "exec_in"},
		GraphLink{2, 2, "exec_out", 3, "exec_in"},
		GraphLink{3, 3, "exec_true", 4, "exec_in"},
	};

	const CompileResult without = compileGraph(graph, false);
	const CompileResult with = compileGraph(graph, true);
	TEST_ASSERT(without.success && with.success, "Both variants must compile");
	TEST_ASSERT(
		without.lua.find("__gfNode(") == std::string::npos, "Breadcrumbs off must emit no breadcrumbs");
	TEST_ASSERT(with.lua.find("__gfNode(") != std::string::npos, "Breadcrumbs on must emit them");

	// Strip the wrapper lines from the wrapped output; what remains must be
	// identical to the unwrapped output, line for line.
	const auto stripBreadcrumbs = [](const std::string& text)
	{
		std::string out;
		std::size_t start = 0;
		while (start <= text.size())
		{
			const std::size_t end = text.find('\n', start);
			const std::string line =
				text.substr(start, end == std::string::npos ? std::string::npos : end - start);
			// Any line MENTIONING the breadcrumb - including the shim that
			// defines it - is a wrapper line. Matching on "__gfNode(" alone
			// missed the shim, whose call form is rawget(_G, "__gfNode").
			if (line.find("__gfNode") == std::string::npos)
			{
				out += line;
				out += '\n';
			}
			if (end == std::string::npos)
			{
				break;
			}
			start = end + 1;
		}
		return out;
	};
	TEST_ASSERT(
		stripBreadcrumbs(with.lua) == stripBreadcrumbs(without.lua),
		"Breadcrumb wrapping must change NOTHING except its own lines");

	// One breadcrumb per node actually reached by the execution chain.
	std::size_t count = 0;
	for (std::size_t at = with.lua.find("__gfNode("); at != std::string::npos;
		 at = with.lua.find("__gfNode(", at + 1))
	{
		++count;
	}
	TEST_ASSERT(count == 3, "Every executed node must announce itself exactly once");
}

// Mind Graph round-trip. The file is the authoring format, so anything that
// does not survive save/load is work the user loses.
void testMindGraphRoundTrip()
{
	using namespace gameforger::editor::mindgraph;

	MindGraph graph;
	graph.name = "VaultAlarm";

	GraphNode start;
	start.id = 1;
	start.type = "event.game_start";
	start.canvasPosition = glm::vec2(-120.5F, 40.25F);

	GraphNode audio;
	audio.id = 2;
	audio.type = "audio.play";
	audio.canvasPosition = glm::vec2(260.0F, 40.25F);
	audio.literals["clip"] = "Game/Audio/alarm.wav";
	audio.literals["target"] = "Vault Speaker";
	// Quotes and backslashes are exactly what a Windows path and a bit of
	// prose will contain, and exactly what a hand-rolled writer gets wrong.
	audio.literals["note"] = "say \"go\" \ loudly";

	graph.nodes = {start, audio};
	graph.links = {GraphLink{10, 1, "exec_out", 2, "exec_in"}};
	graph.nextNodeId = 3;
	graph.nextLinkId = 11;

	const std::string text = serializeGraph(graph);
	const GraphLoadResult loaded = deserializeGraph(text);
	TEST_ASSERT(loaded.success, "A serialized graph must deserialize");
	TEST_ASSERT(loaded.graph.name == "VaultAlarm", "Graph name must survive");
	TEST_ASSERT(loaded.graph.nodes.size() == 2, "Both nodes must survive");
	TEST_ASSERT(loaded.graph.links.size() == 1, "The link must survive");

	const GraphNode* reloadedAudio = loaded.graph.findNode(2);
	TEST_ASSERT(reloadedAudio != nullptr, "Node 2 must be findable by id");
	TEST_ASSERT(reloadedAudio->type == "audio.play", "Node type must survive");
	TEST_ASSERT(
		std::abs(reloadedAudio->canvasPosition.x - 260.0F) < 0.01F, "Canvas position must survive");
	TEST_ASSERT(
		reloadedAudio->literals.at("clip") == "Game/Audio/alarm.wav", "Literal value must survive");
	TEST_ASSERT(
		reloadedAudio->literals.at("note") == "say \"go\" \ loudly",
		"Quotes and backslashes in a literal must survive escaping");

	const GraphLink& link = loaded.graph.links[0];
	TEST_ASSERT(link.fromNode == 1 && link.toNode == 2, "Link endpoints must survive");
	TEST_ASSERT(link.fromPin == "exec_out" && link.toPin == "exec_in", "Pin ids must survive as strings");

	// Serializing the reloaded graph must produce identical text. If it does
	// not, something is being reordered or lost, and every save would show a
	// spurious diff.
	TEST_ASSERT(serializeGraph(loaded.graph) == text, "Round-trip must be byte-identical");

	// A file that is not ours must be refused rather than loaded as an empty
	// graph - loading it empty would destroy the real content on next save.
	TEST_ASSERT(!deserializeGraph("{\"format\":\"Something Else\"}").success, "Foreign format must be refused");
	TEST_ASSERT(!deserializeGraph("not json at all").success, "Malformed input must be refused");
	// A graph from a newer editor must be refused, not silently downgraded.
	TEST_ASSERT(
		!deserializeGraph("{\"format\":\"GameForgerMindGraph\",\"version\":999}").success,
		"A newer format version must be refused");
}

// THE PIN STABILITY TEST - MindGraph-Plan section 13, the load-bearing
// invariant everything else stands on.
//
// Links are stored as (nodeId, pinId STRING). If they were stored as integer
// pin indices assigned at load, adding a pin to a node type would shift every
// later index and silently rebind links to the wrong pins. This asserts that
// mutating a node's pin usage cannot disturb links that were not touched.
void testMindGraphPinStability()
{
	using namespace gameforger::editor::mindgraph;

	MindGraph graph;
	graph.name = "PinStability";
	for (int id = 1; id <= 3; ++id)
	{
		GraphNode node;
		node.id = id;
		node.type = "test.node";
		graph.nodes.push_back(node);
	}
	graph.links = {
		GraphLink{1, 1, "exec_out", 2, "exec_in"},
		GraphLink{2, 2, "value_out", 3, "amount_in"},
	};
	graph.nextNodeId = 4;
	graph.nextLinkId = 3;

	const GraphLoadResult first = deserializeGraph(serializeGraph(graph));
	TEST_ASSERT(first.success, "Baseline graph must load");

	// Simulate the node type gaining a pin and having its pins reordered.
	// Under an index-based scheme this is precisely the change that would
	// corrupt the links; under name-based storage the file does not even
	// mention pin order, so nothing can shift.
	MindGraph mutated = first.graph;
	mutated.nodes[0].literals["newly_added_pin"] = "42";
	mutated.nodes[1].literals["another_pin"] = "hello";

	const GraphLoadResult second = deserializeGraph(serializeGraph(mutated));
	TEST_ASSERT(second.success, "Mutated graph must load");
	TEST_ASSERT(second.graph.links.size() == 2, "Both links must survive a pin-list change");
	TEST_ASSERT(
		second.graph.links[0].fromPin == "exec_out" && second.graph.links[0].toPin == "exec_in",
		"Link 1 must still resolve to the same logical endpoints");
	TEST_ASSERT(
		second.graph.links[1].fromPin == "value_out" && second.graph.links[1].toPin == "amount_in",
		"Link 2 must still resolve to the same logical endpoints");

	// A link to a node that no longer exists is REPORTED, never silently
	// dropped - dropping it would destroy authored work on the first load
	// after an engine update.
	MindGraph orphaned = second.graph;
	orphaned.nodes.erase(orphaned.nodes.begin() + 2);
	const GraphLoadResult third = deserializeGraph(serializeGraph(orphaned));
	TEST_ASSERT(third.success, "A graph with a dangling link must still load");
	TEST_ASSERT(third.graph.links.size() == 2, "The dangling link must be preserved, not dropped");
	TEST_ASSERT(third.graph.brokenLinks().size() == 1, "The dangling link must be reported as broken");

	// Ids are recomputed from content, so a hand-edited file cannot hand out
	// an id already in use - which would make two nodes indistinguishable.
	const GraphLoadResult liar = deserializeGraph(
		"{\"format\":\"GameForgerMindGraph\",\"version\":1,\"nextNodeId\":1,\"nextLinkId\":1,"
		"\"nodes\":[{\"id\":7,\"type\":\"test.node\"}],\"links\":[]}");
	TEST_ASSERT(liar.success, "Graph with an understated nextNodeId must load");
	TEST_ASSERT(liar.graph.nextNodeId > 7, "nextNodeId must be recomputed past the highest used id");
}

// THE PARITY TEST. MissingFunctions.md section 1b records four defects of one
// shape: a ScriptRuntime callback the Editor implements for real and the
// Runtime stubs out, so a game works on Play and is silently inert once
// shipped. Both hosts compiled, both passed tests, and nothing reported it.
//
// bindSharedScriptCallbacks is the structural fix - one binding site both hosts
// call, so a callback bound there is bound in both by construction. This test
// is the guard on that: it asserts every callback the shared binder is
// responsible for is actually bound and actually reaches GameplayState.
//
// If someone adds a callback to ScriptRuntime::Config and forgets the binder,
// the "every field is non-null" assertion below fails. That is the whole point.
void testSharedScriptCallbackParity()
{
	GameplayState gameplay;
	gameforger::core::AudioEngine audio; // not initialize()d - no device needed for binding
	ScriptRuntime::Config config;
	bindSharedScriptCallbacks(config, gameplay, audio, std::filesystem::path("."));

	// Every callback the shared binder owns must be bound. logCallback is
	// deliberately excluded: it genuinely differs per host (Editor Console vs
	// stderr) and is the only one each host still binds itself.
	TEST_ASSERT(config.projectileSpawnCallback != nullptr, "projectileSpawnCallback must be bound");
	TEST_ASSERT(config.gravityProjectileSpawnCallback != nullptr, "gravityProjectileSpawnCallback must be bound");
	TEST_ASSERT(config.heldItemQueryCallback != nullptr, "heldItemQueryCallback must be bound");
	TEST_ASSERT(config.aimingCatapultQueryCallback != nullptr, "aimingCatapultQueryCallback must be bound");
	TEST_ASSERT(config.operatingCatapultSetCallback != nullptr, "operatingCatapultSetCallback must be bound");
	TEST_ASSERT(config.cursorLockSetCallback != nullptr, "cursorLockSetCallback must be bound");
	TEST_ASSERT(config.audioCommandCallback != nullptr, "audioCommandCallback must be bound");
	TEST_ASSERT(config.audioQueryCallback != nullptr, "audioQueryCallback must be bound");

	// Bound is not enough - a stub returning false is also "bound". Each one
	// must actually read and write the shared GameplayState. These are exactly
	// the three 1b defects, asserted directly.
	TEST_ASSERT(!config.heldItemQueryCallback(), "No held item yet");
	gameplay.heldItemEntityName = "Crate";
	TEST_ASSERT(config.heldItemQueryCallback(), "heldItemQueryCallback must read GameplayState, not return a constant");

	TEST_ASSERT(!config.aimingCatapultQueryCallback(), "Not aiming yet");
	config.operatingCatapultSetCallback(true);
	TEST_ASSERT(gameplay.playerOperatingCatapult, "operatingCatapultSetCallback must write GameplayState");
	TEST_ASSERT(config.aimingCatapultQueryCallback(), "aimingCatapultQueryCallback must read GameplayState");
	config.operatingCatapultSetCallback(false);
	TEST_ASSERT(!gameplay.playerOperatingCatapult, "operatingCatapultSetCallback must clear too");

	config.cursorLockSetCallback(true);
	TEST_ASSERT(gameplay.cursorLockDesired, "cursorLockSetCallback must write GameplayState");

	// Straight-line projectile: velocity points from->to, no gravity.
	config.projectileSpawnCallback(glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 10.0F), 5.0F, "Enemy");
	TEST_ASSERT(gameplay.projectiles.size() == 1, "projectileSpawnCallback must push a projectile");
	TEST_ASSERT(!gameplay.projectiles[0].useGravity, "Straight projectile must not use gravity");
	TEST_ASSERT(std::abs(gameplay.projectiles[0].velocity.z - 5.0F) < 0.001F, "Velocity must be normalized then scaled");
	TEST_ASSERT(gameplay.projectiles[0].hitTag == "Enemy", "hitTag must carry through");
	TEST_ASSERT(gameplay.projectilesFiredThisTick == 1, "Firing must bump the per-tick counter");

	// Arced projectile: gravity on, and a longer lifetime because an arc
	// spends more time in the air than a straight shot.
	config.gravityProjectileSpawnCallback(glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F), 20.0F, "Castle");
	TEST_ASSERT(gameplay.projectiles.size() == 2, "gravityProjectileSpawnCallback must push a projectile");
	TEST_ASSERT(gameplay.projectiles[1].useGravity, "Gravity projectile must set useGravity");
	TEST_ASSERT(
		gameplay.projectiles[1].remainingLifetimeSeconds > gameplay.projectiles[0].remainingLifetimeSeconds,
		"An arced shot must outlive a straight one or it despawns mid-flight");
	TEST_ASSERT(gameplay.projectilesFiredThisTick == 2, "Both spawn paths must bump the counter");

	// beginGameplayFrame resets the per-tick counter. The Editor did this and
	// the Runtime did not, so in a shipped game it accumulated forever.
	beginGameplayFrame(gameplay);
	TEST_ASSERT(gameplay.projectilesFiredThisTick == 0, "beginGameplayFrame must reset projectilesFiredThisTick");
	TEST_ASSERT(gameplay.projectiles.size() == 2, "beginGameplayFrame must NOT discard live projectiles");
}

// applyCameraPoseToEntity is what makes a weapon parented to the camera into a
// viewmodel: the entity must end up AT the eye, FACING the way the player
// looks. Get the yaw flip wrong and the gun points behind the player; get the
// pitch sign wrong and it swings the wrong way when they look up.
void testCameraPoseDrivesEntityForward()
{
	const auto check = [](const glm::vec3& eye, const glm::vec3& aim, const char* what)
	{
		const GameCameraState pose = cameraLookingAt(eye, aim);

		const glm::vec3 resolvedEye = gameCameraEye(pose);
		TEST_ASSERT(glm::length(resolvedEye - eye) < 0.01F, "gameCameraEye must round-trip the eye");

		SceneEntity cameraEntity;
		applyCameraPoseToEntity(cameraEntity, pose);
		TEST_ASSERT(glm::length(cameraEntity.position - eye) < 0.01F, "Camera entity must sit at the eye");

		const glm::vec3 wantForward = glm::normalize(aim - eye);
		const glm::vec3 gotForward = entityForward(cameraEntity);
		TEST_ASSERT(glm::length(gotForward - wantForward) < 0.01F, what);
	};

	check(glm::vec3(0.0F, 2.0F, 0.0F), glm::vec3(0.0F, 2.0F, 10.0F), "Looking along +Z");
	check(glm::vec3(0.0F, 2.0F, 0.0F), glm::vec3(10.0F, 2.0F, 0.0F), "Looking along +X");
	check(glm::vec3(0.0F, 2.0F, 0.0F), glm::vec3(0.0F, 2.0F, -10.0F), "Looking along -Z");
	check(glm::vec3(0.0F, 2.0F, 0.0F), glm::vec3(-10.0F, 2.0F, 0.0F), "Looking along -X");
	// Looking up and down is where a wrong pitch sign shows itself.
	check(glm::vec3(0.0F, 2.0F, 0.0F), glm::vec3(0.0F, 9.0F, 5.0F), "Looking up and forward");
	check(glm::vec3(0.0F, 5.0F, 0.0F), glm::vec3(3.0F, 0.0F, 3.0F), "Looking down and diagonally");
	check(glm::vec3(-4.0F, 1.5F, 7.0F), glm::vec3(2.0F, 3.0F, -1.0F), "Arbitrary off-origin pose");
}

// isGizmoOnlyEntity gates the mesh pass, and the mesh pass indexes a
// 6-element array by PrimitiveType - so a false negative for Empty is an
// out-of-bounds read, not just a cosmetic bug.
void testGizmoOnlyEntityClassification()
{
	SceneEntity cube;
	TEST_ASSERT(!isGizmoOnlyEntity(cube), "A plain cube has mesh geometry");

	SceneEntity empty;
	empty.primitive = PrimitiveType::Empty;
	TEST_ASSERT(isGizmoOnlyEntity(empty), "Empty must be gizmo-only");

	SceneEntity light;
	light.isLight = true;
	TEST_ASSERT(isGizmoOnlyEntity(light), "A light must be gizmo-only");

	SceneEntity camera;
	camera.isCamera = true;
	TEST_ASSERT(isGizmoOnlyEntity(camera), "A camera must be gizmo-only");

	SceneEntity ui;
	ui.isUIElement = true;
	TEST_ASSERT(isGizmoOnlyEntity(ui), "A UI element must be gizmo-only");

	SceneEntity cine;
	cine.isCineCamera = true;
	TEST_ASSERT(isGizmoOnlyEntity(cine), "A cine camera must stay gizmo-only");

	// A terrain/text/imported mesh whose `primitive` field happens to still be
	// Empty DOES have geometry - its shape comes from elsewhere, and skipping
	// it would make the terrain vanish.
	SceneEntity terrain;
	terrain.primitive = PrimitiveType::Empty;
	terrain.isTerrain = true;
	TEST_ASSERT(!isGizmoOnlyEntity(terrain), "Terrain draws even if primitive is Empty");
}

void testColliderBoxMeshConvexTypes()
{
	EditorScene scene(".");
	TEST_ASSERT(scene.execute(CreateEntityCommand{"Pyramid", PrimitiveType::Cube, glm::vec3(0.0F)}).success,
		"create pyramid");
	const SceneEntity* found = scene.findEntity("Pyramid");
	TEST_ASSERT(found != nullptr, "pyramid exists");
	SceneEntity* pyramid = scene.findEntityMutable(found->id);
	pyramid->isImportedMesh = true;
	pyramid->hasCollider = true;
	pyramid->scale = glm::vec3(1.0F);

	MeshCollisionGeometry geom;
	geom.triangleVertices = {
		{-2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, 2.0F},
		{-2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, 2.0F}, {-2.0F, 0.0F, 2.0F},
		{-2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, -2.0F}, {0.0F, 4.0F, 0.0F},
		{2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, 2.0F}, {0.0F, 4.0F, 0.0F},
		{2.0F, 0.0F, 2.0F}, {-2.0F, 0.0F, 2.0F}, {0.0F, 4.0F, 0.0F},
		{-2.0F, 0.0F, 2.0F}, {-2.0F, 0.0F, -2.0F}, {0.0F, 4.0F, 0.0F},
	};
	geom.localMin = {-2.0F, 0.0F, -2.0F};
	geom.localMax = {2.0F, 4.0F, 2.0F};
	geom.valid = true;
	const ImportedMeshProvider provider = [&](const SceneEntity&) { return &geom; };

	TEST_ASSERT(scene.execute(SetPropertyCommand{"Pyramid", "Collider", "type", std::string("box")}).success,
		"set Box");
	const BoxCollisionResult boxInside =
		resolveBoxCollision(scene, 999, glm::vec3(0.0F, 1.5F, 0.0F), 0.3F, 1.0F, provider);
	TEST_ASSERT(boxInside.position.y > 3.9F, "Box collider is a solid AABB - inside the bounds is pushed to the roof");

	TEST_ASSERT(scene.execute(SetPropertyCommand{"Pyramid", "Collider", "type", std::string("mesh")}).success,
		"set Mesh");
	const BoxCollisionResult meshInside =
		resolveBoxCollision(scene, 999, glm::vec3(0.0F, 1.5F, 0.0F), 0.3F, 1.0F, provider);
	TEST_ASSERT(std::abs(meshInside.position.y - 1.5F) < 0.05F,
		"Mesh collider is hollow - standing inside the pyramid does not hit a triangle");

	TEST_ASSERT(scene.execute(SetPropertyCommand{"Pyramid", "Collider", "type", std::string("convex")}).success,
		"set Convex");
	const BoxCollisionResult convexInside =
		resolveBoxCollision(scene, 999, glm::vec3(0.0F, 1.5F, 0.0F), 0.3F, 1.0F, provider);
	TEST_ASSERT(std::abs(convexInside.position.x) > 0.01F || std::abs(convexInside.position.y - 1.5F) > 0.05F,
		"Convex hull is solid - a point inside the pyramid must be pushed out");

	const BoxCollisionResult convexCorner =
		resolveBoxCollision(scene, 999, glm::vec3(1.8F, 3.5F, 1.8F), 0.1F, 0.2F, provider);
	TEST_ASSERT(std::abs(convexCorner.position.x - 1.8F) < 0.05F && std::abs(convexCorner.position.y - 3.5F) < 0.05F,
		"Convex does not fill the AABB corners the way Box does");
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Provider response parsing. The editor ships with Anthropic as the DEFAULT
// provider, and its Messages API returns a content[] block array rather than
// OpenAI's choices[0].message.content. Parsing only the OpenAI shape silently
// broke AI script/animation/command generation on a clean install, so both
// shapes are pinned here.
// ----------------------------------------------------------------------------
static void testChatResponseBothProtocols()
{
	// OpenAI-compatible.
	const std::optional<std::string> openai = extractChatMessageContent(
		R"({"choices":[{"message":{"role":"assistant","content":"hello from openai"}}]})");
	TEST_ASSERT(openai.has_value() && *openai == "hello from openai",
		"OpenAI choices[0].message.content must be extracted");

	// Anthropic Messages.
	const std::optional<std::string> anthropic = extractChatMessageContent(
		R"({"content":[{"type":"text","text":"hello from anthropic"}],"stop_reason":"end_turn"})");
	TEST_ASSERT(anthropic.has_value() && *anthropic == "hello from anthropic",
		"Anthropic content[] text block must be extracted");

	// Anthropic with a non-text block interleaved: text blocks concatenate,
	// everything else is skipped rather than derailing the parse.
	const std::optional<std::string> mixed = extractChatMessageContent(
		R"({"content":[{"type":"thinking","thinking":"ignore me"},)"
		R"({"type":"text","text":"part one "},{"type":"text","text":"part two"}]})");
	TEST_ASSERT(mixed.has_value() && *mixed == "part one part two",
		"Anthropic text blocks must concatenate and skip non-text blocks");

	// A body with neither shape must report absence, not an empty string.
	TEST_ASSERT(!extractChatMessageContent(R"({"unexpected":true})").has_value(),
		"Unrecognised response shape must yield nullopt");

	// Anthropic's error envelope must still surface through the shared path.
	const std::string err = extractErrorMessage(
		R"({"type":"error","error":{"type":"invalid_request_error","message":"max_tokens is required"}})");
	TEST_ASSERT(err == "max_tokens is required", "Anthropic error.message must be extracted");
}

// ----------------------------------------------------------------------------
// gameforger::core::resolveProjectFile is THE project-boundary check - every script, model,
// font and texture path authored into a scene goes through it (ScriptRuntime,
// ViewportRenderer, EditorScene). A silent regression here is a security
// regression: scene files are shareable, so a hostile one could otherwise name
// any path on disk. It had no coverage at all until this test.
// ----------------------------------------------------------------------------
static void testResolveProjectFileConfinement()
{
	namespace fs = std::filesystem;

	// A real directory tree, because resolveProjectFile calls weakly_canonical
	// and a purely fictional root would exercise different code paths.
	const fs::path root = fs::absolute("test_confinement_root");
	fs::create_directories(root / "Game" / "Scripts");
	fs::create_directories(root / "Game" / "ScriptsEvil");
	fs::create_directories(root / "Secrets");
	{
		std::ofstream(root / "Game" / "Scripts" / "ok.lua") << "-- ok\n";
		std::ofstream(root / "Game" / "ScriptsEvil" / "sneaky.lua") << "-- sneaky\n";
		std::ofstream(root / "Secrets" / "keys.lua") << "-- secret\n";
	}

	const std::vector<std::string> lua{".lua"};

	// Happy path.
	TEST_ASSERT(gameforger::core::resolveProjectFile(root, "Game/Scripts/ok.lua", "Game/Scripts", lua).has_value(),
		"A normal in-bounds script path must resolve");

	// Traversal out of the required subdirectory.
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, "Game/Scripts/../../Secrets/keys.lua", "Game/Scripts", lua).has_value(),
		"'..' traversal escaping the required directory must be rejected");
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, "Secrets/keys.lua", "Game/Scripts", lua).has_value(),
		"A path outside the required directory must be rejected");

	// Sibling directory sharing a string prefix. This is the case a naive
	// starts_with() boundary check would wrongly allow.
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, "Game/ScriptsEvil/sneaky.lua", "Game/Scripts", lua).has_value(),
		"A sibling directory with a matching string prefix must be rejected");

	// Absolute paths bypass the root entirely, so they are never acceptable.
	const fs::path absolute = root / "Game" / "Scripts" / "ok.lua";
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, absolute, "Game/Scripts", lua).has_value(),
		"An absolute path must be rejected even when it points somewhere legal");

	// Empty input.
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, "", "Game/Scripts", lua).has_value(),
		"An empty relative path must be rejected");

	// Extension allowlist, and its case-insensitivity.
	TEST_ASSERT(!gameforger::core::resolveProjectFile(root, "Game/Scripts/ok.lua", "Game/Scripts", {".glb"}).has_value(),
		"A disallowed extension must be rejected");
	{
		std::ofstream(root / "Game" / "Scripts" / "SHOUTY.LUA") << "-- ok\n";
	}
	TEST_ASSERT(gameforger::core::resolveProjectFile(root, "Game/Scripts/SHOUTY.LUA", "Game/Scripts", lua).has_value(),
		"Extension matching must be case-insensitive");

	// No allowlist means any extension is acceptable.
	TEST_ASSERT(gameforger::core::resolveProjectFile(root, "Game/Scripts/ok.lua", "Game/Scripts", {}).has_value(),
		"An empty allowedExtensions list must accept any extension");

	// A file that does not exist yet still resolves - confinement is a path
	// question, not an existence one, and callers report missing files
	// themselves. Pinned so nobody "fixes" this into an exists() check and
	// breaks save-to-new-path flows.
	TEST_ASSERT(gameforger::core::resolveProjectFile(root, "Game/Scripts/not_created.lua", "Game/Scripts", lua).has_value(),
		"A not-yet-existing in-bounds path must still resolve");

	std::error_code cleanup;
	fs::remove_all(root, cleanup);
}

// ----------------------------------------------------------------------------
// json::serializePretty must produce a tree equal to what it was given -
// Project.json/Settings.json are written through it, so a bug here corrupts
// project config rather than just looking untidy.
// ----------------------------------------------------------------------------
static void testJsonPrettyPrinter()
{
	const std::string source =
		R"({"a":1,"b":[1,2,{"c":"x \" y"}],"d":{"e":true,"f":null},"g":[],"h":{}})";
	const std::optional<json::Value> original = json::parse(source);
	TEST_ASSERT(original.has_value(), "Pretty-printer fixture must parse");

	const std::string pretty = json::serializePretty(*original);
	TEST_ASSERT(pretty.find('\n') != std::string::npos, "Pretty output must contain newlines");
	TEST_ASSERT(!pretty.empty() && pretty.back() == '\n', "Pretty output must end with a newline");
	// Empty containers stay inline rather than becoming "[\n]".
	TEST_ASSERT(pretty.find("[]") != std::string::npos, "Empty array must stay inline");
	TEST_ASSERT(pretty.find("{}") != std::string::npos, "Empty object must stay inline");

	const std::optional<json::Value> reparsed = json::parse(pretty);
	TEST_ASSERT(reparsed.has_value(), "Pretty output must re-parse");
	// Round-tripping through the compact serialiser is the equality check:
	// same tree => byte-identical compact form.
	TEST_ASSERT(json::serialize(*reparsed) == json::serialize(*original),
		"Pretty output must re-parse to an equal tree");

	// Compact serialize() must be untouched - the AI Cockpit's tool-argument
	// path depends on it staying single-line.
	TEST_ASSERT(json::serialize(*original).find('\n') == std::string::npos,
		"Compact serialize() must remain newline-free");
}

// ----------------------------------------------------------------------------
// ProjectSettings round-trip + the command bus's validation. The bus is the
// only writer to project config and the AI drives it, so the rejection cases
// matter more than the happy path.
// ----------------------------------------------------------------------------
static void testProjectSettingsAndBus()
{
	namespace fs = std::filesystem;
	const fs::path root = fs::absolute("test_project_settings_root");
	std::error_code cleanupBefore;
	fs::remove_all(root, cleanupBefore);
	fs::create_directories(root / "Game" / "Scenes");
	{
		std::ofstream(root / "Game" / "Scenes" / "Main.gfprod") << "{}";
	}

	// A Project.json with a UTF-8 BOM and NO bootSequence key - exactly the
	// shape this project ships today. Must load cleanly.
	{
		std::ofstream out(root / "Game" / "Project.json", std::ios::binary);
		out << "\xEF\xBB\xBF"
			<< R"({"format":"GameForgerProject","version":1,"name":"Demo",)"
			<< R"("startupScene":"Game/Scenes/Main.gfprod","assetDirectories":["Game/Audio"],)"
			<< R"("customKeySomeoneAddedByHand":42})";
	}
	{
		std::ofstream out(root / "Game" / "Settings.json");
		out << R"({"mouseSensitivity":0.25,"targetFps":144})";
	}

	ProjectSettings loaded;
	TEST_ASSERT(loadProjectSettings(root, loaded).success, "Loading project settings must succeed");
	TEST_ASSERT(loaded.name == "Demo", "name must load (past the BOM)");
	TEST_ASSERT(loaded.startupScene == "Game/Scenes/Main.gfprod", "startupScene must load");
	TEST_ASSERT(loaded.assetDirectories.size() == 1, "assetDirectories must load");
	TEST_ASSERT(loaded.bootSequence.empty(), "A file with no bootSequence key must load as empty");
	TEST_ASSERT(std::abs(loaded.mouseSensitivity - 0.25F) < 0.0001F, "mouseSensitivity must load");
	TEST_ASSERT(loaded.targetFps == 144, "targetFps must load");

	ProjectSettingsBus bus(root);
	TEST_ASSERT(bus.settings().name == "Demo", "Bus must load settings on construction");

	// --- rejections -------------------------------------------------------
	SetProjectSettingCommand unknown;
	unknown.key = "notARealKey";
	unknown.stringValue = "x";
	TEST_ASSERT(!bus.validate(unknown).success, "An unknown setting key must be rejected");

	SetProjectSettingCommand missingScene;
	missingScene.key = "startupScene";
	missingScene.stringValue = "Game/Scenes/DoesNotExist.gfprod";
	TEST_ASSERT(!bus.validate(missingScene).success,
		"A startupScene that does not exist on disk must be rejected");

	SetProjectSettingCommand badFps;
	badFps.key = "targetFps";
	badFps.numberValue = 5000.0;
	TEST_ASSERT(!bus.validate(badFps).success, "An out-of-range targetFps must be rejected");

	SetProjectSettingCommand nanSensitivity;
	nanSensitivity.key = "mouseSensitivity";
	nanSensitivity.numberValue = std::nan("");
	TEST_ASSERT(!bus.validate(nanSensitivity).success, "A non-finite number must be rejected");

	TEST_ASSERT(!bus.validate(RemoveBootStepCommand{0}).success,
		"Removing from an empty boot sequence must be rejected");

	AddBootStepCommand incomplete;
	incomplete.step.kind = BootStep::Kind::PlayCutscene; // shotName left empty
	TEST_ASSERT(!bus.validate(incomplete).success,
		"play_cutscene with no shotName must be rejected");

	// A rejected command must not have mutated anything.
	TEST_ASSERT(bus.settings().targetFps == 144, "A rejected command must leave settings untouched");

	// --- accepted ---------------------------------------------------------
	SetProjectSettingCommand goodFps;
	goodFps.key = "targetFps";
	goodFps.numberValue = 60.0;
	TEST_ASSERT(bus.execute(goodFps).success, "A valid targetFps must be accepted");
	TEST_ASSERT(bus.settings().targetFps == 60, "targetFps must be applied");

	AddBootStepCommand addWait;
	addWait.step.kind = BootStep::Kind::WaitSeconds;
	addWait.step.seconds = 2.5F;
	TEST_ASSERT(bus.execute(addWait).success, "Adding a wait step must succeed");

	AddBootStepCommand addCutscene;
	addCutscene.step.kind = BootStep::Kind::PlayCutscene;
	addCutscene.step.shotName = "Intro";
	TEST_ASSERT(bus.execute(addCutscene).success, "Adding a cutscene step must succeed");
	TEST_ASSERT(bus.settings().bootSequence.size() == 2, "Boot sequence must have 2 steps");

	TEST_ASSERT(bus.execute(MoveBootStepCommand{1, 0}).success, "Moving a step must succeed");
	TEST_ASSERT(bus.settings().bootSequence[0].kind == BootStep::Kind::PlayCutscene,
		"Move must reorder the sequence");

	// --- persistence ------------------------------------------------------
	// execute() writes through to disk, so a fresh load must agree.
	ProjectSettings reloaded;
	TEST_ASSERT(loadProjectSettings(root, reloaded).success, "Reload after execute must succeed");
	TEST_ASSERT(reloaded.targetFps == 60, "targetFps must have been persisted");
	TEST_ASSERT(reloaded.bootSequence.size() == 2, "Boot sequence must have been persisted");
	TEST_ASSERT(reloaded.bootSequence[0].kind == BootStep::Kind::PlayCutscene &&
			reloaded.bootSequence[0].shotName == "Intro",
		"Boot step kind and fields must survive the round-trip");
	TEST_ASSERT(std::abs(reloaded.bootSequence[1].seconds - 2.5F) < 0.0001F,
		"Boot step seconds must survive the round-trip");

	// A hand-added key must not be destroyed by an editor save.
	{
		std::ifstream in(root / "Game" / "Project.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		TEST_ASSERT(text.find("customKeySomeoneAddedByHand") != std::string::npos,
			"Saving must preserve unknown keys already in Project.json");
		TEST_ASSERT(text.find('\n') != std::string::npos,
			"Saved Project.json must stay pretty-printed, not collapse to one line");
	}

	// An unknown boot-step kind (a newer editor's file) is skipped, not fatal.
	{
		std::ofstream out(root / "Game" / "Project.json", std::ios::binary);
		out << R"({"bootSequence":[{"kind":"from_the_future"},{"kind":"lock_player_input"}]})";
	}
	ProjectSettings forward;
	TEST_ASSERT(loadProjectSettings(root, forward).success, "A file with an unknown step kind must still load");
	TEST_ASSERT(forward.bootSequence.size() == 1 &&
			forward.bootSequence[0].kind == BootStep::Kind::LockPlayerInput,
		"An unknown boot step kind must be skipped, keeping the ones we understand");

	std::error_code cleanup;
	fs::remove_all(root, cleanup);
}

// ----------------------------------------------------------------------------
// The boot sequence gates player control, so "does it ever unlock" is the
// property that matters - a sequence that never finishes leaves the game
// permanently unplayable.
// ----------------------------------------------------------------------------
static void testBootSequence()
{
	EditorScene scene(".");
	GameplayState gameplay;

	// No steps: control is immediate, exactly as before the feature existed.
	resetBootSequence(gameplay, {});
	TEST_ASSERT(!gameplay.bootSequence.running, "An empty boot sequence must not arm");
	TEST_ASSERT(!bootSequenceBlocksInput(gameplay), "An empty boot sequence must not block input");

	// Two waits totalling 0.3s, then control.
	std::vector<BootStep> steps;
	{
		BootStep a;
		a.kind = BootStep::Kind::WaitSeconds;
		a.seconds = 0.1F;
		BootStep b;
		b.kind = BootStep::Kind::WaitSeconds;
		b.seconds = 0.2F;
		steps.push_back(a);
		steps.push_back(b);
	}
	resetBootSequence(gameplay, steps);
	TEST_ASSERT(bootSequenceBlocksInput(gameplay), "An armed boot sequence must block input");

	// Not playing => no progress at all.
	tickBootSequence(steps, scene, gameplay, false, 1.0F);
	TEST_ASSERT(bootSequenceBlocksInput(gameplay), "A paused game must not advance the boot sequence");

	for (int i = 0; i < 10 && gameplay.bootSequence.running; ++i)
	{
		tickBootSequence(steps, scene, gameplay, true, 0.05F);
	}
	TEST_ASSERT(!gameplay.bootSequence.running, "A wait-only sequence must finish");
	TEST_ASSERT(!bootSequenceBlocksInput(gameplay), "Input must be released once the sequence ends");

	// unlock_player_input hands control back early, mid-sequence.
	{
		std::vector<BootStep> early;
		BootStep unlock;
		unlock.kind = BootStep::Kind::UnlockPlayerInput;
		BootStep wait;
		wait.kind = BootStep::Kind::WaitSeconds;
		wait.seconds = 5.0F;
		early.push_back(unlock);
		early.push_back(wait);

		resetBootSequence(gameplay, early);
		TEST_ASSERT(bootSequenceBlocksInput(gameplay), "Sequence must start locked");
		tickBootSequence(early, scene, gameplay, true, 0.016F);
		TEST_ASSERT(!bootSequenceBlocksInput(gameplay),
			"unlock_player_input must release control while later steps still run");
		TEST_ASSERT(gameplay.bootSequence.running, "The sequence must keep running after an early unlock");
	}

	// A cutscene step waits on the host rather than guessing a duration, but
	// must not deadlock once the host reports back.
	{
		std::vector<BootStep> cut;
		BootStep shot;
		shot.kind = BootStep::Kind::PlayCutscene;
		shot.shotName = "Intro";
		cut.push_back(shot);

		resetBootSequence(gameplay, cut);
		tickBootSequence(cut, scene, gameplay, true, 0.016F);
		TEST_ASSERT(gameplay.bootSequence.requestedCutsceneShot == "Intro",
			"A cutscene step must publish the shot name for the host");
		tickBootSequence(cut, scene, gameplay, true, 10.0F);
		TEST_ASSERT(gameplay.bootSequence.running,
			"A cutscene step must NOT time out on its own - it waits for the host");

		gameplay.bootSequence.hostStepFinished = true;
		tickBootSequence(cut, scene, gameplay, true, 0.016F);
		TEST_ASSERT(!gameplay.bootSequence.running, "Host completion must advance past the cutscene");
		TEST_ASSERT(!bootSequenceBlocksInput(gameplay), "Control must return after the cutscene");
	}

	// A play_animation step naming an entity that does not exist must not
	// stall the sequence forever.
	{
		std::vector<BootStep> anim;
		BootStep play;
		play.kind = BootStep::Kind::PlayAnimation;
		play.targetEntity = "NoSuchEntity";
		anim.push_back(play);

		resetBootSequence(gameplay, anim);
		tickBootSequence(anim, scene, gameplay, true, 0.016F);
		TEST_ASSERT(!gameplay.bootSequence.running,
			"play_animation on a missing entity must complete rather than hang");
	}

	// Steps deleted mid-Play must not index out of bounds.
	{
		resetBootSequence(gameplay, steps);
		const std::vector<BootStep> emptied;
		tickBootSequence(emptied, scene, gameplay, true, 0.016F);
		TEST_ASSERT(!gameplay.bootSequence.running && !bootSequenceBlocksInput(gameplay),
			"Emptying the sequence mid-Play must release control, not read past the end");
	}
}

static void writeMinimalWav(const std::filesystem::path& path)
{
	// 44-byte PCM header + 8 silent 16-bit samples (mono, 8000 Hz).
	const unsigned char wav[] = {
		'R','I','F','F', 36 + 16, 0, 0, 0, 'W','A','V','E',
		'f','m','t',' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F, 0, 0,
		0x80, 0x3E, 0, 0, 2, 0, 16, 0, 'd','a','t','a', 16, 0, 0, 0,
		0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0
	};
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(wav), sizeof(wav));
}

static void testAudioSourceAndHooks()
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "gf_audio_test";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "Game" / "Audio", ec);
	fs::create_directories(root / "Game" / "Scenes", ec);
	writeMinimalWav(root / "Game" / "Audio" / "beep.wav");
	{
		std::ofstream project(root / "Game" / "Project.json");
		project << R"({"format":"GameForgerProject","version":1,"name":"t","startupScene":"Game/Scenes/Main.gfprod"})";
	}
	{
		std::ofstream scene(root / "Game" / "Scenes" / "Main.gfprod");
		scene << "{}";
	}

	TEST_ASSERT(gameforger::core::resolveProjectFile(
		root, "Game/Audio/beep.wav", "Game/Audio", gameforger::core::audioClipExtensions()).has_value(),
		"A clip under Game/Audio must resolve");
	TEST_ASSERT(!gameforger::core::resolveProjectFile(
		root, "Game/Audio/../Scripts/x.wav", "Game/Audio", gameforger::core::audioClipExtensions()).has_value(),
		"A clip that escapes Game/Audio must be rejected");

	gameforger::core::AudioEngine audio;
	(void)audio.initialize();
	TEST_ASSERT(audio.play(root, "Game/Audio/beep.wav"),
		"play() must succeed for a valid clip even in silent mode");
	TEST_ASSERT(!audio.play(root, "Game/Audio/missing.wav"),
		"play() must reject a clip that does not exist");
	TEST_ASSERT(audio.clipDurationSeconds(root, "Game/Audio/beep.wav") > 0.0F,
		"A real wav must report a positive duration");
	audio.shutdown();

	EditorScene scene(root.string());
	SceneEntity entity;
	entity.name = "Speaker";
	entity.hasAudioSource = true;
	entity.audioSource.clipAssetPath = "Game/Audio/beep.wav";
	entity.audioSource.loop = true;
	entity.audioSource.is3D = false;
	scene.replaceEntities({entity});

	const fs::path scenePath = root / "Game" / "Scenes" / "audio.gfprod";
	TEST_ASSERT(saveScene(scenePath, scene.entities()).success, "Saving a scene with an audio source must succeed");
	const SceneLoadResult loaded = loadScene(scenePath);
	TEST_ASSERT(loaded.success && loaded.entities.size() == 1, "Loading a scene with an audio source must succeed");
	TEST_ASSERT(loaded.entities[0].hasAudioSource &&
		loaded.entities[0].audioSource.clipAssetPath == "Game/Audio/beep.wav" && loaded.entities[0].audioSource.loop,
		"AudioSourceData must round-trip through the scene file");

	ProjectSettingsBus bus(root);
	AddAudioHookCommand add;
	add.hook.event = AudioHook::Event::OnPickup;
	add.hook.clipPath = "Game/Audio/beep.wav";
	add.hook.volume = 0.5F;
	TEST_ASSERT(bus.execute(add).success, "Adding an audio hook must succeed");
	TEST_ASSERT(bus.settings().audioHooks.size() == 1 &&
		bus.settings().audioHooks[0].event == AudioHook::Event::OnPickup,
		"The hook must persist in memory");

	ProjectSettings reloaded;
	TEST_ASSERT(loadProjectSettings(root, reloaded).success, "Reloading settings must succeed");
	TEST_ASSERT(reloaded.audioHooks.size() == 1 &&
		reloaded.audioHooks[0].clipPath == "Game/Audio/beep.wav",
		"Audio hooks must round-trip through Settings.json");

	AddAudioHookCommand bad;
	bad.hook.event = AudioHook::Event::OnPickup;
	TEST_ASSERT(!bus.validate(bad).success, "A hook with an empty clipPath must be rejected");

	fs::remove_all(root, ec);
}

// ----------------------------------------------------------------------------
// Storyboard shots used to live only in session state and were lost on every
// restart. They now ride along in the scene file, so the round-trip - and the
// backward compatibility of a scene saved before they existed - is pinned.
// ----------------------------------------------------------------------------
static void testStoryboardSerialization()
{
	const std::filesystem::path sceneFile = "test_storyboard.scene";

	CineShot intro;
	intro.name = "Intro";
	intro.cameraPath.enabled = true;
	intro.cameraPath.looping = true;
	intro.cameraPath.keyframes.push_back(TransformKeyframe{0.0F, glm::vec3(1.0F, 2.0F, 3.0F), glm::vec3(0.0F), glm::vec3(1.0F)});
	intro.cameraPath.keyframes.push_back(TransformKeyframe{2.5F, glm::vec3(4.0F, 5.0F, 6.0F), glm::vec3(0.0F, 90.0F, 0.0F), glm::vec3(1.0F)});
	// Deliberately out of order: the loader must sort these.
	(void)insertAudioCueSorted(intro.audioCues, AudioCue{2.0F, "Game/Audio/late.wav", 0.5F});
	(void)insertAudioCueSorted(intro.audioCues, AudioCue{0.5F, "Game/Audio/early.wav", 1.0F});

	CineShot empty;
	empty.name = "NoCues";

	std::vector<SceneEntity> entities;
	SceneEntity camera;
	camera.name = "CineCam";
	camera.isCineCamera = true;
	entities.push_back(camera);

	TEST_ASSERT(saveScene(sceneFile, entities, {intro, empty}).success, "Saving a scene with shots must succeed");

	const SceneLoadResult loaded = loadScene(sceneFile);
	TEST_ASSERT(loaded.success, "Loading a scene with shots must succeed");
	TEST_ASSERT(loaded.shots.size() == 2, "Both shots must round-trip");
	TEST_ASSERT(loaded.shots[0].name == "Intro", "Shot name must round-trip");
	TEST_ASSERT(loaded.shots[0].cameraPath.looping, "Shot looping flag must round-trip");
	TEST_ASSERT(loaded.shots[0].cameraPath.keyframes.size() == 2, "Camera path keyframes must round-trip");
	TEST_ASSERT(std::abs(loaded.shots[0].cameraPath.keyframes[1].time - 2.5F) < 0.001F,
		"Keyframe time must round-trip");
	TEST_ASSERT(std::abs(loaded.shots[0].cameraPath.keyframes[1].rotationEuler.y - 90.0F) < 0.01F,
		"Keyframe rotation must round-trip");

	TEST_ASSERT(loaded.shots[0].audioCues.size() == 2, "Audio cues must round-trip");
	TEST_ASSERT(loaded.shots[0].audioCues[0].clipPath == "Game/Audio/early.wav",
		"Audio cues must come back sorted by time, earliest first");
	TEST_ASSERT(std::abs(loaded.shots[0].audioCues[0].time - 0.5F) < 0.001F, "Cue time must round-trip");
	TEST_ASSERT(std::abs(loaded.shots[0].audioCues[1].volume - 0.5F) < 0.001F, "Cue volume must round-trip");
	TEST_ASSERT(loaded.shots[1].audioCues.empty(), "A shot with no cues must load with none");

	// A scene written before storyboards existed has no "storyboard" key at
	// all. It must load cleanly with an empty shot list, not fail.
	TEST_ASSERT(saveScene(sceneFile, entities).success, "Saving with no shots must succeed");
	const SceneLoadResult noShots = loadScene(sceneFile);
	TEST_ASSERT(noShots.success, "A scene with no storyboard must load");
	TEST_ASSERT(noShots.shots.empty(), "A scene with no storyboard must yield no shots");
	TEST_ASSERT(noShots.entities.size() == 1, "Entities must still load alongside an empty storyboard");

	std::filesystem::remove(sceneFile);
	std::filesystem::remove(sceneFile.string() + ".bak");
}

// ----------------------------------------------------------------------------
// Cue ordering helpers. Playback walks the list assuming it is sorted, and the
// timeline drags cues around freely, so a retimed cue must land in the right
// slot AND the caller's selection must follow it.
// ----------------------------------------------------------------------------
static void testAudioCueOrdering()
{
	std::vector<AudioCue> cues;
	TEST_ASSERT(insertAudioCueSorted(cues, AudioCue{5.0F, "e.wav", 1.0F}) == 0, "First insert lands at 0");
	TEST_ASSERT(insertAudioCueSorted(cues, AudioCue{1.0F, "a.wav", 1.0F}) == 0, "Earlier cue lands at the front");
	TEST_ASSERT(insertAudioCueSorted(cues, AudioCue{3.0F, "c.wav", 1.0F}) == 1, "Middle cue lands between");
	TEST_ASSERT(cues[0].clipPath == "a.wav" && cues[1].clipPath == "c.wav" && cues[2].clipPath == "e.wav",
		"Cues must be ordered by time");

	// Ties keep insertion order rather than jumping ahead of what is there.
	TEST_ASSERT(insertAudioCueSorted(cues, AudioCue{3.0F, "c2.wav", 1.0F}) == 2,
		"A cue at the same time must land after the existing one");

	// Retime the first cue past the end; the returned index must track it.
	cues[0].time = 99.0F;
	const std::size_t moved = resortAudioCues(cues, 0);
	TEST_ASSERT(moved == cues.size() - 1, "A cue dragged to the end must report its new index");
	TEST_ASSERT(cues[moved].clipPath == "a.wav", "The reported index must be the cue that moved");
	for (std::size_t i = 1; i < cues.size(); ++i)
	{
		TEST_ASSERT(cues[i - 1].time <= cues[i].time, "Cues must remain sorted after a retime");
	}
}

// ----------------------------------------------------------------------------
// Manager registry + on_end. This is what replaced the per-entity "Lock Cursor"
// checkbox: the host asks "is anything registered?" instead of reading a flag
// off an entity. A leaked registration would leave the cursor captured with
// nothing driving it, so registration/unregistration is pinned here.
// ----------------------------------------------------------------------------
static void testManagerRegistryAndOnEnd()
{
	const std::filesystem::path scriptsDir = "Game/Scripts";
	std::filesystem::create_directories(scriptsDir);
	const std::filesystem::path scriptFile = scriptsDir / "test_manager_lifecycle.lua";
	{
		std::ofstream out(scriptFile);
		out << "local M = {}\n"
			<< "function M:on_start()\n"
			<< "  self.managers:register('test_manager')\n"
			<< "  self.gameManager:setCursorLock(true)\n"
			<< "end\n"
			<< "function M:on_end()\n"
			<< "  self.managers:unregister('test_manager')\n"
			<< "  self.gameManager:setCursorLock(false)\n"
			<< "end\n"
			<< "return M\n";
	}

	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
	(void)scene.execute(CreateEntityCommand{"Player", PrimitiveType::Capsule, glm::vec3(0.0F)});
	const SceneEntity* player = scene.findEntity("Player");
	TEST_ASSERT(player != nullptr, "Test entity must exist");

	MockInputSource input;

	bool cursorLocked = false;
	ScriptRuntime::Config config;
	config.cursorLockSetCallback = [&cursorLocked](const bool locked) { cursorLocked = locked; };

	ScriptRuntime runtime;
	runtime.initialize(scene, bus, input, config);
	TEST_ASSERT(runtime.listManagers().empty(), "Registry must start empty");

	TEST_ASSERT(runtime.startScript(player->id, "Game/Scripts/test_manager_lifecycle.lua", "."),
		"Manager lifecycle script must start");
	TEST_ASSERT(runtime.hasManager("test_manager"), "on_start must register the manager");
	TEST_ASSERT(runtime.listManagers().size() == 1, "Exactly one manager registered");
	TEST_ASSERT(cursorLocked, "setCursorLock(true) must reach the host callback");

	// Registering the same name twice must not duplicate - otherwise one
	// unregister would leave a phantom entry and the cursor would stay locked.
	runtime.registerManager("test_manager");
	TEST_ASSERT(runtime.listManagers().size() == 1, "register must be idempotent");

	// Detaching the script fires on_end, which unregisters and releases.
	runtime.stopScript(player->id, "Game/Scripts/test_manager_lifecycle.lua");
	TEST_ASSERT(!runtime.hasManager("test_manager"), "on_end must unregister on stopScript");
	TEST_ASSERT(!cursorLocked, "on_end must release the cursor on stopScript");

	// And shutdown must fire on_end for anything still running.
	runtime.initialize(scene, bus, input, config);
	TEST_ASSERT(runtime.startScript(player->id, "Game/Scripts/test_manager_lifecycle.lua", "."),
		"Script must restart for the shutdown case");
	TEST_ASSERT(cursorLocked, "Cursor locked again after restart");
	runtime.shutdown();
	TEST_ASSERT(!cursorLocked, "shutdown must fire on_end before closing the VM");
	TEST_ASSERT(runtime.listManagers().empty(), "shutdown must clear the registry");

	// Unregistering something unknown is a no-op, not a crash.
	runtime.unregisterManager("never_registered");

	std::filesystem::remove(scriptFile);
}

// ----------------------------------------------------------------------------
// AudioHook::loop. Background music is an on_play_start hook with this set;
// without it the track fired once and stopped, so BG music was impossible even
// though AudioEngine::play always took a loop argument. A file written before
// the field existed must still load, with loop defaulting to false.
// ----------------------------------------------------------------------------
static void testAudioHookLoopRoundTrip()
{
	namespace fs = std::filesystem;
	const fs::path root = fs::absolute("test_audio_hook_loop_root");
	std::error_code cleanupBefore;
	fs::remove_all(root, cleanupBefore);
	fs::create_directories(root / "Game" / "Audio");
	{ std::ofstream(root / "Game" / "Audio" / "music.wav") << "x"; }
	{
		std::ofstream out(root / "Game" / "Project.json");
		out << R"({"format":"GameForgerProject","version":1})";
	}

	ProjectSettingsBus bus(root);

	AddAudioHookCommand music;
	music.hook.event = AudioHook::Event::OnPlayStart;
	music.hook.clipPath = "Game/Audio/music.wav";
	music.hook.volume = 0.5F;
	music.hook.loop = true;
	TEST_ASSERT(bus.execute(music).success, "Adding a looping hook must succeed");

	AddAudioHookCommand oneShot;
	oneShot.hook.event = AudioHook::Event::OnPickup;
	oneShot.hook.clipPath = "Game/Audio/music.wav";
	TEST_ASSERT(bus.execute(oneShot).success, "Adding a one-shot hook must succeed");
	TEST_ASSERT(!oneShot.hook.loop, "loop must default to false");

	ProjectSettings reloaded;
	TEST_ASSERT(loadProjectSettings(root, reloaded).success, "Reload must succeed");
	TEST_ASSERT(reloaded.audioHooks.size() == 2, "Both hooks must persist");
	TEST_ASSERT(reloaded.audioHooks[0].loop, "loop=true must survive the round-trip");
	TEST_ASSERT(!reloaded.audioHooks[1].loop, "loop=false must survive the round-trip");

	// A settings file written before `loop` existed: the key is simply absent
	// and must read as false rather than failing or defaulting to true.
	// Note audioHooks live in Settings.json, not Project.json - reader and
	// writer agree on that, so this fixture has to match.
	{
		std::ofstream out(root / "Game" / "Settings.json");
		out << R"({"audioHooks":[{"event":"on_play_start","clipPath":"Game/Audio/music.wav","volume":1.0}]})";
	}
	ProjectSettings legacy;
	TEST_ASSERT(loadProjectSettings(root, legacy).success, "A pre-loop file must still load");
	TEST_ASSERT(legacy.audioHooks.size() == 1, "The legacy hook must load");
	TEST_ASSERT(!legacy.audioHooks[0].loop, "A missing loop key must default to false");

	std::error_code cleanup;
	fs::remove_all(root, cleanup);
}

// ----------------------------------------------------------------------------
// Every script shipped in Game/Scripts must actually load and run. Several did
// not: controller.lua was written against a Unity-shaped API that never existed
// here (Input./Entity./Vector()/Raycast()/UI. and an Update() entry point),
// test.lua was a LOVE 2D sketch, SlidingPuzzle.lua was a bare print() with no
// returned table, BlockDragger.lua registered mouse callbacks that do not
// exist, Script.lua called camera:setCrosshair, and Script_2.lua multiplied a
// table by a number. All of them failed the moment they were attached.
//
// Nothing caught that, because the C++ tests never loaded a .lua file. This
// walks the real directory, so a newly added broken script fails the suite
// rather than waiting to be discovered by someone pressing Play.
// ----------------------------------------------------------------------------
static void testAllShippedScriptsLoad()
{
	namespace fs = std::filesystem;

	// ctest runs this from the build directory, and other tests create an
	// empty Game/Scripts there for their own probe files - so "does
	// Game/Scripts exist relative to cwd" finds the wrong one. Walk up until a
	// Game/Scripts is found that holds at least one non-probe script, which
	// lands on the real source tree both locally and on CI.
	fs::path scriptsDir;
	{
		std::error_code walkError;
		fs::path candidate = fs::current_path(walkError);
		for (int depth = 0; depth < 6 && !candidate.empty(); ++depth)
		{
			const fs::path guess = candidate / "Game" / "Scripts";
			std::error_code ec;
			if (fs::is_directory(guess, ec))
			{
				for (const fs::directory_entry& entry : fs::directory_iterator(guess, ec))
				{
					const std::string name = entry.path().filename().string();
					if (entry.path().extension() == ".lua" && name.rfind("test_", 0) != 0)
					{
						scriptsDir = guess;
						break;
					}
				}
			}
			if (!scriptsDir.empty() || !candidate.has_parent_path() ||
				candidate.parent_path() == candidate)
			{
				break;
			}
			candidate = candidate.parent_path();
		}
	}
	TEST_ASSERT(!scriptsDir.empty(), "Could not locate the project's Game/Scripts from the test working directory");

	std::vector<std::string> scripts;
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(scriptsDir, ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == ".lua")
		{
			// The suite writes its own probe scripts into this directory; skip
			// those so this test only judges what the project ships.
			const std::string name = entry.path().filename().string();
			if (name.rfind("test_", 0) == 0)
			{
				continue;
			}
			scripts.push_back("Game/Scripts/" + name);
		}
	}
	TEST_ASSERT(!scripts.empty(), "Game/Scripts must contain at least one script to check");

	// startScript confines paths under projectRoot/Game/Scripts and REJECTS
	// absolute ones, so pass the relative form plus the root it resolves against.
	const fs::path projectRoot = scriptsDir.parent_path().parent_path();

	int failed = 0;
	for (const std::string& scriptPath : scripts)
	{
		// A fresh scene and runtime per script: one script leaving the VM in a
		// bad state must not be reported against the next one.
		EditorScene scene(".");
		AICommandBus bus;
		bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
		MockInputSource input;
		ScriptRuntime runtime;
		runtime.initialize(scene, bus, input, ScriptRuntime::Config{});

		if (!runtime.startScript(1, scriptPath, projectRoot))
		{
			std::cerr << "       script failed to start: " << scriptPath << "\n";
			++failed;
			continue;
		}
		// on_start ran; drive one frame so on_update is exercised too - most of
		// the fictional-API calls lived there, not in on_start.
		runtime.updateEntity(1, 0.016F);
		runtime.shutdown();
	}

	TEST_ASSERT(failed == 0, "Every script in Game/Scripts must load and tick without error");
}

// ----------------------------------------------------------------------------
// The frame profiler and its exported report. buildReport returns a string
// rather than writing a file precisely so it can be checked here: the export
// is meant to be read after a stutter, so it has to contain the breakdown of
// the dip and not just a frame count.
// ----------------------------------------------------------------------------
static void testFrameProfilerReport()
{
	gameforger::core::FrameProfiler profiler;
	profiler.setDipThresholdMs(5.0F);

	// An empty profiler must produce a report, not crash or lie.
	TEST_ASSERT(profiler.buildReport().find("No frames were recorded") != std::string::npos,
		"An empty profiler must say so rather than emitting an empty report");

	// A fast frame: two cheap zones, well under the threshold.
	for (int i = 0; i < 3; ++i)
	{
		profiler.beginFrame();
		profiler.beginZone("Cheap");
		profiler.endZone();
		profiler.endFrame();
	}
	TEST_ASSERT(profiler.dipCount() == 0, "Fast frames must not be recorded as dips");
	TEST_ASSERT(profiler.history().size() == 3, "Every frame must land in the rolling window");

	// A deliberately slow frame, with the cost inside a named zone so the
	// report can attribute it.
	profiler.beginFrame();
	profiler.beginZone("Slow Zone");
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
	while (std::chrono::steady_clock::now() < until) { /* burn wall time */ }
	profiler.endZone();
	profiler.endFrame();

	TEST_ASSERT(profiler.dipCount() == 1, "A frame over the threshold must be recorded as a dip");
	TEST_ASSERT(!profiler.worstFrames().empty(), "The dip must be retained for inspection");
	const auto& dip = profiler.worstFrames().front();
	TEST_ASSERT(dip.totalMilliseconds >= 10.0F, "The dip must record its real cost");
	TEST_ASSERT(!dip.zones.empty() && dip.zones.front().name == "Slow Zone",
		"The dominant zone must be first, so the report names the culprit");

	// Zones accumulate rather than overwrite when entered twice in one frame.
	profiler.beginFrame();
	profiler.beginZone("Twice");
	profiler.endZone();
	profiler.beginZone("Twice");
	profiler.endZone();
	profiler.endFrame();
	int twiceCount = 0;
	for (const auto& zone : profiler.lastFrame().zones)
	{
		if (zone.name == "Twice") { ++twiceCount; }
	}
	TEST_ASSERT(twiceCount == 1, "A zone entered twice must accumulate into one entry, not duplicate");

	const std::string report = profiler.buildReport("Test report");
	TEST_ASSERT(report.find("# Test report") != std::string::npos, "Report must carry its title");
	TEST_ASSERT(report.find("## Summary") != std::string::npos, "Report must have a summary");
	TEST_ASSERT(report.find("## Average cost per zone") != std::string::npos, "Report must average zones");
	TEST_ASSERT(report.find("Slow Zone") != std::string::npos,
		"Report must name the zone responsible for the dip");
	TEST_ASSERT(report.find("## Raw frame times") != std::string::npos, "Report must include raw samples");

	profiler.clearWorst();
	TEST_ASSERT(profiler.dipCount() == 0 && profiler.worstFrames().empty(),
		"Clearing dips must reset both the list and the counter");
}

// ----------------------------------------------------------------------------
// self.audio:isPlaying() and clip-scoped stop. isPlaying was hardcoded to
// false, so any script branching on it silently took the wrong path, and
// stop() had no scope at all - a music manager stopping its own track
// silenced every other sound in the game.
// ----------------------------------------------------------------------------
static void testAudioQueryAndScopedStop()
{
	namespace fs = std::filesystem;
	const fs::path root = fs::absolute("test_audio_scope_root");
	std::error_code cleanupBefore;
	fs::remove_all(root, cleanupBefore);
	fs::create_directories(root / "Game" / "Audio");

	gameforger::core::AudioEngine audio;
	const bool haveDevice = audio.initialize();

	// Nothing has been played, so nothing can be playing - true with or
	// without a device.
	TEST_ASSERT(!audio.isAnyPlaying(), "A fresh engine must report nothing playing");
	TEST_ASSERT(!audio.isPlaying("Game/Audio/anything.wav"),
		"An unplayed clip must not report as playing");

	// Stopping a clip that was never started, and stopping everything on an
	// empty engine, must both be harmless rather than crashing.
	audio.stop("Game/Audio/never-started.wav");
	audio.stopAll();
	TEST_ASSERT(!audio.isAnyPlaying(), "Stopping on an empty engine must stay empty");

	// A path outside Game/Audio must be refused by the confinement check
	// regardless of whether a device exists.
	TEST_ASSERT(!audio.play(root, "Game/Scripts/notaudio.lua"),
		"A clip outside Game/Audio must be refused");

	if (!haveDevice)
	{
		// CI has no audio device. The queries above are the parts that must
		// hold everywhere; actual playback cannot be asserted here.
		std::cout << "      (no audio device - playback assertions skipped)\n";
	}

	audio.shutdown();
	std::error_code cleanup;
	fs::remove_all(root, cleanup);
}

// ----------------------------------------------------------------------------
// Per-source audio effects. These are edited by the panel, by the AI and by
// hand-editing a scene file, all through the same SetPropertyCommand handler,
// so the clamping matters as much as the round-trip. A delay feedback of 1.0
// never decays; a delay of 0 seconds is a division trap.
// ----------------------------------------------------------------------------
static void testAudioSourceEffectsConversion()
{
	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });

	TEST_ASSERT(bus.execute(CreateEntityCommand{"Speaker"}).success, "Creating the entity must succeed");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "enabled", true}).success,
		"Enabling the audio source must succeed");

	// Held across the whole test: no entity is created after this point, so
	// the scene's storage cannot reallocate and invalidate it.
	const SceneEntity* speaker = scene.findEntity("Speaker");
	TEST_ASSERT(speaker != nullptr, "The entity must exist");

	// A dry source must convert to dry settings, or every object would build a
	// node graph it does not need.
	{
		const gameforger::core::EffectSettings settings = toEngineEffectSettings(speaker->audioSource);
		TEST_ASSERT(!settings.anyEnabled(), "A default source must convert to no effects");
		TEST_ASSERT(settings.filter == gameforger::core::EffectSettings::Filter::None, "Default filter must be None");
	}

	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxReverb", true}).success, "reverb");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxReverbWet", 0.25F}).success, "wet");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxDelay", true}).success, "delay");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxDelaySeconds", 0.5F}).success, "time");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxCutoffHz", 800.0F}).success, "cutoff");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fadeInSeconds", 2.0F}).success, "fade in");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fadeOutSeconds", 3.0F}).success, "fade out");

	{
		const gameforger::core::EffectSettings settings = toEngineEffectSettings(speaker->audioSource);
		TEST_ASSERT(settings.reverb, "Reverb must carry across");
		TEST_ASSERT(std::fabs(settings.reverbWet - 0.25F) < 0.001F, "Reverb wet must carry across");
		TEST_ASSERT(settings.delay, "Delay must carry across");
		TEST_ASSERT(std::fabs(settings.delaySeconds - 0.5F) < 0.001F, "Delay time must carry across");
		TEST_ASSERT(std::fabs(settings.cutoffHz - 800.0F) < 0.001F, "Cutoff must carry across");
		// Fades live on the source rather than inside AudioEffects, which makes
		// them the fields a converter is most likely to forget.
		TEST_ASSERT(std::fabs(settings.fadeInSeconds - 2.0F) < 0.001F, "Fade in must carry across");
		TEST_ASSERT(std::fabs(settings.fadeOutSeconds - 3.0F) < 0.001F, "Fade out must carry across");
	}

	// The filter is the one field converted by hand, case by case. Transposing
	// two cases would swap muffled for thin with nothing to catch it.
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxFilter", std::string("low_pass")}).success,
		"Selecting the low pass filter must succeed");
	TEST_ASSERT(toEngineEffectSettings(speaker->audioSource).filter == gameforger::core::EffectSettings::Filter::LowPass,
		"low_pass must convert to LowPass, not HighPass");

	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxFilter", std::string("high_pass")}).success,
		"Selecting the high pass filter must succeed");
	TEST_ASSERT(toEngineEffectSettings(speaker->audioSource).filter == gameforger::core::EffectSettings::Filter::HighPass,
		"high_pass must convert to HighPass, not LowPass");

	// The names are the serialized spelling, not the C++ enumerator spelling.
	// Writing this test with "LowPass" is what proved the validator rejects a
	// name it does not know rather than silently leaving the filter untouched.
	TEST_ASSERT(!bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxFilter", std::string("LowPass")}).success,
		"An unknown filter name must be rejected");
	TEST_ASSERT(toEngineEffectSettings(speaker->audioSource).filter == gameforger::core::EffectSettings::Filter::HighPass,
		"A rejected filter edit must leave the previous filter in place");
}

static void testAudioEffectsRoundTripAndClamping()
{
	const std::filesystem::path sceneFile = "test_audio_effects.scene";

	EditorScene scene(".");
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });

	TEST_ASSERT(bus.execute(CreateEntityCommand{"Speaker"}).success, "Creating the entity must succeed");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "enabled", true}).success,
		"Enabling the audio source must succeed");

	// Defaults: a brand-new source is dry.
	{
		const SceneEntity* e = scene.findEntity("Speaker");
		TEST_ASSERT(e != nullptr && !e->audioSource.effects.anyEnabled(),
			"A new audio source must start with no effects enabled");
	}

	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxReverb", true}).success,
		"Enabling reverb must succeed");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxDelay", true}).success,
		"Enabling delay must succeed");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxFilter", std::string("low_pass")}).success,
		"Setting a known filter must succeed");
	TEST_ASSERT(!bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxFilter", std::string("bandpass")}).success,
		"An unknown filter name must be rejected, not silently ignored");

	// Clamping. Runaway feedback is the dangerous one.
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxDelayDecay", 5.0F}).success,
		"An out-of-range decay is clamped, not rejected");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxDelaySeconds", 0.0F}).success,
		"An out-of-range delay time is clamped");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxCutoffHz", 999999.0F}).success,
		"An out-of-range cutoff is clamped");
	{
		const SceneEntity* e = scene.findEntity("Speaker");
		TEST_ASSERT(e != nullptr, "Entity must still exist");
		TEST_ASSERT(e->audioSource.effects.delayDecay <= 0.99F,
			"Delay feedback must be clamped below 1.0 or the echo never decays");
		TEST_ASSERT(e->audioSource.effects.delaySeconds >= 0.01F, "Delay time must be clamped above zero");
		TEST_ASSERT(e->audioSource.effects.cutoffHz <= 20000.0F, "Cutoff must be clamped to audible range");
	}

	// A non-finite value must be refused outright rather than clamped to a
	// bound, because NaN compares false against every bound.
	TEST_ASSERT(!bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxReverbWet", std::nanf("")}).success,
		"A non-finite effect value must be rejected");

	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fxReverbRoomSize", 0.8F}).success,
		"Setting room size must succeed");

	// Fades live on the source, not in AudioEffects - they ramp the voice's own
	// volume and need no node graph, so anyEnabled() must stay false for them.
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fadeInSeconds", 1.5F}).success,
		"Setting fade in must succeed");
	TEST_ASSERT(bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fadeOutSeconds", 99.0F}).success,
		"An over-long fade is clamped, not rejected");
	TEST_ASSERT(!bus.execute(SetPropertyCommand{"Speaker", "AudioSource", "fadeInSeconds", std::nanf("")}).success,
		"A non-finite fade must be rejected");
	{
		const SceneEntity* e = scene.findEntity("Speaker");
		TEST_ASSERT(e != nullptr, "Entity must exist");
		TEST_ASSERT(std::abs(e->audioSource.fadeInSeconds - 1.5F) < 0.001F, "Fade in must be stored");
		TEST_ASSERT(e->audioSource.fadeOutSeconds <= 30.0F, "Fade out must be clamped");
	}

	TEST_ASSERT(saveScene(sceneFile, scene.entities()).success, "Saving must succeed");
	const SceneLoadResult loaded = loadScene(sceneFile);
	TEST_ASSERT(loaded.success && loaded.entities.size() == 1, "Loading must succeed");
	const AudioEffects& fx = loaded.entities[0].audioSource.effects;
	TEST_ASSERT(fx.reverb, "reverb flag must round-trip");
	TEST_ASSERT(fx.delay, "delay flag must round-trip");
	TEST_ASSERT(fx.filter == AudioEffects::Filter::LowPass, "filter must round-trip by NAME");
	TEST_ASSERT(std::abs(fx.reverbRoomSize - 0.8F) < 0.001F, "reverb room size must round-trip");
	TEST_ASSERT(fx.delayDecay <= 0.99F, "clamped decay must persist clamped");
	TEST_ASSERT(std::abs(loaded.entities[0].audioSource.fadeInSeconds - 1.5F) < 0.001F,
		"fade in must round-trip");
	TEST_ASSERT(loaded.entities[0].audioSource.fadeOutSeconds <= 30.0F, "fade out must round-trip clamped");

	// A scene written before effects existed has no "effects" key at all and
	// must load dry rather than failing.
	{
		std::ofstream out(sceneFile);
		out << R"({"format":"GameForgerScene","version":8,"entities":[)"
			<< R"({"name":"Old","hasAudioSource":true,"audioSource":{"clipAssetPath":"Game/Audio/a.wav"}}]})";
	}
	const SceneLoadResult legacy = loadScene(sceneFile);
	TEST_ASSERT(legacy.success && legacy.entities.size() == 1, "A pre-effects scene must still load");
	TEST_ASSERT(!legacy.entities[0].audioSource.effects.anyEnabled(),
		"A pre-effects scene must load with effects off");
	TEST_ASSERT(legacy.entities[0].audioSource.fadeInSeconds == 0.0F &&
			legacy.entities[0].audioSource.fadeOutSeconds == 0.0F,
		"A pre-fade scene must load with no fades, i.e. instant start and stop");

	std::filesystem::remove(sceneFile);
	std::filesystem::remove(sceneFile.string() + ".bak");
}

// Main
// ----------------------------------------------------------------------------
int main()
{
	std::cout << "====================================================\n";
	std::cout << " GameForgerAI Engine Automated Test Suite\n";
	std::cout << "====================================================\n";

	RUN_TEST(testPrimitiveMeshes);
	RUN_TEST(testJsonParser);
	RUN_TEST(testSceneSerialization);
	RUN_TEST(testEditorScene);
	RUN_TEST(testScriptRuntimeSandboxing);
	RUN_TEST(testGetRightMatchesFpsCamera);
	RUN_TEST(testGameObjectComponentModel);
	RUN_TEST(testAssetDatabase);
	RUN_TEST(testMaterialSerialization);
	RUN_TEST(testScriptPropertyReflection);
	RUN_TEST(testColliderPrimitiveBlocksWhenChecked);
	RUN_TEST(testImportedMeshColliderUsesTrianglesNotScaleBox);
	RUN_TEST(testParentColliderSolidsChildren);
	RUN_TEST(testLightCameraUiRoundTrip);
	RUN_TEST(testEntityForwardMatchesForwardConvention);
	RUN_TEST(testMindGraphGeneratedLuaRuns);
	RUN_TEST(testMindGraphCatalog);
	RUN_TEST(testMindGraphCompiler);
	RUN_TEST(testMindGraphBreadcrumbWrapper);
	RUN_TEST(testMindGraphRoundTrip);
	RUN_TEST(testMindGraphPinStability);
	RUN_TEST(testSharedScriptCallbackParity);
	RUN_TEST(testCameraPoseDrivesEntityForward);
	RUN_TEST(testGizmoOnlyEntityClassification);
	RUN_TEST(testColliderBoxMeshConvexTypes);
	RUN_TEST(testChatResponseBothProtocols);
	RUN_TEST(testResolveProjectFileConfinement);
	RUN_TEST(testJsonPrettyPrinter);
	RUN_TEST(testProjectSettingsAndBus);
	RUN_TEST(testBootSequence);
	RUN_TEST(testAudioSourceAndHooks);
	RUN_TEST(testStoryboardSerialization);
	RUN_TEST(testAudioCueOrdering);
	RUN_TEST(testManagerRegistryAndOnEnd);
	RUN_TEST(testAudioHookLoopRoundTrip);
	RUN_TEST(testAllShippedScriptsLoad);
	RUN_TEST(testFrameProfilerReport);
	RUN_TEST(testAudioQueryAndScopedStop);
	RUN_TEST(testAudioEffectsRoundTripAndClamping);
	RUN_TEST(testAudioSourceEffectsConversion);

	std::cout << "====================================================\n";
	std::cout << " Tests Passed: " << g_testsPassed << " | Tests Failed: " << g_testsFailed << "\n";
	std::cout << "====================================================\n";

	return g_testsFailed == 0 ? 0 : 1;
}
