/**
 * @file ResourceManager.cpp
 * @brief Implementation of ResourceManager non-template members.
 */

#include "core/ResourceManager.h"

namespace neurus
{

int ResourceManager::Register(std::shared_ptr<UID> obj)
{
	if (!obj)
		throw std::runtime_error("ResourceManager::Register: null resource");

	const int id = obj->GetObjectID();
	if (!resources_.emplace(id, std::move(obj)).second)
		throw std::runtime_error("Resource UID already exists: " + std::to_string(id));
	return id;
}

} // namespace neurus
