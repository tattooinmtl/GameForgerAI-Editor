#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

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
// FPS player / items / inventory / weapon API tests
// ----------------------------------------------------------------------------
#include "GameForger/Editor/FpsRigBuilder.hpp"
#include "GameForger/Runtime/GameCamera.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"

namespace
{
	void writeTextFile(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}

	struct SceneFixture
	{
		EditorScene scene{"."};
		AICommandBus bus;
		MockInputSource input;
		ScriptRuntime runtime;
		GameplayState gameplay;

		SceneFixture()
		{
			bus.setHandler([this](const AIEditorCommand& cmd) { return scene.execute(cmd); });
		}

		void start()
		{
			runtime.initialize(scene, bus, input, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
			runtime.setGameplayState(&gameplay);
			for (const SceneEntity& entity : scene.entities())
			{
				for (const std::string& script : entity.scripts)
				{
					(void)runtime.startScript(entity.id, script, ".");
				}
			}
		}

		const SceneEntity* find(const std::string& name) const { return scene.findEntity(name); }
	};
}

void testScriptPropertyOverrides()
{
	const std::string scriptPath = "Game/Scripts/test_item_props.lua";
	writeTextFile(scriptPath, R"(-- @property item_name string Item
-- @property icon icon Game/Icons/a.png
-- @property weapon enum none|sword|axe sword
-- @property power number 2
local T = {}
function T:on_start()
	self.seen_name = self.item_name
	self.seen_power = self.power
end
return T
)");

	const auto props = ScriptRuntime::parseScriptProperties(scriptPath);
	TEST_ASSERT(props.size() == 4, "Must parse 4 properties including icon and enum");
	TEST_ASSERT(props[1].type == ScriptRuntime::ExposedScriptProperty::Type::Icon, "icon type parsed");
	TEST_ASSERT(props[1].defaultString == "Game/Icons/a.png", "icon default parsed");
	TEST_ASSERT(props[2].type == ScriptRuntime::ExposedScriptProperty::Type::Enum, "enum type parsed");
	TEST_ASSERT(props[2].options.size() == 3 && props[2].defaultString == "sword", "enum options/default parsed");

	SceneFixture fx;
	TEST_ASSERT(fx.bus.execute(CreateEntityCommand{"A"}).success, "create A");
	TEST_ASSERT(fx.bus.execute(CreateEntityCommand{"B"}).success, "create B");
	TEST_ASSERT(fx.bus.execute(AttachScriptCommand{"A", scriptPath}).success, "attach to A");
	TEST_ASSERT(fx.bus.execute(AttachScriptCommand{"B", scriptPath}).success, "attach to B");
	TEST_ASSERT(fx.bus.execute(SetPropertyCommand{"A", "ScriptProperty", scriptPath + "#item_name", std::string("Magic Sword")}).success,
		"override item_name on A");
	TEST_ASSERT(fx.bus.execute(SetPropertyCommand{"A", "ScriptProperty", scriptPath + "#power", std::string("5")}).success,
		"override power on A");
	TEST_ASSERT(!fx.bus.execute(SetPropertyCommand{"A", "ScriptProperty", "no-separator", std::string("x")}).success,
		"malformed script property name is rejected");

	fx.start();
	const int idA = fx.find("A")->id;
	const int idB = fx.find("B")->id;
	TEST_ASSERT(fx.runtime.getScriptStringField(idA, scriptPath, "seen_name", "") == "Magic Sword",
		"A's own value is visible in on_start");
	TEST_ASSERT(std::abs(fx.runtime.getScriptNumberField(idA, scriptPath, "seen_power", 0.0F) - 5.0F) < 0.001F,
		"numeric override converted to a number");
	TEST_ASSERT(fx.runtime.getScriptStringField(idA, scriptPath, "weapon", "") == "sword", "enum default applied");
	TEST_ASSERT(fx.runtime.getScriptStringField(idB, scriptPath, "seen_name", "") == "Item", "B keeps the default");
	fx.runtime.shutdown();

	TEST_ASSERT(fx.bus.execute(SetPropertyCommand{"A", "ScriptPropertyReset", scriptPath + "#item_name", std::string()}).success,
		"reset override");
	TEST_ASSERT(findScriptProperty(*fx.find("A"), scriptPath, "item_name") == nullptr, "override removed by reset");
	TEST_ASSERT(findScriptProperty(*fx.find("A"), scriptPath, "power") != nullptr, "other override kept");
	std::filesystem::remove(scriptPath);
}

void testScriptPropertySerialization()
{
	SceneEntity entity;
	entity.name = "Pickup \"One\"";
	entity.scripts.push_back("Game/Scripts/FPSDemo/items.lua");
	entity.scriptProperties.push_back({"Game/Scripts/FPSDemo/items.lua", "icon", "Game/Icons/My Icon.png"});
	entity.scriptProperties.push_back({"Game/Scripts/FPSDemo/items.lua", "item_name", "Axe \"of\" Doom"});
	const std::filesystem::path path = "test_script_properties.gfprod";
	TEST_ASSERT(saveScene(path, {entity}).success, "save scene with script properties");
	const SceneLoadResult loaded = loadScene(path);
	TEST_ASSERT(loaded.success && loaded.entities.size() == 1, "load it back");
	const auto& props = loaded.entities[0].scriptProperties;
	TEST_ASSERT(props.size() == 2, "both overrides round-trip");
	TEST_ASSERT(props[0].value == "Game/Icons/My Icon.png" && props[1].value == "Axe \"of\" Doom",
		"override values round-trip (incl. quotes/spaces)");
	std::filesystem::remove(path);
}

void testRenameKeepsChildrenAttached()
{
	SceneFixture fx;
	(void)fx.bus.execute(CreateEntityCommand{"Parent"});
	(void)fx.bus.execute(CreateEntityCommand{"Child"});
	(void)fx.bus.execute(SetPropertyCommand{"Child", "Parent", "parentName", std::string("Parent")});
	TEST_ASSERT(fx.bus.execute(RenameEntityCommand{"Parent", "Base"}).success, "rename parent");
	TEST_ASSERT(fx.find("Child")->parentName == "Base", "child follows the renamed parent");
}

void testActiveInHierarchy()
{
	SceneFixture fx;
	(void)fx.bus.execute(CreateEntityCommand{"Root"});
	(void)fx.bus.execute(CreateEntityCommand{"Mid"});
	(void)fx.bus.execute(CreateEntityCommand{"Leaf"});
	(void)fx.bus.execute(SetPropertyCommand{"Mid", "Parent", "parentName", std::string("Root")});
	(void)fx.bus.execute(SetPropertyCommand{"Leaf", "Parent", "parentName", std::string("Mid")});
	TEST_ASSERT(fx.scene.isActiveInHierarchy(*fx.find("Leaf")), "all active");
	TEST_ASSERT(fx.bus.execute(SetPropertyCommand{"Root", "Entity", "active", false}).success, "Entity/active command");
	TEST_ASSERT(!fx.scene.isActiveInHierarchy(*fx.find("Leaf")), "hidden root hides grandchild");
	TEST_ASSERT(fx.find("Leaf")->active, "grandchild keeps its own flag");
	// A parent cycle must not hang.
	(void)fx.bus.execute(SetPropertyCommand{"Root", "Parent", "parentName", std::string("Leaf")});
	(void)fx.scene.isActiveInHierarchy(*fx.find("Leaf"));
}

void testInventorySlotsAndStacking()
{
	GameplayState gameplay;
	ensureInventorySlots(gameplay);
	TEST_ASSERT(static_cast<int>(gameplay.inventoryItems.size()) == kInventorySlotCount, "fixed slot count");

	GameplayState::InventoryItem potion;
	potion.itemName = "Potion";
	potion.stackable = true;
	potion.maxStack = 3;
	TEST_ASSERT(addInventoryItem(gameplay, potion) == 0, "first potion -> slot 0");
	TEST_ASSERT(addInventoryItem(gameplay, potion) == 0, "second potion stacks");
	TEST_ASSERT(addInventoryItem(gameplay, potion) == 0, "third potion stacks");
	TEST_ASSERT(addInventoryItem(gameplay, potion) == 1, "fourth potion exceeds max_stack -> new slot");

	GameplayState::InventoryItem sword;
	sword.itemName = "Sword";
	sword.stackable = false;
	TEST_ASSERT(addInventoryItem(gameplay, sword) == 2, "sword -> next slot");
	TEST_ASSERT(addInventoryItem(gameplay, sword) == 3, "non-stackable never stacks");
	for (int index = 4; index < kInventorySlotCount; ++index)
	{
		(void)addInventoryItem(gameplay, sword);
	}
	TEST_ASSERT(addInventoryItem(gameplay, sword) == -1, "full bag refuses");

	// Old saves (compact lists) are compacted into fixed slots.
	GameplayState legacy;
	legacy.inventoryItems.push_back(potion);
	legacy.inventoryItems.back().count = 2;
	ensureInventorySlots(legacy);
	TEST_ASSERT(legacy.inventoryItems[0].count == 2 && legacy.inventoryItems[1].empty(), "legacy list padded");
}

void testItemPickupHideAndDrop()
{
	const std::string itemScript = kItemScriptPath;
	const bool hadRealItemScript = std::filesystem::exists(itemScript);
	if (!hadRealItemScript)
	{
		writeTextFile(itemScript, R"(-- @property item_name string Item
-- @property icon icon Game/Icons/x.png
-- @property item_type enum weapon|consumable|ammo|misc misc
-- @property weapon enum none|sword|axe none
-- @property stackable bool false
-- @property max_stack number 99
local Item = {}
function Item:on_start() end
return Item
)");
	}

	SceneFixture fx;
	(void)fx.bus.execute(CreateEntityCommand{"Player", PrimitiveType::Capsule, glm::vec3(0.0F)});
	(void)fx.bus.execute(CreateEntityCommand{"SwordProp", PrimitiveType::Cube, glm::vec3(1.0F, 0.5F, 0.0F)});
	(void)fx.bus.execute(CreateEntityCommand{"SwordBlade", PrimitiveType::Cube, glm::vec3(1.0F, 1.0F, 0.0F)});
	(void)fx.bus.execute(SetPropertyCommand{"SwordBlade", "Parent", "parentName", std::string("SwordProp")});
	TEST_ASSERT(fx.bus.execute(AttachScriptCommand{"SwordProp", itemScript}).success, "attach items.lua");
	(void)fx.bus.execute(SetPropertyCommand{"SwordProp", "ScriptProperty", itemScript + "#item_name", std::string("Sword")});
	(void)fx.bus.execute(SetPropertyCommand{"SwordProp", "ScriptProperty", itemScript + "#weapon", std::string("sword")});
	(void)fx.bus.execute(SetPropertyCommand{"SwordProp", "ScriptProperty", itemScript + "#item_type", std::string("weapon")});
	(void)fx.bus.execute(SetPropertyCommand{"SwordProp", "ScriptProperty", itemScript + "#icon", std::string("Game/Icons/sword.png")});
	ensureInventorySlots(fx.gameplay);
	fx.start();

	const SceneEntity* player = fx.find("Player");
	const SceneEntity* candidate = findPickupItemCandidate(fx.scene, player->position, 3.0F, 2.5F, player->id);
	TEST_ASSERT(candidate != nullptr && candidate->name == "SwordProp", "items.lua object is a pickup candidate");
	TEST_ASSERT(pickupDisplayName(*candidate, fx.runtime) == "Sword", "display name from item_name");
	TEST_ASSERT(pickUpItem(fx.scene, fx.bus, fx.gameplay, fx.runtime, *candidate), "pick it up");

	const GameplayState::InventoryItem& slot = fx.gameplay.inventoryItems[0];
	TEST_ASSERT(slot.itemName == "Sword" && slot.weapon == "sword" && slot.itemType == "weapon", "slot carries item data");
	TEST_ASSERT(slot.iconPath == "Game/Icons/sword.png", "slot carries the object's own icon");
	TEST_ASSERT(fx.find("SwordProp") != nullptr, "items.lua object is kept, not deleted");
	TEST_ASSERT(!fx.scene.isActiveInHierarchy(*fx.find("SwordBlade")), "stored item and its children are hidden");
	TEST_ASSERT(findPickupItemCandidate(fx.scene, player->position, 3.0F, 2.5F, player->id) == nullptr,
		"a stored item can't be picked up again");

	TEST_ASSERT(dropInventoryItem(fx.scene, fx.bus, fx.gameplay, 0, glm::vec3(4.0F, 0.6F, 4.0F), 90.0F), "drop it");
	TEST_ASSERT(fx.gameplay.inventoryItems[0].empty(), "slot emptied");
	TEST_ASSERT(fx.scene.isActiveInHierarchy(*fx.find("SwordBlade")), "dropped item visible again");
	TEST_ASSERT(glm::length(fx.find("SwordProp")->position - glm::vec3(4.0F, 0.6F, 4.0F)) < 0.001F, "dropped where asked");

	fx.runtime.shutdown();
	if (!hadRealItemScript)
	{
		std::filesystem::remove(itemScript);
	}
}

void testFpsRigBuilder()
{
	const std::vector<std::string> stubScripts{
		"Game/Scripts/FPSDemo/fps_player.lua", "Game/Scripts/FPSDemo/items.lua", "Game/Scripts/FPSDemo/health.lua", "Game/Scripts/FPSDemo/enemy.lua",
		"Game/Scripts/FPSDemo/projectiles.lua", "Game/Scripts/FPSDemo/effects.lua", "Game/Scripts/FPSDemo/xp_system.lua",
		"Game/Scripts/FPSDemo/game_manager.lua"};
	std::vector<std::string> createdStubs;
	for (const std::string& path : stubScripts)
	{
		if (!std::filesystem::exists(path))
		{
			writeTextFile(path, "local S = {}\nreturn S\n");
			createdStubs.push_back(path);
		}
	}

	SceneFixture fx;
	FpsRigOptions options;
	options.includeDemoContent = true;
	options.includeGround = true;
	const FpsRigBuildResult result = buildFpsPlayerRig(fx.scene, fx.bus, options);
	TEST_ASSERT(result.success, "rig builds without failed steps: " + result.message);
	TEST_ASSERT(result.entitiesCreated > 150, "rig + demo content is a full set of parts");

	// Regenerates the shipped demo scene (Game/Scenes/FPSDemo.gfprod) when
	// GAMEFORGER_WRITE_FPS_DEMO is set to an output path.
	{
		char* demoPath = nullptr;
		std::size_t demoPathLength = 0;
		if (_dupenv_s(&demoPath, &demoPathLength, "GAMEFORGER_WRITE_FPS_DEMO") == 0 && demoPath != nullptr)
		{
			TEST_ASSERT(saveScene(demoPath, fx.scene.entities()).success, "write the demo scene");
			std::cout << "  wrote demo scene to " << demoPath << '\n';
			std::free(demoPath);
		}
	}

	const SceneEntity* player = fx.find(result.playerName);
	TEST_ASSERT(player != nullptr && hasTag(*player, "Player"), "player tagged Player");
	for (const std::string& script : fpsDemoPlayerScripts())
	{
		TEST_ASSERT(hasScript(*player, script), "player has the FPS Demo script " + script);
	}
	const ScriptPropertyOverride* rigName = findScriptProperty(*player, "Game/Scripts/FPSDemo/fps_player.lua", "rig_name");
	TEST_ASSERT(rigName != nullptr && rigName->value == result.rigName, "player points at its rig");
	TEST_ASSERT(player->cameraRig.lockCursor, "cursor lock on for mouse look");

	for (const std::string& weapon : fpsWeaponIds())
	{
		const SceneEntity* node = fx.find(result.rigName + ".W." + weapon);
		TEST_ASSERT(node != nullptr, "weapon node exists: " + weapon);
		TEST_ASSERT(!node->active, "weapon hidden until equipped: " + weapon);
		TEST_ASSERT(node->parentName == result.rigName + ".HandR", "weapon held in the right hand: " + weapon);
	}
	TEST_ASSERT(fx.find(result.rigName + ".W.ak47.Muzzle") != nullptr, "AK-47 has a muzzle marker");
	TEST_ASSERT(fx.find(result.rigName + ".W.ak47.Support.Index") != nullptr, "AK-47 brings its support hand");
	TEST_ASSERT(fx.find(result.rigName + ".HandR.Index") != nullptr, "right hand has fingers");

	// Every rig part is tagged so raycasts and weapon hits ignore it.
	int rigParts = 0;
	for (const SceneEntity& entity : fx.scene.entities())
	{
		if (entity.name.rfind(result.rigName, 0) == 0)
		{
			++rigParts;
			TEST_ASSERT(hasTag(entity, "Viewmodel"), "rig part tagged Viewmodel: " + entity.name);
		}
	}
	TEST_ASSERT(rigParts > 80, "rig has all its parts");

	// Demo pickups are items.lua weapons.
	const SceneEntity* akPickup = fx.find("AK-47 Pickup");
	TEST_ASSERT(akPickup != nullptr && hasScript(*akPickup, kItemScriptPath), "AK-47 pickup uses items.lua");
	const ScriptPropertyOverride* weaponOverride = findScriptProperty(*akPickup, kItemScriptPath, "weapon");
	TEST_ASSERT(weaponOverride != nullptr && weaponOverride->value == "ak47", "pickup says which weapon it is");

	// The hierarchy resolves: the rig's hand ends up in front of the eye.
	applyParentConstraints(fx.scene, fx.bus);
	applyParentConstraints(fx.scene, fx.bus);
	const SceneEntity* rigRoot = fx.find(result.rigName);
	const SceneEntity* hand = fx.find(result.rigName + ".HandR");
	TEST_ASSERT(hand->position.z > rigRoot->position.z + 0.2F, "right hand sits in front of the eye (+Z)");
	// The game camera shows +X on screen-LEFT, so the right hand sits at -X.
	TEST_ASSERT(hand->position.x < rigRoot->position.x, "right hand is on the screen's right (-X)");

	// A second rig gets its own names.
	const FpsRigBuildResult second = buildFpsPlayerRig(fx.scene, fx.bus, FpsRigOptions{});
	TEST_ASSERT(second.success && second.rigName != result.rigName && second.playerName != result.playerName,
		"second rig doesn't collide with the first");

	for (const std::string& path : createdStubs)
	{
		std::filesystem::remove(path);
	}
}

void testWeaponLuaApi()
{
	const std::string targetScript = "Game/Scripts/test_target_health.lua";
	const std::string shooterScript = "Game/Scripts/test_shooter.lua";
	writeTextFile(targetScript, R"(local H = {}
function H:on_start() self.health = 100 self.stunned = 0 end
function H:on_damage(amount, source) self.health = self.health - amount self.last_source = source end
function H:on_stun(seconds) self.stunned = seconds end
return H
)");
	writeTextFile(shooterScript, R"(local S = {}
function S:on_start()
	self.fired = 0
	self.inventory_enabled = true
end
function S:on_update(dt)
	if self.fired == 1 then return end
	self.fired = 1
	local point, distance, name = self.world:raycast({x = 0, y = 1, z = 0}, {x = 0, y = 0, z = 1}, 50)
	self.hit_name = name or ""
	self.hit_distance = distance or -1
	if name then
		self.damaged = self.world:damage(name, 30) and 1 or 0
		self.world:stun(name, 2)
	end
	local near = self.world:findDamageable({x = 0, y = 1, z = 5}, 3)
	self.damageable_count = #near
	self.world:spawnBeam({x = 0, y = 1, z = 0}, point, {r = 1, g = 0, b = 0}, 0.5, 2, true)
	self.world:spawnFlash(point, {r = 1, g = 1, b = 1}, 10, 0.2)
	self.inventory:select(3)
	local selected = self.inventory:getSelected()
	self.selected_name = selected and selected.name or ""
	self.selected_weapon = selected and selected.weapon or ""
end
return S
)");

	SceneFixture fx;
	(void)fx.bus.execute(CreateEntityCommand{"Shooter", PrimitiveType::Capsule, glm::vec3(0.0F, 0.0F, 0.0F)});
	(void)fx.bus.execute(CreateEntityCommand{"Target", PrimitiveType::Cube, glm::vec3(0.0F, 1.0F, 5.0F)});
	(void)fx.bus.execute(CreateEntityCommand{"Viewmodel Part", PrimitiveType::Cube, glm::vec3(0.0F, 1.0F, 2.0F)});
	(void)fx.bus.execute(AddTagCommand{"Viewmodel Part", "Viewmodel"});
	(void)fx.bus.execute(AttachScriptCommand{"Target", targetScript});
	(void)fx.bus.execute(AttachScriptCommand{"Shooter", shooterScript});
	ensureInventorySlots(fx.gameplay);
	fx.gameplay.inventoryItems[2].itemName = "AK-47";
	fx.gameplay.inventoryItems[2].weapon = "ak47";
	fx.gameplay.inventoryItems[2].count = 1;
	fx.start();

	const int shooterId = fx.find("Shooter")->id;
	const int targetId = fx.find("Target")->id;
	fx.runtime.updateEntity(shooterId, 0.016F);

	TEST_ASSERT(fx.runtime.getScriptStringField(shooterId, shooterScript, "hit_name", "") == "Target",
		"raycast skips the Viewmodel-tagged part and the caller, hits the target");
	TEST_ASSERT(std::abs(fx.runtime.getScriptNumberField(shooterId, shooterScript, "hit_distance", -1.0F) - 4.0F) < 0.01F,
		"raycast distance is to the target's box face");
	TEST_ASSERT(fx.runtime.getScriptNumberField(shooterId, shooterScript, "damaged", 0.0F) == 1.0F, "damage() reports handled");
	TEST_ASSERT(std::abs(fx.runtime.getScriptNumberField(targetId, targetScript, "health", 0.0F) - 70.0F) < 0.001F,
		"on_damage received the amount");
	TEST_ASSERT(fx.runtime.getScriptStringField(targetId, targetScript, "last_source", "") == "Shooter",
		"on_damage received the attacker's name");
	TEST_ASSERT(std::abs(fx.runtime.getScriptNumberField(targetId, targetScript, "stunned", 0.0F) - 2.0F) < 0.001F,
		"on_stun received the seconds");
	TEST_ASSERT(fx.runtime.getScriptNumberField(shooterId, shooterScript, "damageable_count", 0.0F) == 1.0F,
		"findDamageable finds the target");
	TEST_ASSERT(fx.gameplay.beams.size() == 1 && fx.gameplay.beams[0].jagged, "spawnBeam queues a jagged beam");
	TEST_ASSERT(fx.gameplay.flashes.size() == 1, "spawnFlash queues a flash");
	TEST_ASSERT(fx.gameplay.selectedSlot == 2, "inventory:select is 1-based");
	TEST_ASSERT(fx.runtime.getScriptStringField(shooterId, shooterScript, "selected_weapon", "") == "ak47",
		"inventory:getSelected returns the slot's weapon");
	TEST_ASSERT(entityProvidesInventory(*fx.find("Shooter"), fx.runtime), "inventory_enabled field is recognized");

	// tickEffects clamps each step to 0.1s (like every gameplay tick).
	tickEffects(fx.gameplay, true, 0.1F);
	tickEffects(fx.gameplay, true, 0.1F);
	tickEffects(fx.gameplay, true, 0.1F);
	TEST_ASSERT(fx.gameplay.flashes.empty() && fx.gameplay.beams.size() == 1, "effects age out by their own lifetime");

	fx.runtime.shutdown();
	std::filesystem::remove(targetScript);
	std::filesystem::remove(shooterScript);
}

void testGetRightMatchesScreenRight()
{
	// Regression: audit H-Script-1 (2026-08-13) flipped getRight to +X on
	// paper, which made D strafe LEFT. Check it against the camera the
	// renderer really uses (glm::lookAt) at several yaws.
	const std::string script = "Game/Scripts/test_get_right.lua";
	writeTextFile(script, R"(local T = {}
function T:on_start()
	local r = self.entity:getRight()
	self.rx = r.x self.ry = r.y self.rz = r.z
end
return T
)");
	for (const float yaw : {0.0F, 37.0F, 90.0F, 180.0F, -125.0F})
	{
		SceneFixture fx;
		(void)fx.bus.execute(CreateEntityCommand{"P"});
		(void)fx.bus.execute(SetPropertyCommand{"P", "Transform", "rotation", glm::vec3(0.0F, yaw, 0.0F)});
		(void)fx.bus.execute(AttachScriptCommand{"P", script});
		fx.start();
		const int id = fx.find("P")->id;
		const glm::vec3 right(fx.runtime.getScriptNumberField(id, script, "rx", 0.0F),
			fx.runtime.getScriptNumberField(id, script, "ry", 0.0F), fx.runtime.getScriptNumberField(id, script, "rz", 0.0F));
		// Camera looking along the entity's forward (fps convention).
		const glm::vec3 forward = yawPitchForward(yaw, 0.0F);
		const glm::vec3 eye(0.0F, 1.6F, 0.0F);
		const glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3(0.0F, 1.0F, 0.0F));
		const glm::vec4 onScreen = view * glm::vec4(eye + right, 1.0F);
		TEST_ASSERT(onScreen.x > 0.99F, "getRight points to the RIGHT of the screen at yaw " + std::to_string(yaw));
		fx.runtime.shutdown();
	}
	std::filesystem::remove(script);
}

