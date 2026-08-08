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

Environment::Environment(std::shared_ptr<ImageData> data)
{
	o_type = ObjectID::GOType::GO_ENVIR;
	o_name = "Environment";
	SetEquirectData(std::move(data));
}

Environment::~Environment() = default;

} // namespace neurus
