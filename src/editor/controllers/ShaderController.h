/**
 * @file ShaderController.h
 * @brief Controller for per-mesh shader lifecycle (create, compile, save).
 */
#pragma once

#include "editor/controllers/Controllers.h"

namespace neurus
{

class DeferredRenderer;
class Editor;
class UploadManager;
struct ShaderCreateRequested;
struct ShaderCompileRequested;
struct ShaderCodeEdited;
struct ShaderModified;

class ShaderController : public Controllers
{
public:
	ShaderController(Editor* editor, DeferredRenderer* renderer, UploadManager* uploadManager);
	~ShaderController() override = default;

	ShaderController(const ShaderController&) = delete;
	ShaderController& operator=(const ShaderController&) = delete;

	void Init(EventQueue& bus) override;

private:
	void OnCreateShader(const ShaderCreateRequested& e);
	void OnCompileShader(const ShaderCompileRequested& e);
	void OnCodeEdited(const ShaderCodeEdited& e);
	void OnStructEdited(const ShaderModified& e);

	Editor*           c_editor        = nullptr;
	DeferredRenderer* c_renderer      = nullptr;
	UploadManager*    c_uploadManager = nullptr;
};

} // namespace neurus
