#include "GameForger/Editor/FpsRigBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* kFpsPlayerScript = fpsdemo::kPlayer;
		constexpr const char* kItemScript = fpsdemo::kItems;
		constexpr const char* kHealthScript = fpsdemo::kHealth;
		constexpr const char* kEnemyAiScript = fpsdemo::kEnemy;
		constexpr const char* kGameManagerScript = fpsdemo::kGameManager;
		constexpr const char* kClimbableScript = fpsdemo::kClimbable;

		// One primitive of a hand or weapon model, in its group's local
		// space. `scale` is the primitive's half-extent (primitives are
		// authored in a [-1,1] box), rotation is degrees (XYZ).
		struct PartSpec
		{
			const char* suffix;
			PrimitiveType primitive;
			glm::vec3 position;
			glm::vec3 rotation;
			glm::vec3 scale;
			glm::vec3 color;
		};

		// Palette.
		constexpr glm::vec3 kSkin{0.86F, 0.66F, 0.52F};
		constexpr glm::vec3 kGlove{0.16F, 0.17F, 0.19F};
		constexpr glm::vec3 kSleeve{0.20F, 0.26F, 0.20F};
		constexpr glm::vec3 kSteel{0.78F, 0.80F, 0.84F};
		constexpr glm::vec3 kDarkSteel{0.34F, 0.35F, 0.38F};
		constexpr glm::vec3 kGunMetal{0.13F, 0.13F, 0.15F};
		constexpr glm::vec3 kWood{0.50F, 0.30F, 0.14F};
		constexpr glm::vec3 kDarkWood{0.34F, 0.19F, 0.08F};
		constexpr glm::vec3 kLeather{0.26F, 0.15F, 0.08F};
		constexpr glm::vec3 kGold{0.86F, 0.68F, 0.22F};
		constexpr glm::vec3 kTaserYellow{0.96F, 0.80F, 0.10F};
		constexpr glm::vec3 kArcBlue{0.45F, 0.78F, 1.00F};
		constexpr glm::vec3 kStormPurple{0.30F, 0.20F, 0.45F};
		constexpr glm::vec3 kCopper{0.72F, 0.38F, 0.18F};
		constexpr glm::vec3 kSilver{0.82F, 0.86F, 0.92F};
		constexpr glm::vec3 kPants{0.22F, 0.24F, 0.30F};
		constexpr glm::vec3 kHair{0.18F, 0.12F, 0.08F};
		constexpr glm::vec3 kPack{0.36F, 0.29F, 0.18F};
		constexpr glm::vec3 kBelt{0.10F, 0.10F, 0.11F};
		constexpr glm::vec3 kEye{0.08F, 0.08F, 0.10F};

		// Hips height of the third-person body - fps_player.lua's
		// BODY_HIPS_HEIGHT must match.
		constexpr float kBodyHipsHeight = 0.93F;

		// A right fist closed around a vertical handle running along Y
		// through the origin (so every weapon's grip sits at 0,0,0).
		std::vector<PartSpec> handParts()
		{
			const float fingerX = 0.008F;
			return {
				{"Back", PrimitiveType::Cube, {0.030F, 0.000F, -0.018F}, {0.0F, 0.0F, 0.0F}, {0.020F, 0.046F, 0.034F}, kSkin},
				{"Index", PrimitiveType::Capsule, {fingerX, 0.033F, 0.020F}, {0.0F, 0.0F, 90.0F}, {0.0115F, 0.026F, 0.0115F}, kSkin},
				{"Middle", PrimitiveType::Capsule, {fingerX, 0.011F, 0.021F}, {0.0F, 0.0F, 90.0F}, {0.0120F, 0.027F, 0.0120F}, kSkin},
				{"Ring", PrimitiveType::Capsule, {fingerX, -0.011F, 0.020F}, {0.0F, 0.0F, 90.0F}, {0.0115F, 0.026F, 0.0115F}, kSkin},
				{"Pinky", PrimitiveType::Capsule, {fingerX + 0.002F, -0.032F, 0.017F}, {0.0F, 0.0F, 90.0F}, {0.0100F, 0.022F, 0.0100F}, kSkin},
				{"Thumb", PrimitiveType::Capsule, {-0.012F, 0.036F, 0.004F}, {20.0F, 0.0F, 65.0F}, {0.0120F, 0.024F, 0.0120F}, kSkin},
				{"Cuff", PrimitiveType::Cylinder, {0.034F, -0.030F, -0.060F}, {73.0F, 0.0F, 0.0F}, {0.037F, 0.022F, 0.037F}, kGlove},
				{"Sleeve", PrimitiveType::Cylinder, {0.042F, -0.085F, -0.200F}, {73.0F, 0.0F, 0.0F}, {0.036F, 0.120F, 0.036F}, kSleeve},
			};
		}

		// Mirror a part list across X (right hand <-> left hand).
		//
		// Everything above is authored with +X = the viewer's RIGHT. The game
		// camera (glm::lookAt, looking down +Z) shows +X on screen-LEFT, so
		// every first-person part is mirrored once more when it goes into
		// the rig - see toRig() below and fps_player.lua's to_rig().
		std::vector<PartSpec> mirrored(std::vector<PartSpec> parts)
		{
			for (PartSpec& part : parts)
			{
				part.position.x = -part.position.x;
				part.rotation.y = -part.rotation.y;
				part.rotation.z = -part.rotation.z;
			}
			return parts;
		}

		// Weapons: grip at the origin along Y (melee: blade/head up, tilted
		// forward by the group's own rotation; guns: barrel along +Z).
		struct WeaponSpec
		{
			const char* id;
			const char* displayName;
			const char* icon;
			glm::vec3 groupRotation; // rest tilt inside the hand
			std::vector<PartSpec> parts;
			glm::vec3 muzzle;        // only meaningful when hasMuzzle
			bool hasMuzzle;
			// Viewmodel only - long guns read smaller/further than their
			// real size so they don't fill the screen.
			float viewScale = 1.0F;
			glm::vec3 viewOffset{0.0F};
			// Two-handed weapons carry their own left (support) hand as part
			// of the weapon model, so it follows every swing/recoil exactly.
			bool hasSupportHand = false;
			glm::vec3 supportPosition{0.0F};
			glm::vec3 supportRotation{0.0F};
			bool supportSleeve = true;
		};

		std::vector<WeaponSpec> weaponSpecs()
		{
			std::vector<WeaponSpec> weapons;
			weapons.push_back({"sword", "Sword", "Game/Icons/FPSDemo/Sword.png", {42.0F, 0.0F, 16.0F},
				{
					{"Grip", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.055F, 0.012F}, kLeather},
					{"Pommel", PrimitiveType::Sphere, {0.0F, -0.063F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.017F, 0.017F, 0.017F}, kGold},
					{"Guard", PrimitiveType::Cube, {0.0F, 0.062F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.065F, 0.009F, 0.015F}, kGold},
					{"Blade", PrimitiveType::Cube, {0.0F, 0.335F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.019F, 0.265F, 0.0045F}, kSteel},
					{"Fuller", PrimitiveType::Cube, {0.0F, 0.30F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.005F, 0.20F, 0.0050F}, kDarkSteel},
					{"Tip", PrimitiveType::Cone, {0.0F, 0.635F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.019F, 0.035F, 0.0045F}, kSteel},
				},
				{}, false, 0.8F});
			weapons.push_back({"axe", "Axe", "Game/Icons/FPSDemo/Axe.png", {36.0F, 0.0F, 12.0F},
				{
					{"Handle", PrimitiveType::Cylinder, {0.0F, 0.14F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.013F, 0.21F, 0.013F}, kWood},
					{"Cap", PrimitiveType::Cylinder, {0.0F, -0.075F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.016F, 0.008F, 0.016F}, kDarkSteel},
					{"Head", PrimitiveType::Cube, {0.045F, 0.315F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.045F, 0.040F, 0.008F}, kDarkSteel},
					{"Edge", PrimitiveType::Cube, {0.094F, 0.315F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.007F, 0.058F, 0.005F}, kSteel},
					{"Poll", PrimitiveType::Cube, {-0.022F, 0.315F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.014F, 0.022F, 0.011F}, kDarkSteel},
				},
				{}, false, 0.85F});
			weapons.push_back({"hammer", "War Hammer", "Game/Icons/FPSDemo/Hammer.png", {36.0F, 0.0F, 10.0F},
				{
					{"Handle", PrimitiveType::Cylinder, {0.0F, 0.15F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.014F, 0.23F, 0.014F}, kDarkWood},
					{"Wrap", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.016F, 0.06F, 0.016F}, kLeather},
					{"Head", PrimitiveType::Cube, {0.0F, 0.39F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.085F, 0.048F, 0.048F}, kDarkSteel},
					{"FaceL", PrimitiveType::Cylinder, {-0.09F, 0.39F, 0.0F}, {0.0F, 0.0F, 90.0F}, {0.050F, 0.010F, 0.050F}, kSteel},
					{"FaceR", PrimitiveType::Cylinder, {0.09F, 0.39F, 0.0F}, {0.0F, 0.0F, 90.0F}, {0.050F, 0.010F, 0.050F}, kSteel},
				},
				{}, false, 0.78F, {0.0F, 0.0F, 0.0F}, true, {0.0F, 0.14F, 0.0F}, {0.0F, 0.0F, 0.0F}, false});
			weapons.push_back({"pickaxe", "Pickaxe", "Game/Icons/FPSDemo/Pickaxe.png", {36.0F, 0.0F, 10.0F},
				{
					{"Handle", PrimitiveType::Cylinder, {0.0F, 0.15F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.23F, 0.012F}, kWood},
					{"Socket", PrimitiveType::Cube, {0.0F, 0.37F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.022F, 0.024F, 0.020F}, kDarkSteel},
					{"PickL", PrimitiveType::Cone, {-0.095F, 0.355F, 0.0F}, {0.0F, 0.0F, 100.0F}, {0.015F, 0.080F, 0.012F}, kSteel},
					{"PickR", PrimitiveType::Cone, {0.095F, 0.355F, 0.0F}, {0.0F, 0.0F, -100.0F}, {0.015F, 0.080F, 0.012F}, kSteel},
				},
				{}, false, 0.78F, {0.0F, 0.0F, 0.0F}, true, {0.0F, 0.14F, 0.0F}, {0.0F, 0.0F, 0.0F}, false});
			weapons.push_back({"gun", "Pistol", "Game/Icons/FPSDemo/Pistol.png", {0.0F, 0.0F, 0.0F},
				{
					{"Grip", PrimitiveType::Cube, {0.0F, -0.005F, -0.004F}, {-14.0F, 0.0F, 0.0F}, {0.014F, 0.045F, 0.021F}, kGunMetal},
					{"Frame", PrimitiveType::Cube, {0.0F, 0.043F, 0.040F}, {0.0F, 0.0F, 0.0F}, {0.0135F, 0.011F, 0.060F}, kGunMetal},
					{"Slide", PrimitiveType::Cube, {0.0F, 0.064F, 0.048F}, {0.0F, 0.0F, 0.0F}, {0.0145F, 0.012F, 0.078F}, {0.24F, 0.24F, 0.26F}},
					{"Barrel", PrimitiveType::Cylinder, {0.0F, 0.064F, 0.130F}, {90.0F, 0.0F, 0.0F}, {0.0065F, 0.008F, 0.0065F}, kGunMetal},
					{"Guard", PrimitiveType::Cube, {0.0F, 0.016F, 0.030F}, {0.0F, 0.0F, 0.0F}, {0.004F, 0.012F, 0.018F}, kGunMetal},
					{"SightF", PrimitiveType::Cube, {0.0F, 0.079F, 0.118F}, {0.0F, 0.0F, 0.0F}, {0.002F, 0.004F, 0.003F}, {0.9F, 0.9F, 0.9F}},
					{"SightR", PrimitiveType::Cube, {0.0F, 0.079F, -0.022F}, {0.0F, 0.0F, 0.0F}, {0.008F, 0.004F, 0.003F}, kGunMetal},
				},
				{0.0F, 0.064F, 0.142F}, true});
			weapons.push_back({"ak47", "AK-47", "Game/Icons/FPSDemo/AK47.png", {0.0F, 0.0F, 0.0F},
				{
					{"PistolGrip", PrimitiveType::Cube, {0.0F, -0.008F, -0.002F}, {-18.0F, 0.0F, 0.0F}, {0.014F, 0.042F, 0.019F}, kDarkWood},
					{"Receiver", PrimitiveType::Cube, {0.0F, 0.052F, 0.080F}, {0.0F, 0.0F, 0.0F}, {0.021F, 0.030F, 0.150F}, kGunMetal},
					{"Cover", PrimitiveType::Cube, {0.0F, 0.086F, 0.070F}, {0.0F, 0.0F, 0.0F}, {0.018F, 0.006F, 0.120F}, {0.22F, 0.22F, 0.24F}},
					{"Handguard", PrimitiveType::Cube, {0.0F, 0.048F, 0.300F}, {0.0F, 0.0F, 0.0F}, {0.023F, 0.026F, 0.075F}, kWood},
					{"GasTube", PrimitiveType::Cylinder, {0.0F, 0.083F, 0.300F}, {90.0F, 0.0F, 0.0F}, {0.009F, 0.075F, 0.009F}, kWood},
					{"Barrel", PrimitiveType::Cylinder, {0.0F, 0.058F, 0.460F}, {90.0F, 0.0F, 0.0F}, {0.008F, 0.090F, 0.008F}, kGunMetal},
					{"FrontSight", PrimitiveType::Cube, {0.0F, 0.080F, 0.520F}, {0.0F, 0.0F, 0.0F}, {0.004F, 0.014F, 0.005F}, kGunMetal},
					{"MuzzleBrake", PrimitiveType::Cylinder, {0.0F, 0.058F, 0.560F}, {90.0F, 0.0F, 0.0F}, {0.011F, 0.014F, 0.011F}, kGunMetal},
					{"MagTop", PrimitiveType::Cube, {0.0F, -0.010F, 0.135F}, {12.0F, 0.0F, 0.0F}, {0.014F, 0.040F, 0.026F}, {0.30F, 0.18F, 0.08F}},
					{"MagBottom", PrimitiveType::Cube, {0.0F, -0.072F, 0.160F}, {32.0F, 0.0F, 0.0F}, {0.014F, 0.036F, 0.025F}, {0.30F, 0.18F, 0.08F}},
					{"Stock", PrimitiveType::Cube, {0.0F, 0.030F, -0.140F}, {8.0F, 0.0F, 0.0F}, {0.018F, 0.036F, 0.110F}, kWood},
					{"ButtPlate", PrimitiveType::Cube, {0.0F, 0.018F, -0.252F}, {8.0F, 0.0F, 0.0F}, {0.019F, 0.040F, 0.006F}, kGunMetal},
				},
				{0.0F, 0.058F, 0.578F}, true, 0.8F, {0.0F, 0.0F, 0.08F}, true, {0.0F, 0.020F, 0.300F},
				{90.0F, 0.0F, 0.0F}, false});
			weapons.push_back({"taser", "Taser", "Game/Icons/FPSDemo/Taser.png", {0.0F, 0.0F, 0.0F},
				{
					{"Grip", PrimitiveType::Cube, {0.0F, -0.004F, -0.004F}, {-14.0F, 0.0F, 0.0F}, {0.015F, 0.044F, 0.021F}, kGunMetal},
					{"Body", PrimitiveType::Cube, {0.0F, 0.052F, 0.050F}, {0.0F, 0.0F, 0.0F}, {0.020F, 0.024F, 0.068F}, kTaserYellow},
					{"Stripe", PrimitiveType::Cube, {0.0F, 0.052F, 0.050F}, {0.0F, 0.0F, 0.0F}, {0.0205F, 0.006F, 0.060F}, kGunMetal},
					{"Cartridge", PrimitiveType::Cube, {0.0F, 0.050F, 0.132F}, {0.0F, 0.0F, 0.0F}, {0.022F, 0.022F, 0.016F}, kGunMetal},
					{"ProngL", PrimitiveType::Cube, {-0.010F, 0.050F, 0.152F}, {0.0F, 0.0F, 0.0F}, {0.003F, 0.003F, 0.006F}, kSteel},
					{"ProngR", PrimitiveType::Cube, {0.010F, 0.050F, 0.152F}, {0.0F, 0.0F, 0.0F}, {0.003F, 0.003F, 0.006F}, kSteel},
					{"Laser", PrimitiveType::Cube, {0.0F, 0.024F, 0.100F}, {0.0F, 0.0F, 0.0F}, {0.006F, 0.005F, 0.018F}, {0.8F, 0.1F, 0.1F}},
				},
				{0.0F, 0.050F, 0.160F}, true});
			weapons.push_back({"lightning", "Storm Caster", "Game/Icons/FPSDemo/ChainLightning.png", {10.0F, 0.0F, 0.0F},
				{
					{"Rod", PrimitiveType::Cylinder, {0.0F, 0.10F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.17F, 0.012F}, kStormPurple},
					{"Wrap", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.015F, 0.05F, 0.015F}, kGold},
					{"Collar", PrimitiveType::Cylinder, {0.0F, 0.275F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.024F, 0.010F, 0.024F}, kGold},
					{"Orb", PrimitiveType::Sphere, {0.0F, 0.325F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.036F, 0.036F, 0.036F}, kArcBlue},
					{"ClawA", PrimitiveType::Cone, {0.030F, 0.315F, 0.0F}, {0.0F, 0.0F, -25.0F}, {0.006F, 0.040F, 0.006F}, kGold},
					{"ClawB", PrimitiveType::Cone, {-0.015F, 0.315F, 0.026F}, {25.0F, 0.0F, 12.0F}, {0.006F, 0.040F, 0.006F}, kGold},
					{"ClawC", PrimitiveType::Cone, {-0.015F, 0.315F, -0.026F}, {-25.0F, 0.0F, 12.0F}, {0.006F, 0.040F, 0.006F}, kGold},
				},
				{0.0F, 0.325F, 0.0F}, true});

			// The three casters xp_system.lua unlocks - the Storm Caster's
			// shape, recolored per element.
			const auto caster = [](const glm::vec3& rod, const glm::vec3& metal, const glm::vec3& orb)
			{
				return std::vector<PartSpec>{
					{"Rod", PrimitiveType::Cylinder, {0.0F, 0.10F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.17F, 0.012F}, rod},
					{"Wrap", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.015F, 0.05F, 0.015F}, metal},
					{"Collar", PrimitiveType::Cylinder, {0.0F, 0.275F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.024F, 0.010F, 0.024F}, metal},
					{"Orb", PrimitiveType::Sphere, {0.0F, 0.325F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.036F, 0.036F, 0.036F}, orb},
					{"ClawA", PrimitiveType::Cone, {0.030F, 0.315F, 0.0F}, {0.0F, 0.0F, -25.0F}, {0.006F, 0.040F, 0.006F}, metal},
					{"ClawB", PrimitiveType::Cone, {-0.015F, 0.315F, 0.026F}, {25.0F, 0.0F, 12.0F}, {0.006F, 0.040F, 0.006F}, metal},
					{"ClawC", PrimitiveType::Cone, {-0.015F, 0.315F, -0.026F}, {-25.0F, 0.0F, 12.0F}, {0.006F, 0.040F, 0.006F}, metal},
				};
			};
			weapons.push_back({"fire", "Fire Caster", "Game/Icons/FPSDemo/FireCaster.png", {10.0F, 0.0F, 0.0F},
				caster({0.38F, 0.09F, 0.06F}, kCopper, {1.0F, 0.42F, 0.08F}), {0.0F, 0.325F, 0.0F}, true});
			weapons.push_back({"frost", "Frost Caster", "Game/Icons/FPSDemo/FrostCaster.png", {10.0F, 0.0F, 0.0F},
				caster({0.70F, 0.82F, 0.95F}, kSilver, {0.55F, 0.85F, 1.0F}), {0.0F, 0.325F, 0.0F}, true});
			weapons.push_back({"heal", "Life Caster", "Game/Icons/FPSDemo/LifeCaster.png", {10.0F, 0.0F, 0.0F},
				caster({0.26F, 0.42F, 0.18F}, kGold, {0.40F, 1.0F, 0.50F}), {0.0F, 0.325F, 0.0F}, true});
			return weapons;
		}

		// Casters the player has to unlock with XP (xp_system.lua) - their
		// demo pickups start hidden and are granted on level-up.
		bool isXpUnlock(const std::string& id)
		{
			return id == "fire" || id == "frost" || id == "heal";
		}

		class Builder
		{
		public:
			Builder(EditorScene& scene, AICommandBus& commandBus) : scene_(scene), commandBus_(commandBus) {}

			std::string uniqueName(const std::string& baseName) const
			{
				std::string candidate = baseName;
				for (int suffix = 1; scene_.findEntity(candidate) != nullptr; ++suffix)
				{
					candidate = baseName + " (" + std::to_string(suffix) + ")";
				}
				return candidate;
			}

			bool create(const std::string& name, const PrimitiveType primitive, const glm::vec3& position)
			{
				CreateEntityCommand command;
				command.name = name;
				command.primitive = primitive;
				command.position = position;
				if (!commandBus_.execute(command).success)
				{
					failures_ += 1;
					return false;
				}
				created_ += 1;
				return true;
			}

			void set(const std::string& name, const char* component, const char* property, EditableValue value)
			{
				if (!commandBus_.execute(SetPropertyCommand{name, component, property, std::move(value)}).success)
				{
					failures_ += 1;
				}
			}

			void tag(const std::string& name, const std::string& tagName)
			{
				(void)commandBus_.execute(AddTagCommand{name, tagName});
			}

			// Attaching needs the .lua file on disk - a missing one is
			// reported, not fatal (the rig itself is still useful).
			void attach(const std::string& name, const std::string& scriptPath)
			{
				if (!commandBus_.execute(AttachScriptCommand{name, scriptPath}).success)
				{
					missingScripts_ += 1;
				}
			}

			void scriptProperty(const std::string& name, const std::string& scriptPath, const std::string& property,
				const std::string& value)
			{
				set(name, "ScriptProperty", (scriptPath + "#" + property).c_str(), value);
			}

			// Transform-only group node (not drawn - see the "Empty" tag).
			void group(const std::string& name, const std::string& parent, const glm::vec3& localPosition,
				const glm::vec3& localRotation, const glm::vec3& localScale, const bool viewmodel)
			{
				if (!create(name, PrimitiveType::Cube, localPosition))
				{
					return;
				}
				tag(name, "Empty");
				if (viewmodel)
				{
					tag(name, ignoreTag_);
				}
				if (!parent.empty())
				{
					set(name, "Parent", "parentName", parent);
					set(name, "Parent", "localPosition", localPosition);
					set(name, "Parent", "localRotation", localRotation);
					set(name, "Parent", "localScale", localScale);
				}
				else
				{
					set(name, "Transform", "rotation", localRotation);
					set(name, "Transform", "scale", localScale);
				}
			}

			void parts(const std::string& parent, const std::vector<PartSpec>& specs, const bool viewmodel)
			{
				for (const PartSpec& spec : specs)
				{
					const std::string name = parent + "." + spec.suffix;
					if (!create(name, spec.primitive, spec.position))
					{
						continue;
					}
					set(name, "Parent", "parentName", parent);
					set(name, "Parent", "localPosition", spec.position);
					set(name, "Parent", "localRotation", spec.rotation);
					set(name, "Parent", "localScale", spec.scale);
					set(name, "Renderer", "color", spec.color);
					if (viewmodel)
					{
						tag(name, ignoreTag_);
					}
				}
			}

			// The tag `viewmodel` parts get: "Viewmodel" for the player's own
			// models, "NoRaycast" for enemy models - both make raycasts and
			// weapon hits ignore the part (hits land on the enemy's capsule).
			void setIgnoreTag(std::string tagName) { ignoreTag_ = std::move(tagName); }

			[[nodiscard]] int created() const noexcept { return created_; }
			[[nodiscard]] int failures() const noexcept { return failures_; }
			[[nodiscard]] int missingScripts() const noexcept { return missingScripts_; }

		private:
			EditorScene& scene_;
			AICommandBus& commandBus_;
			int created_ = 0;
			int failures_ = 0;
			int missingScripts_ = 0;
			std::string ignoreTag_ = "Viewmodel";
		};

		// Scales a part list (positions and sizes) by `size`, and widens it
		// (X/Z sizes) by `bulk`.
		std::vector<PartSpec> sized(std::vector<PartSpec> parts, const float size, const float bulk = 1.0F)
		{
			for (PartSpec& part : parts)
			{
				part.position *= size;
				part.scale *= size;
				part.scale.x *= bulk;
				part.scale.z *= bulk;
			}
			return parts;
		}

		enum class HeadStyle
		{
			Human,
			Goblin,
			Orc
		};

		// How a humanoid body looks. The player, the goblins and the orc all
		// use the same jointed body (so the same animations fit them all).
		struct HumanoidLook
		{
			float size = 1.0F;  // 1 = the player's 1.8 m
			float bulk = 1.0F;  // wider torso and limbs (the orc)
			glm::vec3 skin = kSkin;
			glm::vec3 shirt = kSleeve;
			glm::vec3 pants = kPants;
			glm::vec3 boots = kLeather;
			glm::vec3 hands = kGlove;
			glm::vec3 hair = kHair;
			glm::vec3 eyes = kEye;
			HeadStyle head = HeadStyle::Human;
			bool backpack = true;
			bool bareArms = false;
			bool bareChest = false;
		};

		// Palette for the monsters.
		constexpr glm::vec3 kGoblinSkin{0.36F, 0.56F, 0.22F};
		constexpr glm::vec3 kGoblinVest{0.42F, 0.29F, 0.16F};
		constexpr glm::vec3 kGoblinPants{0.30F, 0.22F, 0.14F};
		constexpr glm::vec3 kGoblinEyes{0.98F, 0.82F, 0.12F};
		constexpr glm::vec3 kOrcSkin{0.27F, 0.40F, 0.19F};
		constexpr glm::vec3 kOrcDark{0.19F, 0.29F, 0.13F};
		constexpr glm::vec3 kOrcEyes{0.95F, 0.18F, 0.10F};
		constexpr glm::vec3 kIvory{0.95F, 0.92F, 0.80F};
		constexpr glm::vec3 kIron{0.30F, 0.31F, 0.34F};

		std::vector<PartSpec> headParts(const HumanoidLook& look)
		{
			switch (look.head)
			{
				case HeadStyle::Goblin:
					return {
						{"Neck", PrimitiveType::Cylinder, {0.0F, 0.04F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.05F, 0.05F, 0.05F}, look.skin},
						{"Skull", PrimitiveType::Sphere, {0.0F, 0.17F, 0.02F}, {0.0F, 0.0F, 0.0F}, {0.125F, 0.115F, 0.125F}, look.skin},
						{"EarR", PrimitiveType::Cone, {-0.15F, 0.21F, 0.0F}, {0.0F, 0.0F, 70.0F}, {0.035F, 0.11F, 0.02F}, look.skin},
						{"EarL", PrimitiveType::Cone, {0.15F, 0.21F, 0.0F}, {0.0F, 0.0F, -70.0F}, {0.035F, 0.11F, 0.02F}, look.skin},
						{"Nose", PrimitiveType::Cone, {0.0F, 0.15F, 0.16F}, {90.0F, 0.0F, 0.0F}, {0.025F, 0.07F, 0.025F}, look.skin},
						{"EyeR", PrimitiveType::Sphere, {-0.048F, 0.19F, 0.11F}, {0.0F, 0.0F, 0.0F}, {0.02F, 0.016F, 0.014F}, look.eyes},
						{"EyeL", PrimitiveType::Sphere, {0.048F, 0.19F, 0.11F}, {0.0F, 0.0F, 0.0F}, {0.02F, 0.016F, 0.014F}, look.eyes},
						{"Mouth", PrimitiveType::Cube, {0.0F, 0.10F, 0.115F}, {0.0F, 0.0F, 0.0F}, {0.045F, 0.008F, 0.01F}, kEye},
					};
				case HeadStyle::Orc:
					return {
						{"Neck", PrimitiveType::Cylinder, {0.0F, 0.04F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.075F, 0.05F, 0.075F}, look.skin},
						{"Skull", PrimitiveType::Sphere, {0.0F, 0.17F, 0.01F}, {0.0F, 0.0F, 0.0F}, {0.125F, 0.13F, 0.125F}, look.skin},
						{"Jaw", PrimitiveType::Cube, {0.0F, 0.09F, 0.05F}, {0.0F, 0.0F, 0.0F}, {0.105F, 0.05F, 0.09F}, look.skin},
						{"Brow", PrimitiveType::Cube, {0.0F, 0.215F, 0.1F}, {0.0F, 0.0F, 0.0F}, {0.11F, 0.025F, 0.035F}, kOrcDark},
						{"TuskR", PrimitiveType::Cone, {-0.055F, 0.13F, 0.13F}, {0.0F, 0.0F, 0.0F}, {0.016F, 0.045F, 0.016F}, kIvory},
						{"TuskL", PrimitiveType::Cone, {0.055F, 0.13F, 0.13F}, {0.0F, 0.0F, 0.0F}, {0.016F, 0.045F, 0.016F}, kIvory},
						{"EyeR", PrimitiveType::Sphere, {-0.045F, 0.185F, 0.115F}, {0.0F, 0.0F, 0.0F}, {0.017F, 0.012F, 0.012F}, look.eyes},
						{"EyeL", PrimitiveType::Sphere, {0.045F, 0.185F, 0.115F}, {0.0F, 0.0F, 0.0F}, {0.017F, 0.012F, 0.012F}, look.eyes},
						{"Topknot", PrimitiveType::Cylinder, {0.0F, 0.31F, -0.03F}, {-20.0F, 0.0F, 0.0F}, {0.035F, 0.06F, 0.035F}, look.hair},
					};
				case HeadStyle::Human:
				default:
					return {
						{"Neck", PrimitiveType::Cylinder, {0.0F, 0.04F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.05F, 0.05F, 0.05F}, look.skin},
						{"Skull", PrimitiveType::Sphere, {0.0F, 0.17F, 0.01F}, {0.0F, 0.0F, 0.0F}, {0.105F, 0.12F, 0.11F}, look.skin},
						{"Hair", PrimitiveType::Sphere, {0.0F, 0.21F, -0.015F}, {0.0F, 0.0F, 0.0F}, {0.11F, 0.095F, 0.115F}, look.hair},
						{"EyeR", PrimitiveType::Sphere, {-0.038F, 0.18F, 0.10F}, {0.0F, 0.0F, 0.0F}, {0.014F, 0.014F, 0.014F}, look.eyes},
						{"EyeL", PrimitiveType::Sphere, {0.038F, 0.18F, 0.10F}, {0.0F, 0.0F, 0.0F}, {0.014F, 0.014F, 0.014F}, look.eyes},
						{"Nose", PrimitiveType::Cube, {0.0F, 0.15F, 0.115F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.02F, 0.012F}, look.skin},
					};
			}
		}

		// A round shield on the left forearm (group under ElbowL). Its face
		// looks forward in the "carry" pose and straight ahead when the arm
		// is raised to block - see the body poses in fps_player.lua/enemy.lua.
		std::vector<PartSpec> roundShieldParts(const float radius, const glm::vec3& face, const glm::vec3& metal)
		{
			return {
				{"Face", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {radius, 0.012F, radius}, face},
				{"Rim", PrimitiveType::Cylinder, {0.0F, -0.004F, 0.0F}, {0.0F, 0.0F, 0.0F}, {radius * 1.07F, 0.008F, radius * 1.07F}, metal},
				{"Boss", PrimitiveType::Sphere, {0.0F, 0.014F, 0.0F}, {0.0F, 0.0F, 0.0F}, {radius * 0.25F, radius * 0.16F, radius * 0.25F}, metal},
			};
		}

		// Hand-held weapons for the humanoid's right hand (group at the
		// hand, pointing along +Z of the forearm).
		std::vector<PartSpec> goblinDaggerParts()
		{
			return {
				{"Grip", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.03F}, {90.0F, 0.0F, 0.0F}, {0.018F, 0.05F, 0.018F}, kLeather},
				{"Guard", PrimitiveType::Cube, {0.0F, 0.0F, 0.085F}, {0.0F, 0.0F, 0.0F}, {0.012F, 0.045F, 0.01F}, kIron},
				{"Blade", PrimitiveType::Cube, {0.0F, 0.0F, 0.2F}, {0.0F, 0.0F, 0.0F}, {0.01F, 0.035F, 0.11F}, kSteel},
				{"Tip", PrimitiveType::Cone, {0.0F, 0.0F, 0.33F}, {90.0F, 0.0F, 0.0F}, {0.01F, 0.03F, 0.035F}, kSteel},
			};
		}

		std::vector<PartSpec> goblinSpearParts()
		{
			return {
				{"Shaft", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.2F}, {90.0F, 0.0F, 0.0F}, {0.015F, 0.45F, 0.015F}, kWood},
				{"Tip", PrimitiveType::Cone, {0.0F, 0.0F, 0.72F}, {90.0F, 0.0F, 0.0F}, {0.03F, 0.08F, 0.012F}, kSteel},
				{"Binding", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.62F}, {90.0F, 0.0F, 0.0F}, {0.02F, 0.03F, 0.02F}, kLeather},
			};
		}

		std::vector<PartSpec> orcClubParts()
		{
			std::vector<PartSpec> parts{
				{"Handle", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.12F}, {90.0F, 0.0F, 0.0F}, {0.03F, 0.2F, 0.03F}, kDarkWood},
				{"Head", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.5F}, {90.0F, 0.0F, 0.0F}, {0.085F, 0.22F, 0.085F}, kWood},
				{"Band", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.3F}, {90.0F, 0.0F, 0.0F}, {0.09F, 0.02F, 0.09F}, kIron},
				{"SpikeTip", PrimitiveType::Cone, {0.0F, 0.0F, 0.76F}, {90.0F, 0.0F, 0.0F}, {0.022F, 0.045F, 0.022F}, kSteel},
			};
			// Spikes all round the head: +X, -X, +Y, -Y at two heights.
			static const std::array<const char*, 8> names{"Spike1", "Spike2", "Spike3", "Spike4", "Spike5", "Spike6",
				"Spike7", "Spike8"};
			int index = 0;
			for (const float z : {0.4F, 0.6F})
			{
				const std::array<std::pair<glm::vec3, glm::vec3>, 4> around{{
					{{0.11F, 0.0F, z}, {0.0F, 0.0F, -90.0F}},
					{{-0.11F, 0.0F, z}, {0.0F, 0.0F, 90.0F}},
					{{0.0F, 0.11F, z}, {0.0F, 0.0F, 0.0F}},
					{{0.0F, -0.11F, z}, {0.0F, 0.0F, 180.0F}},
				}};
				for (const auto& [position, rotation] : around)
				{
					parts.push_back({names[static_cast<std::size_t>(index++)], PrimitiveType::Cone, position, rotation,
						{0.022F, 0.045F, 0.022F}, kSteel});
				}
			}
			return parts;
		}

		// The jointed body: primitive parts like the first-person hands, on
		// joint groups the scripts animate (Hips, Spine, Head, ShoulderR/L,
		// ElbowR/L, HipR/L, KneeR/L). It faces +Z; its RIGHT side is -X (the
		// camera looking down +Z shows +X on screen-left). `parentScale` is
		// cancelled so parts keep their own size; `localPosition` is where
		// the feet go in the parent's (scaled) space. Optional `weapon` goes
		// in the right hand (group ".Weapon"), `shield` on the left forearm
		// (group ".Shield").
		void buildHumanoid(Builder& builder, const std::string& body, const std::string& parent,
			const glm::vec3& parentScale, const glm::vec3& localPosition, const HumanoidLook& look,
			const std::vector<PartSpec>& weapon = {}, const std::vector<PartSpec>& shield = {})
		{
			const float s = look.size;
			const float w = look.bulk;
			builder.group(body, parent, localPosition, glm::vec3(0.0F),
				glm::vec3(1.0F / parentScale.x, 1.0F / parentScale.y, 1.0F / parentScale.z), true);

			const std::string hips = body + ".Hips";
			builder.group(hips, body, glm::vec3(0.0F, kBodyHipsHeight, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
			builder.parts(hips, sized({
				{"Pelvis", PrimitiveType::Cube, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.16F, 0.09F, 0.10F}, look.pants},
				{"Belt", PrimitiveType::Cube, {0.0F, 0.08F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.165F, 0.025F, 0.105F}, kBelt},
			}, s, w), true);

			const std::string spine = body + ".Spine";
			builder.group(spine, hips, glm::vec3(0.0F, 0.08F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
			const glm::vec3 torso = look.bareChest ? look.skin : look.shirt;
			std::vector<PartSpec> spineParts{
				{"Belly", PrimitiveType::Cube, {0.0F, 0.12F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.155F, 0.10F, 0.10F}, torso},
				{"Chest", PrimitiveType::Cube, {0.0F, 0.33F, 0.005F}, {0.0F, 0.0F, 0.0F}, {0.195F, 0.14F, 0.115F}, torso},
			};
			if (look.backpack)
			{
				spineParts.push_back({"Pack", PrimitiveType::Cube, {0.0F, 0.30F, -0.16F}, {0.0F, 0.0F, 0.0F}, {0.13F, 0.15F, 0.055F}, kPack});
			}
			if (look.bareChest)
			{
				spineParts.push_back({"Strap", PrimitiveType::Cube, {0.0F, 0.3F, 0.0F}, {0.0F, 0.0F, 35.0F}, {0.03F, 0.2F, 0.12F}, kLeather});
				spineParts.push_back({"Pauldron", PrimitiveType::Sphere, {0.2F, 0.45F, 0.0F}, {0.0F, 0.0F, -15.0F}, {0.09F, 0.06F, 0.1F}, kIron});
			}
			builder.parts(spine, sized(spineParts, s, w), true);

			const std::string head = body + ".Head";
			builder.group(head, spine, glm::vec3(0.0F, 0.48F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
			builder.parts(head, sized(headParts(look), s), true);

			// Arms: shoulder -> upper arm -> elbow -> forearm + hand.
			for (const auto& [side, x] : {std::pair<const char*, float>{"R", -0.245F}, {"L", 0.245F}})
			{
				const std::string shoulder = body + ".Shoulder" + side;
				builder.group(shoulder, spine, glm::vec3(x * w, 0.41F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
				builder.parts(shoulder, sized({
					{"UpperArm", PrimitiveType::Capsule, {0.0F, -0.14F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.058F, 0.15F, 0.058F},
						look.bareArms ? look.skin : look.shirt},
				}, s, w), true);
				const std::string elbow = body + ".Elbow" + side;
				builder.group(elbow, shoulder, glm::vec3(0.0F, -0.29F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
				builder.parts(elbow, sized({
					{"Forearm", PrimitiveType::Capsule, {0.0F, -0.13F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.05F, 0.13F, 0.05F}, look.skin},
					{"Hand", PrimitiveType::Cube, {0.0F, -0.285F, 0.01F}, {0.0F, 0.0F, 0.0F}, {0.042F, 0.055F, 0.028F}, look.hands},
				}, s, w), true);
				if (std::string(side) == "R" && !weapon.empty())
				{
					const std::string weaponNode = body + ".Weapon";
					builder.group(weaponNode, elbow, glm::vec3(0.0F, -0.285F, 0.01F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
					builder.parts(weaponNode, sized(weapon, s), true);
				}
				if (std::string(side) == "L" && !shield.empty())
				{
					// Face normal (0,-0.77,-0.64) in forearm space: forward
					// in the carry pose and when raised to block.
					const std::string shieldNode = body + ".Shield";
					builder.group(shieldNode, elbow, glm::vec3(0.0F, -0.21F, -0.04F) * s, {-140.0F, 0.0F, 0.0F},
						glm::vec3(1.0F), true);
					builder.parts(shieldNode, sized(shield, s), true);
				}
			}

			// Legs: hip -> thigh -> knee -> shin + boot.
			for (const auto& [side, x] : {std::pair<const char*, float>{"R", -0.095F}, {"L", 0.095F}})
			{
				const std::string hip = body + ".Hip" + side;
				builder.group(hip, hips, glm::vec3(x * w, -0.04F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
				builder.parts(hip, sized({
					{"Thigh", PrimitiveType::Capsule, {0.0F, -0.215F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.075F, 0.215F, 0.075F}, look.pants},
				}, s, w), true);
				const std::string knee = body + ".Knee" + side;
				builder.group(knee, hip, glm::vec3(0.0F, -0.43F, 0.0F) * s, glm::vec3(0.0F), glm::vec3(1.0F), true);
				builder.parts(knee, sized({
					{"Shin", PrimitiveType::Capsule, {0.0F, -0.20F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.064F, 0.20F, 0.064F}, look.pants},
					{"Boot", PrimitiveType::Cube, {0.0F, -0.41F, 0.045F}, {0.0F, 0.0F, 0.0F}, {0.06F, 0.045F, 0.115F}, look.boots},
				}, s, w), true);
			}
		}

		// A monster: a hidden capsule (the collision/hit shape, center at
		// its middle - enemy.lua's convention) carrying health.lua +
		// enemy.lua, with a humanoid body under it. `values` are enemy.lua
		// Inspector values for this monster.
		std::string buildMonster(Builder& builder, const std::string& baseName, const glm::vec3& feet,
			const HumanoidLook& look, const std::vector<PartSpec>& weapon, const std::vector<PartSpec>& shield,
			const float maxHealth, const std::vector<std::pair<std::string, std::string>>& values)
		{
			const std::string name = builder.uniqueName(baseName);
			const float height = 1.8F * look.size;
			const float radius = 0.32F * look.size * look.bulk;
			const glm::vec3 scale(radius, height * 0.5F, radius);
			if (!builder.create(name, PrimitiveType::Capsule, feet + glm::vec3(0.0F, height * 0.5F, 0.0F)))
			{
				return {};
			}
			builder.set(name, "Transform", "scale", scale);
			builder.set(name, "Renderer", "color", look.skin);
			builder.set(name, "Collider", "enabled", true);
			builder.tag(name, "Enemy");
			builder.tag(name, "Empty"); // the body below is what you see
			builder.attach(name, kHealthScript);
			builder.scriptProperty(name, kHealthScript, "max_health", std::to_string(static_cast<int>(maxHealth)));
			builder.attach(name, kEnemyAiScript);
			const std::string body = name + " Body";
			builder.scriptProperty(name, kEnemyAiScript, "body_name", body);
			builder.scriptProperty(name, kEnemyAiScript, "model_scale", std::to_string(look.size));
			for (const auto& [property, value] : values)
			{
				builder.scriptProperty(name, kEnemyAiScript, property, value);
			}
			builder.setIgnoreTag("NoRaycast");
			buildHumanoid(builder, body, name, scale, {0.0F, -1.0F, 0.0F}, look, weapon, shield);
			builder.setIgnoreTag("Viewmodel");
			return name;
		}

		// Two goblins (melee + ranged, both with shields) and the Orc Warlord
		// boss with his spiked club, further down the arena.
		void buildMonsters(Builder& builder, const glm::vec3& origin)
		{
			HumanoidLook goblin;
			goblin.size = 0.72F;
			goblin.skin = kGoblinSkin;
			goblin.shirt = kGoblinVest;
			goblin.pants = kGoblinPants;
			goblin.boots = kLeather;
			goblin.hands = kGoblinSkin;
			goblin.eyes = kGoblinEyes;
			goblin.head = HeadStyle::Goblin;
			goblin.backpack = false;
			goblin.bareArms = true;
			const std::vector<PartSpec> buckler = roundShieldParts(0.17F, kWood, kIron);

			(void)buildMonster(builder, "Goblin Cutter", origin + glm::vec3(-5.0F, 0.0F, 21.0F), goblin,
				goblinDaggerParts(), buckler, 60.0F,
				{{"attack_style", "melee"}, {"damage", "7"}, {"attack_range", "1.3"}, {"attack_cooldown", "1.1"},
					{"windup", "0.4"}, {"chase_speed", "3.8"}, {"search_radius", "10"}, {"escape_radius", "18"},
					{"has_shield", "true"}, {"shield_block", "0.6"}});
			(void)buildMonster(builder, "Goblin Spearthrower", origin + glm::vec3(5.0F, 0.0F, 23.0F), goblin,
				goblinSpearParts(), buckler, 45.0F,
				{{"attack_style", "ranged"}, {"damage", "6"}, {"attack_range", "13"}, {"attack_cooldown", "1.8"},
					{"windup", "0.55"}, {"chase_speed", "3.2"}, {"search_radius", "14"}, {"escape_radius", "22"},
					{"keep_distance", "7"}, {"projectile_speed", "15"}, {"has_shield", "true"}, {"shield_block", "0.5"}});

			HumanoidLook orc;
			orc.size = 1.45F;
			orc.bulk = 1.35F;
			orc.skin = kOrcSkin;
			orc.pants = kLeather;
			orc.boots = kIron;
			orc.hands = kOrcSkin;
			orc.hair = kEye;
			orc.eyes = kOrcEyes;
			orc.head = HeadStyle::Orc;
			orc.backpack = false;
			orc.bareArms = true;
			orc.bareChest = true;
			(void)buildMonster(builder, "Orc Warlord", origin + glm::vec3(0.0F, 0.0F, 32.0F), orc, orcClubParts(), {},
				500.0F,
				{{"attack_style", "melee"}, {"damage", "30"}, {"attack_range", "2.6"}, {"attack_cooldown", "2.2"},
					{"windup", "0.85"}, {"chase_speed", "2.6"}, {"wander_speed", "1.2"}, {"search_radius", "15"},
					{"escape_radius", "32"}, {"knockback", "7"}, {"is_boss", "true"}, {"boss_title", "Orc Warlord"}});
		}

		// The player's first-person shield (FPSRig.Shield), raised with
		// the right mouse button - hidden until then. Authored facing the
		// camera (disc along Z); fps_player.lua moves it.
		std::vector<PartSpec> fpsShieldParts()
		{
			return {
				{"Face", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.0F}, {90.0F, 0.0F, 0.0F}, {0.14F, 0.01F, 0.14F}, kWood},
				{"Rim", PrimitiveType::Cylinder, {0.0F, 0.0F, 0.004F}, {90.0F, 0.0F, 0.0F}, {0.15F, 0.007F, 0.15F}, kIron},
				{"Band", PrimitiveType::Cube, {0.0F, 0.0F, -0.012F}, {0.0F, 0.0F, 0.0F}, {0.14F, 0.014F, 0.004F}, kIron},
				{"Boss", PrimitiveType::Sphere, {0.0F, 0.0F, 0.014F}, {0.0F, 0.0F, 0.0F}, {0.035F, 0.035F, 0.022F}, kIron},
			};
		}

		// Something to climb in the demo arena (climbable.lua): a ladder
		// and a rock cliff face.
		void buildClimbables(Builder& builder, const glm::vec3& origin)
		{
			// Ladder: two rails + rungs under one climbable box, leaning on
			// a wall block you can pull yourself up onto.
			const std::string wall = builder.uniqueName("Climb Wall");
			if (builder.create(wall, PrimitiveType::Cube, origin + glm::vec3(9.0F, 2.0F, 4.0F)))
			{
				builder.set(wall, "Transform", "scale", glm::vec3(1.5F, 2.0F, 1.5F));
				builder.set(wall, "Renderer", "color", glm::vec3(0.55F, 0.52F, 0.48F));
				builder.set(wall, "Collider", "enabled", true);
			}
			// The ladder: an empty group (move it and everything follows) with
			// the climbable box (1 wide, 4 tall, thin - its scale IS the
			// climbable area) and the visible rails and rungs under it.
			const std::string ladder = builder.uniqueName("Ladder");
			const glm::vec3 ladderBase = origin + glm::vec3(7.42F, 0.0F, 4.0F);
			builder.group(ladder, "", ladderBase, glm::vec3(0.0F), glm::vec3(1.0F), false);
			const std::string climbBox = ladder + ".Climb";
			builder.group(climbBox, ladder, {0.0F, 2.0F, 0.0F}, glm::vec3(0.0F), {0.08F, 2.0F, 0.5F}, false);
			builder.attach(climbBox, kClimbableScript);
			// 270 = its -X side, facing away from the wall.
			builder.scriptProperty(climbBox, kClimbableScript, "climb_angle", "270");
			std::vector<PartSpec> ladderParts{
				{"RailA", PrimitiveType::Cube, {0.0F, 2.0F, -0.45F}, {0.0F, 0.0F, 0.0F}, {0.04F, 2.0F, 0.04F}, kWood},
				{"RailB", PrimitiveType::Cube, {0.0F, 2.0F, 0.45F}, {0.0F, 0.0F, 0.0F}, {0.04F, 2.0F, 0.04F}, kWood},
			};
			static const std::array<const char*, 11> rungNames{
				"Rung1", "Rung2", "Rung3", "Rung4", "Rung5", "Rung6", "Rung7", "Rung8", "Rung9", "Rung10", "Rung11"};
			for (std::size_t rung = 0; rung < rungNames.size(); ++rung)
			{
				ladderParts.push_back({rungNames[rung], PrimitiveType::Cylinder,
					{0.0F, 0.3F + static_cast<float>(rung) * 0.35F, 0.0F}, {90.0F, 0.0F, 0.0F}, {0.03F, 0.45F, 0.03F},
					kDarkWood});
			}
			builder.parts(ladder, ladderParts, false);

			// Rock cliff: a tall block you climb on its -Z face (the side
			// facing the start). Not turned: colliders ignore rotation, so a
			// turned solid block would stop you short of its real face.
			const std::string cliff = builder.uniqueName("Rock Cliff");
			if (builder.create(cliff, PrimitiveType::Cube, origin + glm::vec3(-9.0F, 3.0F, 12.0F)))
			{
				builder.set(cliff, "Transform", "scale", glm::vec3(2.5F, 3.0F, 1.2F));
				builder.set(cliff, "Renderer", "color", glm::vec3(0.46F, 0.42F, 0.38F));
				builder.set(cliff, "Collider", "enabled", true);
				builder.attach(cliff, kClimbableScript);
				builder.scriptProperty(cliff, kClimbableScript, "climb_angle", "180");
			}
		}

		std::string createGameManager(Builder& builder, const glm::vec3& position)
		{
			const std::string name = builder.uniqueName("Game Manager");
			builder.group(name, "", position, glm::vec3(0.0F), glm::vec3(1.0F), false);
			builder.tag(name, "GameManager");
			builder.attach(name, kGameManagerScript);
			return builder.created() > 0 ? name : std::string();
		}

		void buildDemoContent(Builder& builder, const glm::vec3& origin, const std::vector<WeaponSpec>& weapons)
		{
			// Weapon pickups on an arc 4-6 units in front (+Z) of the player;
			// the XP-unlocked casters are placed hidden off to the side (they
			// arrive in the inventory on level-up, see xp_system.lua).
			std::vector<const WeaponSpec*> starting;
			for (const WeaponSpec& weapon : weapons)
			{
				if (!isXpUnlock(weapon.id))
				{
					starting.push_back(&weapon);
				}
			}
			const int count = static_cast<int>(starting.size());
			int lockedIndex = 0;
			for (const WeaponSpec& weapon : weapons)
			{
				const bool locked = isXpUnlock(weapon.id);
				const auto found = std::find(starting.begin(), starting.end(), &weapon);
				const int index = locked ? 0 : static_cast<int>(found - starting.begin());
				const float angle = (static_cast<float>(index) - static_cast<float>(count - 1) * 0.5F) * 0.30F;
				const float radius = 5.0F;
				const glm::vec3 position = locked
					? origin + glm::vec3(-10.0F + static_cast<float>(lockedIndex++) * 1.5F, 0.45F, -4.0F)
					: origin + glm::vec3(std::sin(angle) * radius, 0.45F, std::cos(angle) * radius);
				const std::string pickupName = builder.uniqueName(std::string(weapon.displayName) + " Pickup");
				builder.group(pickupName, "", position, {0.0F, 0.0F, weapon.hasMuzzle ? 0.0F : -35.0F},
					glm::vec3(1.6F), false);
				builder.parts(pickupName, weapon.parts, false);
				builder.attach(pickupName, kItemScript);
				builder.scriptProperty(pickupName, kItemScript, "item_name", weapon.displayName);
				builder.scriptProperty(pickupName, kItemScript, "icon", weapon.icon);
				builder.scriptProperty(pickupName, kItemScript, "item_type", "weapon");
				builder.scriptProperty(pickupName, kItemScript, "weapon", weapon.id);
				if (locked)
				{
					builder.set(pickupName, "Entity", "active", false);
				}
			}

			// Training targets: two standing dummies and one that chases you.
			const std::array<std::pair<const char*, glm::vec3>, 4> dummies{{
				{"Training Dummy", origin + glm::vec3(-4.0F, 0.95F, 10.0F)},
				{"Training Dummy", origin + glm::vec3(4.0F, 0.95F, 10.0F)},
				{"Chaser", origin + glm::vec3(0.0F, 0.95F, 16.0F)},
				{"Chaser", origin + glm::vec3(8.0F, 0.95F, 18.0F)},
			}};
			for (const auto& [baseName, position] : dummies)
			{
				const std::string name = builder.uniqueName(baseName);
				if (!builder.create(name, PrimitiveType::Capsule, position))
				{
					continue;
				}
				const bool chaser = std::string(baseName) == "Chaser";
				builder.set(name, "Transform", "scale", glm::vec3(0.45F, 0.95F, 0.45F));
				builder.set(name, "Renderer", "color",
					chaser ? glm::vec3(0.75F, 0.18F, 0.16F) : glm::vec3(0.82F, 0.72F, 0.42F));
				builder.set(name, "Collider", "enabled", true);
				builder.tag(name, "Enemy");
				builder.attach(name, kHealthScript);
				builder.scriptProperty(name, kHealthScript, "max_health", chaser ? "100" : "150");
				if (chaser)
				{
					builder.attach(name, kEnemyAiScript);
				}
			}

			// A rock the pickaxe mines faster than anything else.
			const std::string rockName = builder.uniqueName("Ore Rock");
			if (builder.create(rockName, PrimitiveType::Sphere, origin + glm::vec3(-7.0F, 0.55F, 6.0F)))
			{
				builder.set(rockName, "Transform", "scale", glm::vec3(0.8F, 0.6F, 0.8F));
				builder.set(rockName, "Renderer", "color", glm::vec3(0.42F, 0.40F, 0.44F));
				builder.set(rockName, "Collider", "enabled", true);
				builder.tag(rockName, "Mineable");
				builder.attach(rockName, kHealthScript);
				builder.scriptProperty(rockName, kHealthScript, "max_health", "120");
			}

			buildClimbables(builder, origin);
			buildMonsters(builder, origin);

			(void)createGameManager(builder, origin + glm::vec3(0.0F, 0.5F, -3.0F));
		}
	}

	std::string createGameManager(EditorScene& scene, AICommandBus& commandBus, const glm::vec3& position)
	{
		Builder builder(scene, commandBus);
		return createGameManager(builder, position);
	}

	FpsRigBuildResult buildFpsPlayerRig(EditorScene& scene, AICommandBus& commandBus, const FpsRigOptions& options)
	{
		Builder builder(scene, commandBus);
		FpsRigBuildResult result;
		const std::vector<WeaponSpec> weapons = weaponSpecs();

		if (options.includeGround)
		{
			const std::string groundName = builder.uniqueName("Ground");
			if (builder.create(groundName, PrimitiveType::Cube, options.position + glm::vec3(0.0F, -0.5F, 0.0F)))
			{
				builder.set(groundName, "Transform", "scale", glm::vec3(40.0F, 0.5F, 40.0F));
				builder.set(groundName, "Renderer", "color", glm::vec3(0.30F, 0.36F, 0.28F));
				builder.tag(groundName, "Ground"); // also turns its collider on
			}
		}

		// Player: a capsule whose Position is its FEET (pivot at the bottom),
		// matching the controller's feet-based collision.
		result.playerName = builder.uniqueName("Player");
		if (!builder.create(result.playerName, PrimitiveType::Capsule, options.position))
		{
			result.message = "Could not create the player entity.";
			return result;
		}
		const glm::vec3 playerScale(0.4F, 0.9F, 0.4F);
		builder.set(result.playerName, "Transform", "scale", playerScale);
		builder.set(result.playerName, "Transform", "pivot", glm::vec3(0.0F, -1.0F, 0.0F));
		builder.set(result.playerName, "Renderer", "color", glm::vec3(0.25F, 0.45F, 0.75F));
		builder.set(result.playerName, "Camera", "fpsEyeHeight", 1.62F);
		builder.set(result.playerName, "Camera", "thirdPersonAimHeight", 1.4F);
		builder.set(result.playerName, "Camera", "lockCursor", true);
		builder.tag(result.playerName, "Player");
		// The capsule is only the collision shape now - the body below is
		// what you see in third person.
		builder.tag(result.playerName, "Empty");

		result.bodyName = builder.uniqueName("PlayerBody");
		buildHumanoid(builder, result.bodyName, result.playerName, playerScale, glm::vec3(0.0F), HumanoidLook{}, {},
			roundShieldParts(0.2F, kWood, kIron));

		// The viewmodel rig.
		result.rigName = builder.uniqueName("FPSRig");
		const std::string& rig = result.rigName;
		const glm::vec3 eye = options.position + glm::vec3(0.0F, 1.62F, 0.0F);
		builder.group(rig, "", eye, glm::vec3(0.0F), glm::vec3(1.0F), true);
		builder.group(rig + ".Pitch", rig, glm::vec3(0.0F), glm::vec3(0.0F), glm::vec3(1.0F), true);
		builder.group(rig + ".Sway", rig + ".Pitch", glm::vec3(0.0F), glm::vec3(0.0F), glm::vec3(1.0F), true);

		const std::string handR = rig + ".HandR";
		// Rig space = authored space mirrored in X (see mirrored()).
		builder.group(handR, rig + ".Sway", {-0.19F, -0.23F, 0.42F}, glm::vec3(0.0F), glm::vec3(1.0F), true);
		builder.parts(handR, mirrored(handParts()), true);
		const std::string handL = rig + ".HandL";
		builder.group(handL, rig + ".Sway", {0.16F, -0.24F, 0.46F}, glm::vec3(0.0F), glm::vec3(1.0F), true);
		builder.parts(handL, handParts(), true);
		builder.set(handL, "Entity", "active", false);
		const std::string shield = rig + ".Shield";
		builder.group(shield, rig + ".Sway", {0.10F, -0.55F, 0.40F}, glm::vec3(0.0F), glm::vec3(1.0F), true);
		builder.parts(shield, fpsShieldParts(), true);
		builder.set(shield, "Entity", "active", false);

		for (const WeaponSpec& weapon : weapons)
		{
			const std::string weaponNode = rig + ".W." + weapon.id;
			const glm::vec3 groupRotation(weapon.groupRotation.x, -weapon.groupRotation.y, -weapon.groupRotation.z);
			builder.group(weaponNode, handR, glm::vec3(-weapon.viewOffset.x, weapon.viewOffset.y, weapon.viewOffset.z),
				groupRotation, glm::vec3(weapon.viewScale), true);
			builder.parts(weaponNode, mirrored(weapon.parts), true);
			if (weapon.hasSupportHand)
			{
				// The support hand is a LEFT hand (mirrored() of the right
				// one) - mirrored again into rig space, i.e. unmirrored.
				const std::string support = weaponNode + ".Support";
				const glm::vec3 supportPosition(
					-weapon.supportPosition.x, weapon.supportPosition.y, weapon.supportPosition.z);
				const glm::vec3 supportRotation(
					weapon.supportRotation.x, -weapon.supportRotation.y, -weapon.supportRotation.z);
				builder.group(support, weaponNode, supportPosition, supportRotation, glm::vec3(1.0F), true);
				std::vector<PartSpec> supportParts = handParts();
				if (!weapon.supportSleeve)
				{
					std::erase_if(supportParts, [](const PartSpec& part)
						{ return std::string(part.suffix) == "Sleeve" || std::string(part.suffix) == "Cuff"; });
				}
				builder.parts(support, supportParts, true);
			}
			if (weapon.hasMuzzle)
			{
				// A marker only - its world position is where shots start.
				const std::string muzzle = weaponNode + ".Muzzle";
				builder.group(muzzle, weaponNode, glm::vec3(-weapon.muzzle.x, weapon.muzzle.y, weapon.muzzle.z),
					glm::vec3(0.0F), glm::vec3(0.01F), true);
			}
			builder.set(weaponNode, "Entity", "active", false);
		}

		// The FPS Demo preset on the player.
		for (const std::string& script : fpsDemoPlayerScripts())
		{
			builder.attach(result.playerName, script);
		}
		builder.scriptProperty(result.playerName, kFpsPlayerScript, "rig_name", rig);
		builder.scriptProperty(result.playerName, kFpsPlayerScript, "body_name", result.bodyName);
		builder.scriptProperty(result.playerName, kHealthScript, "is_player", "true");
		builder.scriptProperty(result.playerName, kHealthScript, "destroy_on_death", "false");

		if (options.includeDemoContent)
		{
			buildDemoContent(builder, options.position, weapons);
		}

		result.entitiesCreated = builder.created();
		result.success = builder.failures() == 0;
		result.message = "Created " + std::to_string(result.entitiesCreated) + " entities (player '" +
			result.playerName + "', body '" + result.bodyName + "', rig '" + rig + "').";
		if (builder.failures() > 0)
		{
			result.message += " " + std::to_string(builder.failures()) + " step(s) failed - see the Console.";
		}
		if (builder.missingScripts() > 0)
		{
			result.message += " " + std::to_string(builder.missingScripts()) +
				" script(s) could not be attached - the FPS Demo kit is missing from this project "
				"(File > Import FPS Demo Kit into This Project).";
		}
		return result;
	}
}
