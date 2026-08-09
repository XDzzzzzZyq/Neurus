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
 *   - Compile stays a non-undoable lifecycle action; Create Shader is
 *     undoable via ShaderLinkOp (the Editor records it; undo drops the pooled
 *     reference, redo relinks it - see editor.instructions.md).
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
	 * @brief Subscribes to shader events on the given EventQueue.
	 *
	 * Registers lambda handlers that forward each event to the
	 * corresponding free-function handler. Must be called once during
	 * initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 * @param ops Operation sink; records shader content edits for undo/redo.
	 */
	void Init(EventQueue& bus, IOperationSink& ops) override;

private:
	// --- Code-edit gesture state (ShaderEditBegin -> ShaderEditEnd) ---
	bool            m_codeEditing = false;   ///< True between begin and end.
	const neurus::UID* m_editObject = nullptr; ///< Object captured at gesture start.
	int             m_editStage   = 0;       ///< Stage captured at gesture start.
	std::string     m_beforeCode;            ///< Code snapshot at gesture start.
};

} // namespace neurus