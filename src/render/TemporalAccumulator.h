#pragma once
#include <cstdint>

namespace neurus {

/**
 * @brief Shadow accumulation alpha mode.
 */
enum class ShadowAlphaMode : uint32_t
{
    FixedEMA    = 0,  ///< Fixed alpha = 1/8 per frame
    MovingAvg   = 1,  ///< Alpha = 1/(frameCount + 1) — true moving average
};

/**
 * @brief Computes the temporal blend alpha for shadow accumulation.
 *
 * frameCount = 0 always returns alpha = 1.0 (overwrite / no history).
 *
 * @param mode       Blend mode (FixedEMA or MovingAvg).
 * @param frameCount Number of frames accumulated so far (0 = first frame).
 * @return Alpha value in (0, 1] for EMA blend: accum = lerp(prev, sample, alpha).
 */
inline float ComputeShadowAlpha(ShadowAlphaMode mode, uint32_t frameCount)
{
    if (frameCount == 0) return 1.0f;  // overwrite on first frame / reset

    switch (mode)
    {
    case ShadowAlphaMode::FixedEMA:     return 1.0f / 8.0f;
    case ShadowAlphaMode::MovingAvg:    {
        // Cap iteration so alpha never drops below 1/9 — prevents
        // accumulation from becoming visually frozen after many frames.
        static constexpr uint32_t kMaxFrames = 8;
        const uint32_t capped = (frameCount > kMaxFrames) ? kMaxFrames : frameCount;
        return 1.0f / static_cast<float>(capped + 1);
    }
    }

    return 1.0f;
}

} // namespace neurus
