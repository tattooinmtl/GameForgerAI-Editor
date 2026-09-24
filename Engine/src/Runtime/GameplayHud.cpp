#include "GameForger/Runtime/GameplayHud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include <glm/common.hpp>
#include <glm/vec4.hpp>

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* kHealthScriptPath = "Game/Scripts/health.lua";

		HudColor withAlpha(const glm::vec3& color, const float alpha)
		{
			return {glm::clamp(color.r, 0.0F, 1.0F), glm::clamp(color.g, 0.0F, 1.0F), glm::clamp(color.b, 0.0F, 1.0F),
				glm::clamp(alpha, 0.0F, 1.0F)};
		}

		// Text with a 1px dark drop shadow so it reads on any background.
		void shadowedText(HudCanvas& canvas, const glm::vec2& position, const HudColor& color, const std::string& text,
			const float scale)
		{
			canvas.text(position + glm::vec2(1.0F, 1.0F), {0.0F, 0.0F, 0.0F, color.a * 0.8F}, text, scale);
			canvas.text(position, color, text, scale);
		}

		void drawBeams(HudCanvas& canvas, const HudFrame& frame, const GameplayState& gameplay)
		{
			for (const GameplayState::Beam& beam : gameplay.beams)
			{
				glm::vec2 from{};
				glm::vec2 to{};
				if (!hudWorldToScreen(frame, beam.from, from) || !hudWorldToScreen(frame, beam.to, to))
				{
					continue;
				}
				const float life = glm::clamp(beam.remainingSeconds / std::max(beam.totalSeconds, 0.001F), 0.0F, 1.0F);
				const HudColor core = withAlpha(beam.color, life * 0.92F);
				const HudColor glow = withAlpha(beam.color, life * 0.28F);
				if (!beam.jagged)
				{
					canvas.line(from, to, glow, beam.width * 3.0F);
					canvas.line(from, to, core, beam.width);
					continue;
				}
				// Lightning: zig-zag re-rolled ~30 times a second so it crackles.
				constexpr int kSegments = 12;
				std::array<glm::vec2, kSegments + 1> points{};
				const glm::vec2 delta = to - from;
				const float length = glm::length(delta);
				const glm::vec2 normal = length > 0.001F ? glm::vec2(-delta.y, delta.x) / length : glm::vec2(0.0F);
				const float amplitude = std::min(22.0F, length * 0.07F);
				std::mt19937 jitter(beam.seed + static_cast<unsigned int>(frame.timeSeconds * 30.0F));
				std::uniform_real_distribution<float> offset(-1.0F, 1.0F);
				for (int index = 0; index <= kSegments; ++index)
				{
					const float t = static_cast<float>(index) / static_cast<float>(kSegments);
					const float wobble = (index == 0 || index == kSegments) ? 0.0F : offset(jitter) * amplitude;
					points[static_cast<std::size_t>(index)] = from + delta * t + normal * wobble;
				}
				for (int index = 0; index < kSegments; ++index)
				{
					canvas.line(points[static_cast<std::size_t>(index)], points[static_cast<std::size_t>(index + 1)], glow,
						beam.width * 3.5F);
				}
				for (int index = 0; index < kSegments; ++index)
				{
					canvas.line(points[static_cast<std::size_t>(index)], points[static_cast<std::size_t>(index + 1)], core,
						beam.width);
				}
			}
		}

		void drawFlashes(HudCanvas& canvas, const HudFrame& frame, const GameplayState& gameplay)
		{
			for (const GameplayState::Flash& flash : gameplay.flashes)
			{
				glm::vec2 center{};
				if (!hudWorldToScreen(frame, flash.position, center))
				{
					continue;
				}
				const float life = flash.particle
					? glm::clamp(flash.intensity, 0.0F, 1.0F)
					: glm::clamp(flash.remainingSeconds / std::max(flash.totalSeconds, 0.001F), 0.0F, 1.0F);
				// Particles are sized in world units -> shrink with distance.
				float radius = flash.size * (0.5F + 0.5F * life);
				if (flash.particle)
				{
					const glm::vec4 clip = frame.projection * frame.view * glm::vec4(flash.position, 1.0F);
					// Projected size: world size * focal length (from the
					// projection's own [1][1] = 1/tan(fov/2)) / depth.
					radius = flash.size * frame.projection[1][1] * frame.size.y * 0.5F / std::max(clip.w, 0.05F);
					if (radius < 0.4F)
					{
						continue;
					}
					// Soft glow, body, hot center.
					canvas.circleFilled(center, radius * 1.7F, withAlpha(flash.color, life * 0.12F));
					canvas.circleFilled(center, radius, withAlpha(flash.color, life * 0.75F));
					if (radius > 2.5F)
					{
						canvas.circleFilled(center, radius * 0.45F, withAlpha(glm::mix(flash.color, glm::vec3(1.0F), 0.6F), life * 0.8F));
					}
					continue;
				}
				canvas.circleFilled(center, radius, withAlpha(flash.color, life * 0.6F));
				canvas.circleFilled(center, radius * 0.45F, {1.0F, 1.0F, 0.95F, life * 0.9F});
			}
		}

		void drawFloatingTexts(HudCanvas& canvas, const HudFrame& frame, const GameplayState& gameplay)
		{
			for (const GameplayState::FloatingText& text : gameplay.floatingTexts)
			{
				glm::vec2 anchor{};
				if (!hudWorldToScreen(frame, text.position, anchor))
				{
					continue;
				}
				const float life = glm::clamp(text.remainingSeconds / std::max(text.totalSeconds, 0.001F), 0.0F, 1.0F);
				// Pop in slightly bigger, then settle.
				const float age = 1.0F - life;
				const float scale = text.scale * (age < 0.12F ? 1.0F + (0.12F - age) * 3.0F : 1.0F);
				const glm::vec2 size = canvas.textSize(text.text, scale);
				shadowedText(canvas, anchor - size * 0.5F, withAlpha(text.color, std::min(1.0F, life * 2.0F)), text.text,
					scale);
			}
		}

		void drawHealthBars(HudCanvas& canvas, const HudFrame& frame, const EditorScene& scene,
			const ScriptRuntime& scriptRuntime)
		{
			for (const SceneEntity& other : scene.entities())
			{
				if (!hasScript(other, kHealthScriptPath) || !scene.isActiveInHierarchy(other) ||
					(frame.player != nullptr && other.id == frame.player->id))
				{
					continue;
				}
				const float health = scriptRuntime.getScriptNumberField(other.id, kHealthScriptPath, "health", -1.0F);
				const float maxHealth = scriptRuntime.getScriptNumberField(other.id, kHealthScriptPath, "max_health", 100.0F);
				if (health < 0.0F || maxHealth <= 0.0F || health >= maxHealth)
				{
					continue;
				}
				glm::vec2 center{};
				if (!hudWorldToScreen(frame, other.position + glm::vec3(0.0F, std::abs(other.scale.y) + 0.45F, 0.0F), center))
				{
					continue;
				}
				constexpr float kWidth = 70.0F;
				constexpr float kHeight = 7.0F;
				const float fraction = glm::clamp(health / maxHealth, 0.0F, 1.0F);
				const glm::vec2 min(center.x - kWidth * 0.5F, center.y - kHeight * 0.5F);
				const glm::vec2 max(center.x + kWidth * 0.5F, center.y + kHeight * 0.5F);
				canvas.rectFilled(min, max, {0.08F, 0.08F, 0.08F, 0.75F}, 2.0F);
				canvas.rectFilled(min, glm::vec2(min.x + kWidth * fraction, max.y),
					{0.9F * (1.0F - fraction) + 0.24F * fraction, 0.24F * (1.0F - fraction) + 0.78F * fraction, 0.24F, 0.92F},
					2.0F);
				canvas.rect(min, max, {0.0F, 0.0F, 0.0F, 0.85F}, 2.0F, 1.0F);
				// Status tint: burning / frozen (health.lua publishes these).
				const float burning = scriptRuntime.getScriptNumberField(other.id, kHealthScriptPath, "burning", 0.0F);
				const float frozen = scriptRuntime.getScriptNumberField(other.id, kHealthScriptPath, "frozen", 0.0F);
				if (burning > 0.0F || frozen > 0.0F)
				{
					const HudColor tint = burning > 0.0F ? HudColor{1.0F, 0.5F, 0.1F, 1.0F} : HudColor{0.5F, 0.8F, 1.0F, 1.0F};
					canvas.circleFilled(glm::vec2(max.x + 7.0F, center.y), 4.0F, tint);
				}
			}
		}

		void drawHotbar(HudCanvas& canvas, const HudFrame& frame, const GameplayState& gameplay)
		{
			if (!frame.showHotbar || static_cast<int>(gameplay.inventoryItems.size()) < kHotbarSlotCount)
			{
				return;
			}
			constexpr float kSlot = 48.0F;
			constexpr float kGap = 6.0F;
			const float totalWidth = kSlot * kHotbarSlotCount + kGap * (kHotbarSlotCount - 1);
			const glm::vec2 origin(
				frame.origin.x + (frame.size.x - totalWidth) * 0.5F, frame.origin.y + frame.size.y - kSlot - 16.0F);
			for (int slot = 0; slot < kHotbarSlotCount; ++slot)
			{
				const GameplayState::InventoryItem& item = gameplay.inventoryItems[static_cast<std::size_t>(slot)];
				const bool selected = gameplay.selectedSlot == slot;
				const glm::vec2 min(origin.x + static_cast<float>(slot) * (kSlot + kGap), origin.y);
				const glm::vec2 max = min + glm::vec2(kSlot);
				canvas.rectFilled(min, max, {0.05F, 0.05F, 0.06F, selected ? 0.82F : 0.6F}, 5.0F);
				if (!item.empty())
				{
					if (!item.iconPath.empty())
					{
						canvas.image(item.iconPath, min + glm::vec2(5.0F), max - glm::vec2(5.0F));
					}
					else
					{
						canvas.text(min + glm::vec2(12.0F, 16.0F), {0.9F, 0.9F, 0.9F, 1.0F}, item.itemName.substr(0, 2), 1.0F);
					}
					if (item.count > 1)
					{
						const std::string count = std::to_string(item.count);
						const glm::vec2 size = canvas.textSize(count, 0.85F);
						shadowedText(canvas, max - size - glm::vec2(4.0F, 2.0F), {1.0F, 1.0F, 1.0F, 1.0F}, count, 0.85F);
					}
				}
				shadowedText(canvas, min + glm::vec2(4.0F, 1.0F), {0.8F, 0.8F, 0.8F, 0.8F}, std::to_string(slot + 1), 0.75F);
				canvas.rect(min, max,
					selected ? HudColor{0.976F, 0.53F, 0.012F, 1.0F} : HudColor{0.6F, 0.6F, 0.65F, 0.5F}, 5.0F,
					selected ? 3.0F : 1.0F);
			}
			const int selectedSlot = gameplay.selectedSlot;
			if (selectedSlot >= 0 && selectedSlot < static_cast<int>(gameplay.inventoryItems.size()) &&
				!gameplay.inventoryItems[static_cast<std::size_t>(selectedSlot)].empty())
			{
				const std::string& name = gameplay.inventoryItems[static_cast<std::size_t>(selectedSlot)].itemName;
				const glm::vec2 size = canvas.textSize(name, 1.0F);
				shadowedText(canvas, glm::vec2(frame.origin.x + frame.size.x * 0.5F - size.x * 0.5F, origin.y - size.y - 6.0F),
					{1.0F, 1.0F, 1.0F, 0.95F}, name, 1.0F);
			}
		}

		void drawHudBars(HudCanvas& canvas, const HudFrame& frame, const GameplayState& gameplay)
		{
			constexpr float kWidth = 220.0F;
			constexpr float kHeight = 16.0F;
			constexpr float kGap = 6.0F;
			glm::vec2 cursor(frame.origin.x + 18.0F, frame.origin.y + frame.size.y - 22.0F - kHeight);
			// Stack upward from the bottom-left, first bar at the bottom.
			for (auto it = gameplay.hudBars.rbegin(); it != gameplay.hudBars.rend(); ++it)
			{
				const GameplayState::HudBar& bar = *it;
				const glm::vec2 min = cursor;
				const glm::vec2 max = cursor + glm::vec2(kWidth, kHeight);
				canvas.rectFilled(min - glm::vec2(2.0F), max + glm::vec2(2.0F), {0.0F, 0.0F, 0.0F, 0.55F}, 4.0F);
				canvas.rectFilled(min, glm::vec2(min.x + kWidth * glm::clamp(bar.fraction, 0.0F, 1.0F), max.y),
					withAlpha(bar.color, 0.95F), 3.0F);
				canvas.rect(min, max, {1.0F, 1.0F, 1.0F, 0.25F}, 3.0F, 1.0F);
				shadowedText(canvas, min + glm::vec2(6.0F, 0.0F), {1.0F, 1.0F, 1.0F, 1.0F}, bar.label, 0.8F);
				cursor.y -= kHeight + kGap;
			}
		}
	}

	bool hudWorldToScreen(const HudFrame& frame, const glm::vec3& world, glm::vec2& screen)
	{
		const glm::vec4 clip = frame.projection * frame.view * glm::vec4(world, 1.0F);
		if (clip.w <= 0.0001F)
		{
			return false;
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		screen = glm::vec2(frame.origin.x + (ndc.x * 0.5F + 0.5F) * frame.size.x,
			frame.origin.y + (1.0F - (ndc.y * 0.5F + 0.5F)) * frame.size.y);
		return true;
	}

	void drawGameplayHud(
		HudCanvas& canvas, const HudFrame& frame, const EditorScene& scene, const ScriptRuntime& scriptRuntime,
		const GameplayState& gameplay)
	{
		drawBeams(canvas, frame, gameplay);
		drawFlashes(canvas, frame, gameplay);
		drawHealthBars(canvas, frame, scene, scriptRuntime);
		drawFloatingTexts(canvas, frame, gameplay);

		if (frame.drawCrosshair)
		{
			const glm::vec2 center = frame.origin + frame.size * 0.5F;
			const HudColor white{1.0F, 1.0F, 1.0F, 0.85F};
			canvas.line(center - glm::vec2(9.0F, 0.0F), center - glm::vec2(3.0F, 0.0F), white, 2.0F);
			canvas.line(center + glm::vec2(3.0F, 0.0F), center + glm::vec2(9.0F, 0.0F), white, 2.0F);
			canvas.line(center - glm::vec2(0.0F, 9.0F), center - glm::vec2(0.0F, 3.0F), white, 2.0F);
			canvas.line(center + glm::vec2(0.0F, 3.0F), center + glm::vec2(0.0F, 9.0F), white, 2.0F);
		}

		drawHotbar(canvas, frame, gameplay);
		drawHudBars(canvas, frame, gameplay);

		// Weapon/ammo line a script publishes as self.hud_text.
		if (frame.player != nullptr)
		{
			for (const std::string& scriptPath : frame.player->scripts)
			{
				const std::string hudText = scriptRuntime.getScriptStringField(frame.player->id, scriptPath, "hud_text", "");
				if (hudText.empty())
				{
					continue;
				}
				const glm::vec2 size = canvas.textSize(hudText, 1.0F);
				const glm::vec2 position(
					frame.origin.x + frame.size.x - size.x - 22.0F, frame.origin.y + frame.size.y - size.y - 26.0F);
				canvas.rectFilled(position - glm::vec2(10.0F, 6.0F), position + size + glm::vec2(10.0F, 6.0F),
					{0.0F, 0.0F, 0.0F, 0.58F}, 5.0F);
				canvas.text(position, {1.0F, 0.92F, 0.75F, 1.0F}, hudText, 1.0F);
				break;
			}
		}

		if (!frame.interactionHint.empty())
		{
			const glm::vec2 size = canvas.textSize(frame.interactionHint, 1.0F);
			const glm::vec2 position(frame.origin.x + frame.size.x * 0.5F - size.x * 0.5F,
				frame.origin.y + frame.size.y * 0.5F + 26.0F);
			canvas.rectFilled(position - glm::vec2(7.0F, 4.0F), position + size + glm::vec2(7.0F, 4.0F),
				{0.0F, 0.0F, 0.0F, 0.55F}, 4.0F);
			canvas.text(position, {1.0F, 1.0F, 1.0F, 0.95F}, frame.interactionHint, 1.0F);
		}

		if (gameplay.messageSecondsRemaining > 0.0F && !gameplay.messageText.empty())
		{
			const float alpha = glm::clamp(gameplay.messageSecondsRemaining / 0.4F, 0.0F, 1.0F);
			const glm::vec2 size = canvas.textSize(gameplay.messageText, 1.25F);
			const glm::vec2 position(frame.origin.x + frame.size.x * 0.5F - size.x * 0.5F,
				frame.origin.y + frame.size.y * 0.24F);
			canvas.rectFilled(position - glm::vec2(12.0F, 6.0F), position + size + glm::vec2(12.0F, 6.0F),
				{0.0F, 0.0F, 0.0F, alpha * 0.6F}, 6.0F);
			canvas.text(position, {1.0F, 0.95F, 0.8F, alpha}, gameplay.messageText, 1.25F);
		}
	}
}
