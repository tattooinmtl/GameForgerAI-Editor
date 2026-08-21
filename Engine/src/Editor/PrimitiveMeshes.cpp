#include "GameForger/Editor/PrimitiveMeshes.hpp"

#include <cmath>

#include <glm/geometric.hpp>

namespace gameforger::editor
{
	namespace
	{
		constexpr float kPi = 3.14159265358979323846F;

		void pushVertex(std::vector<float>& out, const glm::vec3& position, const glm::vec3& normal)
		{
			out.push_back(position.x);
			out.push_back(position.y);
			out.push_back(position.z);
			out.push_back(normal.x);
			out.push_back(normal.y);
			out.push_back(normal.z);
		}

		void pushTriangle(
			std::vector<float>& out,
			const glm::vec3& a, const glm::vec3& na,
			const glm::vec3& b, const glm::vec3& nb,
			const glm::vec3& c, const glm::vec3& nc)
		{
			pushVertex(out, a, na);
			pushVertex(out, b, nb);
			pushVertex(out, c, nc);
		}

		void pushFlatTriangle(
			std::vector<float>& out,
			const glm::vec3& a,
			const glm::vec3& b,
			const glm::vec3& c,
			const glm::vec3& normal)
		{
			pushTriangle(out, a, normal, b, normal, c, normal);
		}

		void pushFlatQuad(
			std::vector<float>& out,
			const glm::vec3& a,
			const glm::vec3& b,
			const glm::vec3& c,
			const glm::vec3& d,
			const glm::vec3& normal)
		{
			pushFlatTriangle(out, a, b, c, normal);
			pushFlatTriangle(out, a, c, d, normal);
		}

		void generateCube(std::vector<float>& out)
		{
			pushFlatQuad(out, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, {0, 0, 1});
			pushFlatQuad(out, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {0, 0, -1});
			pushFlatQuad(out, {1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, 0, 0});
			pushFlatQuad(out, {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, 0, 0});
			pushFlatQuad(out, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}, {0, 1, 0});
			pushFlatQuad(out, {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {0, -1, 0});
		}

		void generatePlane(std::vector<float>& out)
		{
			pushFlatQuad(out, {-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 0});
		}

		void generateUvSphereShell(
			std::vector<float>& out,
			const int startStack,
			const int stackCount,
			const int totalStacks,
			const int slices,
			const float radius,
			const float centerY)
		{
			for (int i = startStack; i < startStack + stackCount; ++i)
			{
				const float theta1 = kPi * static_cast<float>(i) / static_cast<float>(totalStacks) - kPi * 0.5F;
				const float theta2 = kPi * static_cast<float>(i + 1) / static_cast<float>(totalStacks) - kPi * 0.5F;
				for (int j = 0; j < slices; ++j)
				{
					const float phi1 = 2.0F * kPi * static_cast<float>(j) / static_cast<float>(slices);
					const float phi2 = 2.0F * kPi * static_cast<float>(j + 1) / static_cast<float>(slices);

					const glm::vec3 n1{std::cos(theta1) * std::cos(phi1), std::sin(theta1), std::cos(theta1) * std::sin(phi1)};
					const glm::vec3 n2{std::cos(theta1) * std::cos(phi2), std::sin(theta1), std::cos(theta1) * std::sin(phi2)};
					const glm::vec3 n3{std::cos(theta2) * std::cos(phi2), std::sin(theta2), std::cos(theta2) * std::sin(phi2)};
					const glm::vec3 n4{std::cos(theta2) * std::cos(phi1), std::sin(theta2), std::cos(theta2) * std::sin(phi1)};

					const glm::vec3 p1 = n1 * radius + glm::vec3(0, centerY, 0);
					const glm::vec3 p2 = n2 * radius + glm::vec3(0, centerY, 0);
					const glm::vec3 p3 = n3 * radius + glm::vec3(0, centerY, 0);
					const glm::vec3 p4 = n4 * radius + glm::vec3(0, centerY, 0);

					// Counter-clockwise winding for outward-facing normals
					pushTriangle(out, p1, n1, p4, n4, p2, n2);
					pushTriangle(out, p2, n2, p4, n4, p3, n3);
				}
			}
		}

		void generateSphere(std::vector<float>& out)
		{
			generateUvSphereShell(out, 0, 16, 16, 24, 1.0F, 0.0F);
		}

		void generateDisk(
			std::vector<float>& out,
			const float y,
			const float radius,
			const int segments,
			const glm::vec3& normal,
			const bool reverseWinding)
		{
			const glm::vec3 center{0, y, 0};
			for (int j = 0; j < segments; ++j)
			{
				const float a1 = 2.0F * kPi * static_cast<float>(j) / static_cast<float>(segments);
				const float a2 = 2.0F * kPi * static_cast<float>(j + 1) / static_cast<float>(segments);
				const glm::vec3 r1{std::cos(a1) * radius, y, std::sin(a1) * radius};
				const glm::vec3 r2{std::cos(a2) * radius, y, std::sin(a2) * radius};
				if (reverseWinding)
				{
					pushFlatTriangle(out, center, r2, r1, normal);
				}
				else
				{
					pushFlatTriangle(out, center, r1, r2, normal);
				}
			}
		}

