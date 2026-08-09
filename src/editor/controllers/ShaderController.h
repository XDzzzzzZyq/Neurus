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
 *   - ShaderStructEdited     → SetFieldValue   (mutate one ShaderStruct field, no bump)
 *   - ShaderFieldAdded       → AppendDefault   (append one default entry, no bump)
 *
 * Undo/redo (content edits only):
 *   - ShaderEditBegin/End bracket a code-editor burst; one SetShaderCodeOp is
 *     recorded on focus-out. Discrete struct edits record one SetShaderFieldOp
 *     each; field adds record one AddShaderFieldOp each.
 *   - Replay uses dedicated restore events (ShaderCodeRestored /
 *     ShaderFieldRestored / ShaderFieldAddRestored / ShaderFieldRemoved): each
 *     re-applies one edit dimension and bumps the ShaderUnit version so the
 *     panel refreshes. CPU-only, no recompile.
 *   - Create/Compile stay non-undoable lifecycle actions.
 *
 * Architecture:
 *   - Bound to a ControllerContext via Init() — no per-frame Update() polling.
 *   - Resolves the target mesh by integer UID (from each event) against the
 *     current scene via the context — no raw pointers in events.
 *   - No Editor*, no DeferredRenderer*, no UploadManager* — pure shader logic.
 *
 * @note ShaderController does not own the shader — it operates on a mesh
 *       resolved by UID from the scene (via the context).
 */

#pragma once

#include <string>

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"
#include "editor/events/ShaderEvents.h"

namespace neurus {

class ShaderController : public Controllers
{
public:
	ShaderController() = default;

	/**
	 * @brief Subscribes to shader events on the given context.
	 *
	 * Registers lambda handlers that forward each event to the
	 * corresponding free-function handler. Must be called once during
	 * initialization, before any events are enqueued.
	 *
	 * @param ctx Controller context (events, resources, ops, scene).
	 */
	void Init(ControllerContext& ctx) override;

private:
	// --- Code-edit gesture state (ShaderEditBegin -> ShaderEditEnd) ---
	bool m_codeEditing = false;    ///< True between begin and end.
	int  m_editObjectId = 0;       ///< Object UID captured at gesture start.
	int  m_editStage = 0;          ///< Stage captured at gesture start.
	std::string m_beforeCode;      ///< Code snapshot at gesture start.
};

} // namespace neurus