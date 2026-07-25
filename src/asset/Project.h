#pragma once

/**
 * @file Project.h
 * @brief Project layer - the DNA/persistence layer for scene serialization.
 *
 * Provides the Project class which encapsulates a Scene and its serialization
 * to/from .neurus.json files via cereal JSON archives. The Project is the
 * top-level serialization unit: it owns the Scene.
 *
 * Architecture:
 * - Project owns Scene via unique_ptr (enables move semantics for factory pattern)
 * - New() creates an empty project with default-constructed Scene
 * - Open(path) deserializes from a .neurus.json file
 * - Save(path) / Save() serializes to .neurus.json
 * - IsDirty() tracks unsaved modifications
 *
 * Dependencies: cereal, scene types only (no Qt, Vulkan, or renderer).
 */

#include <memory>
#include <string>

#include <cereal/cereal.hpp>

#include "core/Log.h"

#include "scene/Scene.h"
#include "render/RenderConfig.h"

namespace neurus::project
{

/**
 * @brief Top-level project container - owns Scene and handles persistence.
 *
 * Project is the serialization root. It owns the Scene (all scene objects)
 * and tracks file path and dirty state. Factory methods New() and Open()
 * are the only ways to construct a Project; the default constructor is private.
 *
 * Serialization format (cereal JSON):
 * @code
 * {
 *   "project": {
 *     "proj_scene": {
 *       "cam_list": [],
 *       "mesh_list": [],
 *       "light_list": [],
 *       "sprite_list": [],
 *       "dLine_list": [],
 *       "dPoints_list": []
 *     },
 *     "proj_config": {
 *       "r_pipeline": "Deferred",
 *       "r_aa": "None",
 *       ...
 *     }
 *   }
 * }
 * @endcode
 */
class Project
{
public:
	// --- Factory methods ---

	/**
	 * @brief Creates a new empty project with a default-constructed Scene.
	 * @return Project with empty scene pools, no file path, and dirty = false.
	 */
	static Project New();

	/**
	 * @brief Opens an existing project from a .neurus.json file.
	 * @param path Filesystem path to the .neurus.json file.
	 * @param assetDir Optional base directory for resolving OBJ mesh paths.
	 *                 After deserialization, all meshes have ReloadMeshData(assetDir) called
	 *                 to reload vertex/index buffers from disk. If empty, o_meshPath is used as-is.
	 * @return Project deserialized from the file with mesh geometry reloaded.
	 * @throws std::runtime_error if the file cannot be opened.
	 */
	static Project Open(const std::string& path, const std::string& assetDir = "");

	/**
	 * @brief Creates a default project with camera, sphere mesh, and point light.
	 *
	 * Constructs a pre-configured project matching the DefaultScene factory:
	 *   - Camera: pos(0,2,5), target(0,0,0), FOV 60°, near 0.1, far 100
	 *   - Mesh:   Loaded from the given OBJ path, default PBR material
	 *   - Light:  Point light, pos(3,3,3), white, power 10, radius 0.05
	 *
	 * @param objPath Path to the sphere.obj file.
	 * @return Project with a populated default scene.
	 */
	static Project CreateDefault(const std::string& objPath);

	// --- Persistence ---

	/**
	 * @brief Saves the project to the given path as .neurus.json.
	 * @param path Filesystem path for the output file.
	 * @throws std::runtime_error if the file cannot be created.
	 * @note Updates proj_filePath and clears the dirty flag on success.
	 */
	void Save(const std::string& path);

	/**
	 * @brief Saves the project, overwriting the current file.
	 * @throws std::runtime_error if no file path has been set (call Save(path) first).
	 */
	void Save();

	// --- Dirty tracking ---

	/**
	 * @brief Returns whether the project has unsaved modifications.
	 * @return true if the scene has been modified since the last save.
	 */
	bool IsDirty() const { return proj_dirty; }

	/**
	 * @brief Marks the project as modified (called by editor controllers).
	 */
	void MarkDirty() { proj_dirty = true; }

	// --- Scene access ---

	/**
	 * @brief Returns a mutable reference to the scene.
	 * @return Reference to the owned Scene.
	 */
	Scene& GetScene() { return *proj_scene; }

	/**
	 * @brief Returns a const reference to the scene.
	 * @return Const reference to the owned Scene.
	 */
	const Scene& GetScene() const { return *proj_scene; }

	// --- Render config access ---

	/**
	 * @brief Returns a mutable reference to the render configuration.
	 * @return Reference to the owned RenderConfig.
	 */
	RenderConfig& GetRenderConfig() { return proj_config; }

	/**
	 * @brief Returns a const reference to the render configuration.
	 * @return Const reference to the owned RenderConfig.
	 */
	const RenderConfig& GetRenderConfig() const { return proj_config; }

	// --- File path ---

	/**
	 * @brief Returns the current project file path.
	 * @return The path last used for Open() or Save(), or empty string if never saved.
	 */
	const std::string& GetFilePath() const { return proj_filePath; }

	// --- Cereal serialization ---

	/**
	 * @brief Cereal serialization entry point.
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 * @note Serializes scene data and RenderConfig. On input, missing
	 *       RenderConfig (old file format) defaults to initialized values.
	 */
	template<class Archive>
	void save(Archive& ar, const uint32_t /*version*/) const
	{
		ar(cereal::make_nvp("m_scene", *proj_scene),
		   CEREAL_NVP(proj_config));
	}

	template<class Archive>
	void load(Archive& ar, const uint32_t /*version*/)
	{
		// Scene — selections field added later, backward-compat
		try
		{
			ar(cereal::make_nvp("m_scene", *proj_scene));
		}
		catch (const cereal::Exception& e)
		{
			NEURUS_ERR("Project::load: " << e.what()
				<< " - selections defaulted (old project format).");
		}

		// If no camera was deserialized, add a default one
		if (proj_scene->cam_list.empty())
		{
			NEURUS_ERR("[Project] No camera in scene, adding default camera.");
			auto defaultCam = std::make_shared<Camera>();
			defaultCam->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
			defaultCam->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
			proj_scene->UseCamera(defaultCam);
		}

		// Config is optional — old files won't have it
		try
		{
			ar(CEREAL_NVP(proj_config));
		}
		catch (const cereal::Exception& e)
		{
			NEURUS_ERR("Project::load: " << e.what()
				<< " - proj_config defaulted (old project format).");
			proj_config = RenderConfig{};
		}
	}

private:
	/**
	 * @brief Default constructor - private; use New() or Open().
	 *
	 * Allocates the owned Scene via unique_ptr to enable move semantics,
	 * which is required for the factory pattern (New/Open) since UID
	 * deletes both copy and move on Scene types.
	 */
	Project();

	std::unique_ptr<Scene> proj_scene;   ///< Owned scene container (all scene objects)
	RenderConfig proj_config;           ///< Rendering configuration (persisted alongside scene)
	std::string proj_filePath;          ///< Path to the .neurus.json file (empty if never saved)
	bool proj_dirty = false;            ///< Unsaved modifications flag
};

} // namespace neurus::project

CEREAL_CLASS_VERSION(neurus::project::Project, 0)