		void generateCylinder(std::vector<float>& out)
		{
			constexpr int segments = 24;
			constexpr float radius = 1.0F;
			for (int j = 0; j < segments; ++j)
			{
				const float a1 = 2.0F * kPi * static_cast<float>(j) / static_cast<float>(segments);
				const float a2 = 2.0F * kPi * static_cast<float>(j + 1) / static_cast<float>(segments);
				const glm::vec3 n1{std::cos(a1), 0.0F, std::sin(a1)};
				const glm::vec3 n2{std::cos(a2), 0.0F, std::sin(a2)};
				const glm::vec3 bottom1 = n1 * radius + glm::vec3(0, -1, 0);
				const glm::vec3 bottom2 = n2 * radius + glm::vec3(0, -1, 0);
				const glm::vec3 top1 = n1 * radius + glm::vec3(0, 1, 0);
				const glm::vec3 top2 = n2 * radius + glm::vec3(0, 1, 0);

				// Counter-clockwise winding for outward-facing normals
				pushTriangle(out, bottom1, n1, top1, n1, top2, n2);
				pushTriangle(out, bottom1, n1, top2, n2, bottom2, n2);
			}
			generateDisk(out, 1.0F, radius, segments, {0, 1, 0}, true);
			generateDisk(out, -1.0F, radius, segments, {0, -1, 0}, false);
		}

		void generateCone(std::vector<float>& out)
		{
			constexpr int segments = 24;
			constexpr float radius = 1.0F;
			const glm::vec3 apex{0, 1, 0};
			for (int j = 0; j < segments; ++j)
			{
				const float a1 = 2.0F * kPi * static_cast<float>(j) / static_cast<float>(segments);
				const float a2 = 2.0F * kPi * static_cast<float>(j + 1) / static_cast<float>(segments);
				const glm::vec3 base1{std::cos(a1) * radius, -1.0F, std::sin(a1) * radius};
				const glm::vec3 base2{std::cos(a2) * radius, -1.0F, std::sin(a2) * radius};
				const float amid = 0.5F * (a1 + a2);
				const glm::vec3 normal = glm::normalize(
					glm::vec3(std::cos(amid) * radius, apex.y - base1.y, std::sin(amid) * radius));
				// Counter-clockwise winding for outward-facing normals
				pushFlatTriangle(out, base1, apex, base2, normal);
			}
			generateDisk(out, -1.0F, radius, segments, {0, -1, 0}, false);
		}

		void generateCapsule(std::vector<float>& out)
		{
			constexpr float radius = 0.5F;
			constexpr float halfHeight = 0.5F;
			constexpr int segments = 24;
			constexpr int halfStacks = 8;
			constexpr int totalStacks = 16;

			// Top hemisphere: stack 8..16, centered at +halfHeight
			generateUvSphereShell(out, halfStacks, halfStacks, totalStacks, segments, radius, halfHeight);
			// Bottom hemisphere: stack 0..8, centered at -halfHeight (with positive radius)
			generateUvSphereShell(out, 0, halfStacks, totalStacks, segments, radius, -halfHeight);

			for (int j = 0; j < segments; ++j)
			{
				const float a1 = 2.0F * kPi * static_cast<float>(j) / static_cast<float>(segments);
				const float a2 = 2.0F * kPi * static_cast<float>(j + 1) / static_cast<float>(segments);
				const glm::vec3 n1{std::cos(a1), 0.0F, std::sin(a1)};
				const glm::vec3 n2{std::cos(a2), 0.0F, std::sin(a2)};
				const glm::vec3 bottom1 = n1 * radius + glm::vec3(0, -halfHeight, 0);
				const glm::vec3 bottom2 = n2 * radius + glm::vec3(0, -halfHeight, 0);
				const glm::vec3 top1 = n1 * radius + glm::vec3(0, halfHeight, 0);
				const glm::vec3 top2 = n2 * radius + glm::vec3(0, halfHeight, 0);

				// Counter-clockwise winding for outward-facing normals
				pushTriangle(out, bottom1, n1, top1, n1, top2, n2);
				pushTriangle(out, bottom1, n1, top2, n2, bottom2, n2);
			}
		}
	}

	PrimitiveMeshData generatePrimitiveMesh(const PrimitiveType type)
	{
		PrimitiveMeshData mesh;
		switch (type)
		{
			case PrimitiveType::Cube:
				generateCube(mesh.vertices);
				break;
			case PrimitiveType::Sphere:
				generateSphere(mesh.vertices);
				break;
			case PrimitiveType::Cylinder:
				generateCylinder(mesh.vertices);
				break;
			case PrimitiveType::Cone:
				generateCone(mesh.vertices);
				break;
			case PrimitiveType::Plane:
				generatePlane(mesh.vertices);
				break;
			case PrimitiveType::Capsule:
				generateCapsule(mesh.vertices);
				break;
		}
		return mesh;
	}
}