void testCameraBasisMatchesScreen()
{
	// Regression: the editor Viewport camera used cross(up, forward) as
	// "right", which is screen-LEFT - A/D were swapped and middle-mouse pan
	// was inverted on both axes. Check cameraBasis against glm::lookAt.
	for (const float yawDegrees : {0.0F, 40.0F, 90.0F, 180.0F, -125.0F})
	{
		for (const float pitchDegrees : {-60.0F, 0.0F, 35.0F})
		{
			const glm::vec3 forward = yawPitchForward(yawDegrees, pitchDegrees);
			const CameraBasis basis = cameraBasis(forward);
			const glm::vec3 eye(1.0F, 2.0F, 3.0F);
			const glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3(0.0F, 1.0F, 0.0F));
			const glm::vec4 rightOnScreen = view * glm::vec4(eye + basis.right, 1.0F);
			const glm::vec4 upOnScreen = view * glm::vec4(eye + basis.up, 1.0F);
			const std::string where = " at yaw " + std::to_string(yawDegrees) + " pitch " + std::to_string(pitchDegrees);
			TEST_ASSERT(rightOnScreen.x > 0.99F, "camera right is screen-right" + where);
			TEST_ASSERT(upOnScreen.y > 0.99F, "camera up is screen-up" + where);
		}
	}
}

