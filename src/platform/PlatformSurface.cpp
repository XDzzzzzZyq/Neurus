#include "platform/PlatformSurface.h"

#ifdef _WIN32
#include "platform/win/Win32Surface.h"
#elif __APPLE__
#include "platform/mac/MacOSSurface.h"
#endif

namespace neurus {

std::unique_ptr<PlatformSurface> CreatePlatformSurface()
{
#ifdef _WIN32
	return std::make_unique<Win32Surface>();
#elif __APPLE__
	return std::make_unique<MacOSSurface>();
#endif
}

} // namespace neurus
