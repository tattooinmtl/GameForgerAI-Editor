#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/AIChatResponse.hpp"
#include "GameForger/Editor/AICommand.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/Json.hpp"
#include "GameForger/Editor/PrimitiveMeshes.hpp"
#include "GameForger/Editor/SceneSerializer.hpp"

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
	runtime.initialize(
		scene, bus, input,
		[&logs](bool isError, const std::string& msg) {
			if (isError) logs.push_back(msg);
		},
		nullptr, nullptr, nullptr, nullptr, nullptr);

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
	runtime.initialize(scene, bus, input, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

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

	runtime.initialize(scene, bus, input, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
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

	std::cout << "====================================================\n";
	std::cout << " Tests Passed: " << g_testsPassed << " | Tests Failed: " << g_testsFailed << "\n";
	std::cout << "====================================================\n";

	return g_testsFailed == 0 ? 0 : 1;
}