void testThirdPersonMouseUpLooksUp()
{
	// Regression: in third person, mouse up (look pitch > 0, same as first
	// person) swung the camera UP so the view tilted DOWN.
	SceneEntity player;
	player.position = glm::vec3(0.0F);
	const auto viewForwardY = [&](const float lookPitchDegrees)
	{
		const GameCameraState camera = scriptedPlayCamera(player, "third_person", 0.0F, lookPitchDegrees);
		// The renderer's eye is target + distance * dir(yaw, pitch); it looks back at target.
		return -std::sin(camera.pitch);
	};
	TEST_ASSERT(viewForwardY(20.0F) > viewForwardY(0.0F), "third person: looking up tilts the view up");
	TEST_ASSERT(viewForwardY(-20.0F) < viewForwardY(0.0F), "third person: looking down tilts the view down");
	const GameCameraState firstPerson = scriptedPlayCamera(player, "fps", 0.0F, 20.0F);
	TEST_ASSERT(-std::sin(firstPerson.pitch) > 0.0F, "first person: looking up tilts the view up");
}

void testScriptMessagingApi()
{
	const std::string a = "Game/Scripts/test_msg_a.lua";
	const std::string b = "Game/Scripts/test_msg_b.lua";
	writeTextFile(a, R"(-- @property logo image Game/Branding/x.png
local A = {}
function A:on_start() self.got = 0 end
function A:on_update(dt)
	local handled, value = self.entity:send("double_it", 21)
	self.doubled = value or -1
	local h2, v2 = self.world:send("Other", "ping", "hello")
	self.pong = v2 or ""
	local handledDamage, killed = self.world:damage("Other", 500, {crit = true, element = "fire"})
	self.killed = killed and 1 or 0
	self.world:spawnText({x = 0, y = 1, z = 0}, "+10 XP", {r = 1, g = 1, b = 1}, 1.0, 1.0)
	self.world:setHudBar("mana", "Mana", 0.5, {r = 0, g = 0, b = 1}, 3)
	self.world:showMessage("Hi", 2)
	self.world:particle({x = 0, y = 0, z = 0}, {r = 1, g = 0, b = 0}, 0.1, 1)
end
return A
-- @preset Test Kit | player
)");
	writeTextFile(b, R"(local B = {}
function B:double_it(x) return x * 2 end
function B:ping(text) return text .. " back" end
function B:on_damage(amount, attacker, info)
	self.last_element = info and info.element or ""
	self.last_crit = (info and info.crit) and 1 or 0
	return amount >= 100
end
return B
)");
	TEST_ASSERT(ScriptRuntime::cachedScriptPreset(a).name == "Test Kit" &&
			ScriptRuntime::cachedScriptPreset(a).role == "player",
		"@preset tag parsed");
	TEST_ASSERT(ScriptRuntime::cachedScriptPreset(b).name.empty(), "no @preset = not part of a preset");
	TEST_ASSERT(ScriptRuntime::parseScriptProperties(a).front().type == ScriptRuntime::ExposedScriptProperty::Type::Image,
		"image property type parsed");

	SceneFixture fx;
	(void)fx.bus.execute(CreateEntityCommand{"Self"});
	(void)fx.bus.execute(CreateEntityCommand{"Other"});
	(void)fx.bus.execute(AttachScriptCommand{"Self", a});
	(void)fx.bus.execute(AttachScriptCommand{"Self", b});
	(void)fx.bus.execute(AttachScriptCommand{"Other", b});
	fx.start();
	const int selfId = fx.find("Self")->id;
	const int otherId = fx.find("Other")->id;
	fx.runtime.updateEntity(selfId, 0.016F);
	TEST_ASSERT(fx.runtime.getScriptNumberField(selfId, a, "doubled", 0.0F) == 42.0F, "entity:send reaches sibling scripts");
	TEST_ASSERT(fx.runtime.getScriptStringField(selfId, a, "pong", "") == "hello back", "world:send reaches other objects");
	TEST_ASSERT(fx.runtime.getScriptNumberField(selfId, a, "killed", 0.0F) == 1.0F, "damage returns on_damage's result");
	TEST_ASSERT(fx.runtime.getScriptStringField(otherId, b, "last_element", "") == "fire" &&
			fx.runtime.getScriptNumberField(otherId, b, "last_crit", 0.0F) == 1.0F,
		"damage passes its info table");
	TEST_ASSERT(fx.gameplay.floatingTexts.size() == 1 && fx.gameplay.hudBars.size() == 1 &&
			fx.gameplay.messageText == "Hi",
		"spawnText / setHudBar / showMessage");
	TEST_ASSERT(fx.gameplay.flashes.size() == 1 && fx.gameplay.flashes.front().particle, "particle queued");
	tickEffects(fx.gameplay, true, 0.016F);
	TEST_ASSERT(fx.gameplay.flashes.size() == 1, "a particle survives the frame it was made in");
	tickEffects(fx.gameplay, true, 0.016F);
	TEST_ASSERT(fx.gameplay.flashes.empty(), "a particle is gone the next frame");
	fx.runtime.shutdown();
	std::filesystem::remove(a);
	std::filesystem::remove(b);
}

