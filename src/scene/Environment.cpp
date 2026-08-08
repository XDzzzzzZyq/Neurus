/**
 * @file Environment.cpp
 * @brief Implementation of Environment - CPU-only IBL environment map source.
 */

#include "scene/Environment.h"
#include "asset/data/ImageData.h"
#include "core/Log.h"

namespace neurus
{

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

Environment::Environment()
{
	o_type = ObjectID::GOType::GO_ENVIR;
	o_name = "Environment";
}

Environment::Environment(std::shared_ptr<ImageData> data, std::string path)
{
	o_type = ObjectID::GOType::GO_ENVIR;
	o_name = "Environment";
	o_equirectPath = std::move(path);
	SetEquirectData(std::move(data));
}

Environment::~Environment() = default;

// -----------------------------------------------------------------------
// File path
// -----------------------------------------------------------------------

void Environment::SetEquirectPath(const std::string& path)
{
	o_equirectPath = path;
	if (!path.empty())
		o_equirectData = std::make_shared<ImageData>(path);
}

void Environment::LoadEquirectFromPath(const std::string& path)
{
	// Does NOT touch o_equirectPath (stays relative / portable) or
	// o_imageDataId (the pooled reference stays authoritative).
	o_equirectData = std::make_shared<ImageData>(path);
}

} // namespace neurus
