/**
 * @file ShaderController.h
 * @brief Shader lifecycle controller — event-driven, no per-frame polling.
 *
 * ShaderController translates shader editor events (create, compile, code edit,
 * struct edit) into shader data mutations. All shader logic is triggered by
 * events enqueued from the UI layer (ShaderEditorPanel).
 *
 * Stateless — all handler logic lives as free functions in the .cpp file.
 *
 * Event Mapping:
 *   - ShaderCreateRequested  → OnCreateShader  (load default, parse, compile, bump version)
 *   - ShaderCompileRequested → OnCompileShader (compile stage, store spv, bump version)
 *   - ShaderCodeEdited       → OnCodeEdited    (update ShaderUnit::code, no version bump)
 *   - ShaderStructEdited     → OnStructEdited  (mutate ShaderStruct IR, regenerate GLSL, no bump)
 *
 * Architecture:
 *   - Bound to an EventQueue via Init() — no per-frame Update() polling.
 *   - Operates on ObjectID* provided by each event (cast to Mesh* in .cpp).
 *   - No Editor*, no DeferredRenderer*, no UploadManager* — pure shader logic.
 *
 * @note ShaderController does not own the shader — it operates on a pointer
 *       provided by each event (via ObjectID* -> GetShader()).
 */

#pragma once

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"

namespace neurus {

class ShaderController : public Controllers
{
public:
	ShaderController() = default;

	/**
	 * @brief Subscribes to shader events on the given EventQueue.
	 *
	 * Registers lambda handlers that forward each event to the
	 * corresponding free-function handler. Must be called once during
	 * initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 */
	void Init(EventQueue& bus) override;
};

} // namespace neurus