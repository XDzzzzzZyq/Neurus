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

Environment::~Environment() = default;

// -----------------------------------------------------------------------
// File path
// -----------------------------------------------------------------------

void Environment::SetEquirectPath(const std::string& path)
{
	o_equirectPath = path;
	if (!path.empty())
		o_equirectData = ImageData(path);
}

} // namespace neurus
