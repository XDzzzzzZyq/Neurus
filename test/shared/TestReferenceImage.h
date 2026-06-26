/**
 * @file TestReferenceImage.h
 * @brief Shared reference-image regression test utilities.
 *
 * Provides kReferenceDir, ComparePixels, and CheckReferenceOrGenerate
 * for the first-run-generates / second-run-compares pattern used by
 * GPU reference-image regression tests.
 */
#pragma once

#include "asset/ImageData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace neurus {
namespace test {

/// @brief Path to reference images, relative from build/debug/test/
static const char* kReferenceDir = "../../../test/render/reference/";

/// @brief Convenience helper: concatenates kReferenceDir with a suffix.
static std::string ReferencePath(const std::string& suffix)
{
	return std::string(kReferenceDir) + suffix;
}

/**
 * @brief Compare two RGBA8 images pixel-by-pixel.
 * @return Number of pixels where any channel exceeds maxDiffPerChannel.
 */
inline int ComparePixels(
	const uint8_t* a, const uint8_t* b,
	int width, int height,
	int maxDiffPerChannel = 2)
{
	const size_t pixelCount = static_cast<size_t>(width) * height * 4;
	int badPixels = 0;
	for (size_t i = 0; i < pixelCount; i += 4)
	{
		for (int c = 0; c < 4; ++c)
		{
			if (std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c])) > maxDiffPerChannel)
			{
				++badPixels;
				break;
			}
		}
	}
	return badPixels;
}

/**
 * @brief Reference image regression check.
 *
 * Caller must have already saved the captured image to `refPath + ".tmp"`.
 *
 * - If reference doesn't exist: renames .tmp → refPath, returns -1
 *   (caller should issue GTEST_SKIP)
 * - If reference exists: loads both via ImageData(path) constructor,
 *   compares via ComparePixels, removes .tmp, returns bad pixel count
 *
 * @param refPath          Full path to the reference PNG file.
 * @param maxDiffPerChannel Per-channel pixel tolerance (default 2).
 * @return -1 if reference was just generated (caller: GTEST_SKIP),
 *         >=0 bad pixel count otherwise.
 */
inline int CheckReferenceOrGenerate(const std::string& refPath, int maxDiffPerChannel = 2)
{
	const std::string tmpPath = refPath + ".tmp";

	if (!std::filesystem::exists(refPath))
	{
		// First run — rename .tmp to reference
		std::filesystem::create_directories(std::filesystem::path(refPath).parent_path());
		std::rename(tmpPath.c_str(), refPath.c_str());
		return -1;
	}

	// Second run — load and compare
	auto tmpResult = ImageData(tmpPath);
	auto refResult = ImageData(refPath);

	if (!tmpResult.IsValid() || !refResult.IsValid())
		return -2;  // load failure

	int bad = ComparePixels(
		tmpResult.GetPixelData().data(), refResult.GetPixelData().data(),
		static_cast<int>(tmpResult.GetWidth()), static_cast<int>(tmpResult.GetHeight()),
		maxDiffPerChannel);

	std::remove(tmpPath.c_str());
	return bad;
}

} // namespace test
} // namespace neurus
