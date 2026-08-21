#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "GameForger/Core/Component.hpp"

namespace gameforger::core
{
	class GameObject final
	{
	public:
		GameObject()
		{
			// Every GameObject always has a TransformComponent attached by default
			transform_ = addComponent<TransformComponent>();
		}

		explicit GameObject(std::string name, const int id = 0)
			: name_(std::move(name)), id_(id)
		{
			transform_ = addComponent<TransformComponent>();
		}

		~GameObject() = default;

		GameObject(GameObject&& other) noexcept = default;
		GameObject& operator=(GameObject&& other) noexcept = default;

		GameObject(const GameObject&) = delete;
		GameObject& operator=(const GameObject&) = delete;

		[[nodiscard]] int id() const noexcept { return id_; }
		void setId(const int id) noexcept { id_ = id; }

		[[nodiscard]] const std::string& name() const noexcept { return name_; }
		void setName(std::string name) noexcept { name_ = std::move(name); }

		[[nodiscard]] bool activeSelf() const noexcept { return activeSelf_; }
		void setActive(const bool active) noexcept { activeSelf_ = active; }

		[[nodiscard]] const std::vector<std::string>& tags() const noexcept { return tags_; }
		std::vector<std::string>& tags() noexcept { return tags_; }

		[[nodiscard]] TransformComponent& transform() noexcept { return *transform_; }
		[[nodiscard]] const TransformComponent& transform() const noexcept { return *transform_; }

		template<typename T, typename... Args>
		requires std::is_base_of_v<Component, T>
		T* addComponent(Args&&... args)
		{
			auto comp = std::make_unique<T>(std::forward<Args>(args)...);
			T* ptr = comp.get();
			ptr->setGameObject(this);
			components_.push_back(std::move(comp));
			return ptr;
		}

		template<typename T>
		requires std::is_base_of_v<Component, T>
		[[nodiscard]] T* getComponent() const noexcept
		{
			for (const auto& comp : components_)
			{
				if (auto* casted = dynamic_cast<T*>(comp.get()))
				{
					return casted;
				}
			}
			return nullptr;
		}

		template<typename T>
		requires std::is_base_of_v<Component, T>
		[[nodiscard]] std::vector<T*> getComponents() const
		{
			std::vector<T*> result;
			for (const auto& comp : components_)
			{
				if (auto* casted = dynamic_cast<T*>(comp.get()))
				{
					result.push_back(casted);
				}
			}
			return result;
		}

		template<typename T>
		requires std::is_base_of_v<Component, T>
		bool removeComponent()
		{
			const auto it = std::find_if(
				components_.begin(), components_.end(),
				[](const std::unique_ptr<Component>& comp) {
					return dynamic_cast<T*>(comp.get()) != nullptr;
				});

			if (it != components_.end())
			{
				components_.erase(it);
				return true;
			}
			return false;
		}

		[[nodiscard]] const std::vector<std::unique_ptr<Component>>& components() const noexcept
		{
			return components_;
		}

	private:
		int id_ = 0;
		std::string name_ = "GameObject";
		bool activeSelf_ = true;
		std::vector<std::string> tags_;
		TransformComponent* transform_ = nullptr;
		std::vector<std::unique_ptr<Component>> components_;
	};
}
