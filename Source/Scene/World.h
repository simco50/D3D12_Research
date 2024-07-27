#pragma once

#include "entt.hpp"

struct Mesh;
struct Material;
struct Light;
struct Skeleton;
struct Animation;

struct Transform
{
	Vector3 Position	= Vector3::Zero;
	Quaternion Rotation = Quaternion::Identity;
	Vector3 Scale		= Vector3::One;

	Matrix WorldPrev	= Matrix::Identity;
	Matrix World		= Matrix::Identity;
};

struct Identity
{
	String Name;
};

struct World
{
	entt::entity CreateEntity(const char* pName)
	{
		entt::entity e = Registry.create();
		Registry.emplace<Identity>(e, pName);
		return e;
	}

	template<typename T>
	T& GetComponent(entt::entity entity) { return Registry.get<T>(entity); }

	Array<Ref<Texture>> Textures;
	Array<Mesh> Meshes;
	Array<Material> Materials;
	Array<Skeleton> Skeletons;
	Array<Animation> Animations;

	entt::registry Registry;
	entt::entity Sunlight;
	entt::entity Camera;
};


