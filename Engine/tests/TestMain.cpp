#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"
#include "GameForger/Editor/AIChatResponse.hpp"
#include "GameForger/Editor/AICommand.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/Json.hpp"
#include "GameForger/Editor/PrimitiveMeshes.hpp"
#include "GameForger/Editor/SceneSerializer.hpp"
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

	std::cout << "====================================================\n";
	std::cout << " Tests Passed: " << g_testsPassed << " | Tests Failed: " << g_testsFailed << "\n";
	std::cout << "====================================================\n";

	return g_testsFailed == 0 ? 0 : 1;
}