// ----------------------------------------------------------------------------
// End-to-end: the REAL FPS Demo kit scripts (fps_player / items / health / enemy ...)
// (from the source tree) driving a real demo arena for a few hundred frames
// with simulated input - walking, picking up weapons, switching, firing,
// melee. Catches Lua runtime errors the C++ build can't.
// ----------------------------------------------------------------------------
namespace
{
	class ScriptedInput final : public InputSource
	{
	public:
		std::vector<std::string> keysDown;
		std::vector<std::string> keysPressed;
		bool leftMouse = false;
		bool rightMouse = false;
		float mouseDx = 0.0F;

		[[nodiscard]] bool isKeyDown(const std::string& name) const override
		{
			return std::find(keysDown.begin(), keysDown.end(), name) != keysDown.end();
		}
		[[nodiscard]] bool isKeyPressed(const std::string& name) const override
		{
			return std::find(keysPressed.begin(), keysPressed.end(), name) != keysPressed.end();
		}
		[[nodiscard]] float getAxis(const std::string& positive, const std::string& negative) const override
		{
			return (isKeyDown(positive) ? 1.0F : 0.0F) - (isKeyDown(negative) ? 1.0F : 0.0F);
		}
		[[nodiscard]] float getMouseDeltaX() const override { return mouseDx; }
		[[nodiscard]] float getMouseDeltaY() const override { return 0.0F; }
		[[nodiscard]] bool isMouseButtonDown(const std::string& name) const override
		{
			return name == "Left" ? leftMouse : (name == "Right" ? rightMouse : false);
		}
		[[nodiscard]] float getScrollDelta() const override { return 0.0F; }
	};
}

void testFpsPlayerEndToEnd()
{
#ifndef GAMEFORGER_SOURCE_DIR
	std::cout << "  (skipped: GAMEFORGER_SOURCE_DIR not defined)\n";
	return;
#else
	// A fresh project that gets the FPS Demo kit the same way any other
	// project does - by importing the kit folder, nothing else.
	const std::filesystem::path projectRoot = "fps_e2e_project";
	std::filesystem::remove_all(projectRoot);
	const FpsDemoKitImportResult imported =
		importFpsDemoKit(std::filesystem::path(GAMEFORGER_SOURCE_DIR), projectRoot, false);
	TEST_ASSERT(imported.success && imported.filesCopied >= 8, "import the FPS Demo kit: " + imported.message);

	EditorScene scene(projectRoot);
	AICommandBus bus;
	bus.setHandler([&scene](const AIEditorCommand& cmd) { return scene.execute(cmd); });
	FpsRigOptions options;
	options.includeDemoContent = true;
	options.includeGround = true;
	const FpsRigBuildResult rig = buildFpsPlayerRig(scene, bus, options);
	TEST_ASSERT(rig.success, "demo arena builds with the real scripts attached: " + rig.message);

	// A tiny driver the test pokes through a field: grant XP / hurt the player.
	writeTextFile(projectRoot / "Game" / "Scripts" / "test_driver.lua", R"(local D = {}
function D:on_start() self.cmd = "" end
function D:on_update(dt)
	if self.cmd == "xp" then
		self.entity:send("on_weapon_hit", "gun", 5000, true, false, self.entity:getPosition())
	elseif self.cmd == "hurt" then
		self.world:damage(self.entity:getName(), 40)
	end
	self.cmd = ""
end
return D
)");
	TEST_ASSERT(bus.execute(AttachScriptCommand{rig.playerName, "Game/Scripts/test_driver.lua"}).success, "attach driver");
	std::vector<std::string> chasers;
	for (const SceneEntity& entity : scene.entities())
	{
		if (entity.name.rfind("Chaser", 0) == 0)
		{
			chasers.push_back(entity.name);
		}
	}
	TEST_ASSERT(chasers.size() == 2, "demo arena has two chasers");
	for (const std::string& chaser : chasers)
	{
		(void)bus.execute(SetPropertyCommand{chaser, "Entity", "active", false});
	}
	const SceneEntity* managerEntity = nullptr;
	for (const SceneEntity& entity : scene.entities())
	{
		if (hasScript(entity, "Game/Scripts/FPSDemo/game_manager.lua"))
		{
			managerEntity = &entity;
		}
	}
	TEST_ASSERT(managerEntity != nullptr, "demo arena has a Game Manager");
	TEST_ASSERT(ScriptRuntime::scriptPropertyText(*managerEntity, "Game/Scripts/FPSDemo/game_manager.lua", "splash_logo", projectRoot) ==
			"Game/Branding/logo.jpg",
		"Game Manager's splash logo readable without running scripts");

	ScriptedInput input;
	ScriptRuntime runtime;
	GameplayState gameplay;
	ensureInventorySlots(gameplay);
	std::vector<std::string> errors;
	runtime.initialize(
		scene, bus, input,
		[&errors](const bool isError, const std::string& message)
		{
			if (isError)
			{
				errors.push_back(message);
			}
		},
		nullptr, nullptr, nullptr, nullptr, nullptr);
	runtime.setGameplayState(&gameplay);
	for (const SceneEntity& entity : scene.entities())
	{
		for (const std::string& script : entity.scripts)
		{
			TEST_ASSERT(runtime.startScript(entity.id, script, projectRoot), "start " + script + " on " + entity.name);
		}
	}
	TEST_ASSERT(errors.empty(), "no errors starting scripts: " + (errors.empty() ? std::string() : errors.front()));

	const int playerId = scene.findEntity(rig.playerName)->id;
	const std::string playerScript = "Game/Scripts/FPSDemo/fps_player.lua";
	std::size_t mostBeams = 0;
	const auto frames = [&](const int count)
	{
		for (int frame = 0; frame < count; ++frame)
		{
			gameplay.lookPitchDegrees = 0.0F;
			applyParentConstraints(scene, bus);
			tickScripts(scene, runtime, true, 1.0F / 60.0F);
			applyParentConstraints(scene, bus);
			tickProjectiles(scene, bus, gameplay, true, 1.0F / 60.0F);
			mostBeams = std::max(mostBeams, gameplay.beams.size());
			tickEffects(gameplay, true, 1.0F / 60.0F);
			input.keysPressed.clear();
		}
	};

	// Walk forward a little, look around.
	input.keysDown = {"W"};
	input.mouseDx = 3.0F;
	frames(30);
	input.keysDown.clear();
	input.mouseDx = 0.0F;
	TEST_ASSERT(errors.empty(), "no errors while walking: " + (errors.empty() ? std::string() : errors.front()));
	TEST_ASSERT(runtime.hasActiveCamera() && runtime.activeCameraEntityId() == playerId, "player claimed the camera");
	TEST_ASSERT(runtime.getScriptStringField(playerId, playerScript, "hud_text", "").rfind("Fists", 0) == 0,
		"starts with fists");
	const auto hasHudBar = [&gameplay](const std::string& id)
	{
		return std::any_of(gameplay.hudBars.begin(), gameplay.hudBars.end(), [&id](const auto& bar) { return bar.id == id; });
	};
	TEST_ASSERT(hasHudBar("health") && hasHudBar("xp"), "player health and XP bars are on the HUD");

	// Pick up every weapon pickup (as the E key would).
	for (const std::string& weaponName : {"Sword", "Axe", "War Hammer", "Pickaxe", "Pistol", "AK-47", "Taser", "Storm Caster"})
	{
		const SceneEntity* pickup = scene.findEntity(weaponName + " Pickup");
		TEST_ASSERT(pickup != nullptr, "pickup exists: " + weaponName);
		TEST_ASSERT(pickUpItem(scene, bus, gameplay, runtime, *pickup), "pick up " + weaponName);
	}
	TEST_ASSERT(gameplay.inventoryItems[5].weapon == "ak47", "AK-47 went into hotbar slot 6");

	// Punch, then each weapon in turn: select its hotbar key, fire/swing.
	input.leftMouse = true;
	frames(20);
	input.leftMouse = false;
	frames(10);
	for (int slot = 1; slot <= 8; ++slot)
	{
		input.keysPressed = {std::to_string(slot)};
		frames(30); // equip animation
		input.leftMouse = true;
		frames(45);
		input.leftMouse = false;
		frames(20);
		TEST_ASSERT(errors.empty(),
			"no script errors using slot " + std::to_string(slot) + ": " + (errors.empty() ? std::string() : errors.front()));
		const std::string& expected = gameplay.inventoryItems[static_cast<std::size_t>(slot - 1)].weapon;
		const SceneEntity* weaponNode = scene.findEntity(rig.rigName + ".W." + expected);
		TEST_ASSERT(weaponNode != nullptr && weaponNode->active, "equipped weapon model is shown: " + expected);
	}
	TEST_ASSERT(mostBeams > 0, "guns/taser/lightning produced beams");

	// Reload the AK and aim down sights.
	input.keysPressed = {"6"};
	frames(30);
	input.keysPressed = {"R"};
	frames(5);
	TEST_ASSERT(runtime.getScriptStringField(playerId, playerScript, "hud_text", "").find("RELOADING") != std::string::npos,
		"R starts a reload");
	frames(150);
	{
		// "AK-47  Lv N   <ammo> / <max>" - full after the reload (max grows with weapon level).
		const std::string hud = runtime.getScriptStringField(playerId, playerScript, "hud_text", "");
		const std::size_t slash = hud.find(" / ");
		TEST_ASSERT(slash != std::string::npos, "AK hud shows ammo: " + hud);
		const std::size_t ammoStart = hud.rfind(' ', slash - 1) + 1;
		const int ammo = std::stoi(hud.substr(ammoStart, slash - ammoStart));
		const int maxAmmo = std::stoi(hud.substr(slash + 3));
		TEST_ASSERT(ammo == maxAmmo && maxAmmo >= 30, "reload refills the magazine: " + hud);
	}
	input.rightMouse = true;
	frames(30);
	TEST_ASSERT(runtime.getScriptNumberField(playerId, playerScript, "ads", 0.0F) > 0.9F, "right mouse aims down sights");
	input.rightMouse = false;

	// Melee a dummy: stand in front of it with the sword.
	const SceneEntity* dummy = nullptr;
	for (const SceneEntity& entity : scene.entities())
	{
		if (entity.name.rfind("Training Dummy", 0) == 0)
		{
			dummy = &entity;
			break;
		}
	}
	TEST_ASSERT(dummy != nullptr, "a training dummy exists");
	const std::string dummyName = dummy->name;
	const glm::vec3 dummyPosition = dummy->position;
	(void)bus.execute(SetPropertyCommand{rig.playerName, "Transform", "position",
		glm::vec3(dummyPosition.x, 0.0F, dummyPosition.z - 1.6F)});
	(void)bus.execute(SetPropertyCommand{rig.playerName, "Transform", "rotation", glm::vec3(0.0F)});
	input.keysPressed = {"1"};
	frames(30);
	const float healthBefore = runtime.getScriptNumberField(scene.findEntity(dummyName)->id, "Game/Scripts/FPSDemo/health.lua", "health", -1.0F);
	input.leftMouse = true;
	frames(40);
	input.leftMouse = false;
	frames(10);
	const float healthAfter = runtime.getScriptNumberField(scene.findEntity(dummyName)->id, "Game/Scripts/FPSDemo/health.lua", "health", -1.0F);
	TEST_ASSERT(healthBefore > 0.0F && healthAfter < healthBefore, "the sword damages the dummy in front of you");

	const std::string driver = "Game/Scripts/test_driver.lua";
	const std::string health = "Game/Scripts/FPSDemo/health.lua";
	const std::string xpScript = "Game/Scripts/FPSDemo/xp_system.lua";
	const auto slotOf = [&gameplay](const std::string& weapon)
	{
		for (int index = 0; index < static_cast<int>(gameplay.inventoryItems.size()); ++index)
		{
			if (gameplay.inventoryItems[static_cast<std::size_t>(index)].weapon == weapon)
			{
				return index;
			}
		}
		return -1;
	};

	// XP: a big batch of XP levels the player up and unlocks every caster.
	TEST_ASSERT(slotOf("fire") < 0, "Fire Caster starts locked");
	runtime.setScriptStringField(playerId, driver, "cmd", "xp");
	frames(3);
	TEST_ASSERT(runtime.getScriptNumberField(playerId, xpScript, "level", 0.0F) >= 4.0F, "XP levels the player up");
	TEST_ASSERT(slotOf("fire") >= 0 && slotOf("frost") >= 0 && slotOf("heal") >= 0,
		"level-ups unlock the Fire, Frost and Life Casters into the inventory");

	const auto aimAt = [&](const std::string& targetName, const float distance)
	{
		const glm::vec3 target = scene.findEntity(targetName)->position;
		(void)bus.execute(SetPropertyCommand{rig.playerName, "Transform", "position",
			glm::vec3(target.x, 0.0F, target.z - distance)});
		(void)bus.execute(SetPropertyCommand{rig.playerName, "Transform", "rotation", glm::vec3(0.0F)});
	};
	const auto castOnce = [&](const std::string& weapon)
	{
		gameplay.selectedSlot = slotOf(weapon);
		frames(30); // equip
		input.leftMouse = true;
		frames(2);
		input.leftMouse = false;
	};

	// Fire Caster: the fireball flies, explodes, sets the dummy on fire.
	std::string otherDummy;
	for (const SceneEntity& entity : scene.entities())
	{
		if (entity.name.rfind("Training Dummy", 0) == 0 && entity.name != dummyName)
		{
			otherDummy = entity.name;
		}
	}
	TEST_ASSERT(!otherDummy.empty(), "a second dummy exists");
	aimAt(otherDummy, 7.0F);
	const int otherDummyId = scene.findEntity(otherDummy)->id;
	const float fireBefore = runtime.getScriptNumberField(otherDummyId, health, "health", -1.0F);
	std::size_t mostParticles = 0;
	bool sawBurning = false;
	castOnce("fire");
	for (int frame = 0; frame < 60; ++frame)
	{
		frames(1);
		mostParticles = std::max<std::size_t>(mostParticles, static_cast<std::size_t>(std::count_if(gameplay.flashes.begin(),
			gameplay.flashes.end(), [](const auto& flash) { return flash.particle; })));
		sawBurning = sawBurning || runtime.getScriptNumberField(otherDummyId, health, "burning", 0.0F) > 0.5F;
	}
	TEST_ASSERT(runtime.getScriptNumberField(otherDummyId, health, "health", -1.0F) < fireBefore,
		"the fireball damages the dummy");
	TEST_ASSERT(sawBurning, "the fireball sets the dummy on fire");
	TEST_ASSERT(mostParticles > 20, "effects.lua draws particles for the explosion");
	TEST_ASSERT(!gameplay.floatingTexts.empty() || mostParticles > 0, "damage numbers / effects appear");

	// Frost Caster: the ice shard freezes what it hits.
	aimAt(dummyName, 6.0F);
	const int dummyId = scene.findEntity(dummyName)->id;
	bool sawFrozen = false;
	castOnce("frost");
	for (int frame = 0; frame < 45 && !sawFrozen; ++frame)
	{
		frames(1);
		sawFrozen = runtime.getScriptNumberField(dummyId, health, "frozen", 0.0F) > 0.5F;
	}
	TEST_ASSERT(sawFrozen, "the frost shard freezes the dummy");

	// Life Caster heals the player.
	runtime.setScriptStringField(playerId, driver, "cmd", "hurt");
	frames(2);
	const float hurtHealth = runtime.getScriptNumberField(playerId, health, "health", -1.0F);
	TEST_ASSERT(hurtHealth > 0.0F && hurtHealth <= 60.5F, "the player can be hurt");
	castOnce("heal");
	frames(5);
	TEST_ASSERT(runtime.getScriptNumberField(playerId, health, "health", -1.0F) > hurtHealth, "the Life Caster heals you");

	// Chasers hit back.
	const float beforeChaser = runtime.getScriptNumberField(playerId, health, "health", -1.0F);
	const glm::vec3 playerFeet = scene.findEntity(rig.playerName)->position;
	(void)bus.execute(SetPropertyCommand{chasers.front(), "Transform", "position", playerFeet + glm::vec3(1.0F, 0.95F, 1.0F)});
	(void)bus.execute(SetPropertyCommand{chasers.front(), "Entity", "active", true});
	frames(120);
	TEST_ASSERT(runtime.getScriptNumberField(playerId, health, "health", -1.0F) < beforeChaser, "the chaser damages the player");

	// Third person hides the hands rig.
	input.keysPressed = {"C"};
	frames(2);
	TEST_ASSERT(!scene.findEntity(rig.rigName)->active, "third-person hides the first-person rig");

	TEST_ASSERT(errors.empty(), "no script errors at all: " + (errors.empty() ? std::string() : errors.front()));
	runtime.shutdown();
	std::filesystem::remove_all(projectRoot);
#endif
}

// ----------------------------------------------------------------------------
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
	RUN_TEST(testGameObjectComponentModel);
	RUN_TEST(testAssetDatabase);
	RUN_TEST(testMaterialSerialization);
	RUN_TEST(testScriptPropertyReflection);
	RUN_TEST(testScriptPropertyOverrides);
	RUN_TEST(testScriptPropertySerialization);
	RUN_TEST(testRenameKeepsChildrenAttached);
	RUN_TEST(testActiveInHierarchy);
	RUN_TEST(testInventorySlotsAndStacking);
	RUN_TEST(testItemPickupHideAndDrop);
	RUN_TEST(testFpsRigBuilder);
	RUN_TEST(testWeaponLuaApi);
	RUN_TEST(testGetRightMatchesScreenRight);
	RUN_TEST(testCameraBasisMatchesScreen);
	RUN_TEST(testThirdPersonMouseUpLooksUp);
	RUN_TEST(testScriptMessagingApi);
	RUN_TEST(testFpsPlayerEndToEnd);

	std::cout << "====================================================\n";
	std::cout << " Tests Passed: " << g_testsPassed << " | Tests Failed: " << g_testsFailed << "\n";
	std::cout << "====================================================\n";

	return g_testsFailed == 0 ? 0 : 1;
}
