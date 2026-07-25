# Shader CPU/GPU Separation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the shader system to strictly separate CPU-side shader representation (ShaderUnit, Shader, RenderShader, ComputeShader) from GPU-side pipeline artifacts (PipelineCache, Pipeline), making SPIR-V and ShaderModule intermediate/temporary values.

**Architecture:** CPU Shader owns path/code/IR per stage in a `dict<ShaderType, ShaderUnit>`. ShaderLibrary becomes a Parse+Compile service (no caching). PipelineBuilder loses ShaderModule dependency. UploadManager gains UploadShader() to compile a CPU Shader → Pipeline. PipelineCache in RenderCache stores per-UID pipelines for future per-mesh custom shaders.

**Tech Stack:** C++20, Vulkan-HPP vk::raii, shaderc, Qt6

## Global Constraints

- C++20 standard (see root CMakeLists.txt)
- Allman braces, tabs for indentation, max 120 columns
- `#pragma once` in headers
- `m_` prefix for member variables, PascalCase for classes/methods
- No separate `Init()`/`Terminate()` — full RAII
- Non-copyable for GPU resource classes (`= delete` copy/assign)
- Vulkan-HPP `vk::raii` namespace for all Vulkan objects
- Do NOT call raw `vkDestroy*` — let RAII handle cleanup
- All image layout transitions through `Barrier::Transition()`, not raw barriers

---

### Task 1: ShaderUnit struct + ShaderParser return-by-value

**Files:**
- Create: `src/render/shaders/ShaderUnit.h`
- Modify: `src/render/shaders/ShaderParser.h` (signatures)
- Modify: `src/render/shaders/ShaderParser.cpp` (implementation, ~1281 lines)
- Modify: `src/render/shaders/ShaderGenerator.h` (signature only — `ShaderStruct&` stays same)
- Test: `test/render/test_shader_parser.cpp` (already uses these APIs — update for new signatures)

**Interfaces:**
- Produces: `struct ShaderUnit { std::string path; std::string code; ShaderStruct parsed; };`
- Produces: `ShaderStruct ShaderParser::ParseShaderFile(path, type)` — returns by value, empty struct on failure
- Produces: `ShaderStruct ShaderParser::ParseShaderCode(source, type)` — returns by value, empty struct on failure
- Consumes: `std::string ShaderGenerator::Generate(ShaderStruct& s)` — unchanged (already returns by value)

- [ ] **Step 1: Create `ShaderUnit.h`**

File: `src/render/shaders/ShaderUnit.h`

```cpp
#pragma once

#include "ShaderStruct.h"

#include <string>

namespace neurus {

struct ShaderUnit
{
    std::string path;         // Path to GLSL source file
    std::string code;         // Generated GLSL code (ShaderGenerator output)
    ShaderStruct parsed;      // Parsed IR (ShaderParser output)
};

} // namespace neurus
```

- [ ] **Step 2: Update `ShaderParser.h` — change ParseShaderFile/ParseShaderCode to return by value**

Current:
```cpp
static bool ParseShaderFile(const std::string& filepath, ShaderType type, ShaderStruct& out);
static bool ParseShaderCode(const std::string& source, ShaderType type, ShaderStruct& out);
```

New:
```cpp
static ShaderStruct ParseShaderFile(const std::string& filepath, ShaderType type);
static ShaderStruct ParseShaderCode(const std::string& source, ShaderType type);
```

- [ ] **Step 3: Update `ShaderParser.cpp` — implement return-by-value**

Replace `ParseShaderFile`:
```cpp
ShaderStruct ShaderParser::ParseShaderFile(const std::string& filepath, ShaderType type)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        NEURUS_ERR("[ShaderParser] Failed to open file: " << filepath);
        return ShaderStruct();
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return ParseShaderCode(buffer.str(), type);
}
```

Replace `ParseShaderCode` — change the entry point to create a local ShaderStruct, populate it, and return it:
```cpp
ShaderStruct ShaderParser::ParseShaderCode(const std::string& source, ShaderType type)
{
    ShaderStruct result;

    std::istringstream stream(source);
    std::string line;
    bool inBlockComment = false;
    // ... (the existing ~900-line parsing body, now referencing `result` instead of `out`)

    return result;  // NRVO / move
}
```

The internal parsing body stays identical except `out.` → `result.`. At the end, `return result;` instead of `return true;`.

- [ ] **Step 4: Build and fix compile errors**

```bash
cmake --build build --config Debug
```

Fix any callers of the old signature. The key callers are:
- `RenderShader::CompileStage()` (which calls `ParseShaderFile`)
- `ComputeShader::Compile()` (calls `ParseShaderFile`)
- `ShaderParser` test file

These will be updated in later tasks. For now, just make them compile by adding a `.get()` compatibility method or temporarily wrapping.

Actually, since these callers are being rewritten in Task 2, we can leave them temporarily broken and fix them in Task 2. Or add a compatibility overload. The cleanest approach: keep the old signature as a deprecated overload temporarily, remove it in Task 2.

Simplest approach: Keep a compatibility inline:
```cpp
// DEPRECATED — use return-by-value version instead
inline bool ParseShaderFile(const std::string& path, ShaderType type, ShaderStruct& out) {
    out = ParseShaderFile(path, type);
    return !out.IsEmpty();
}
```

Remove in Task 2.

- [ ] **Step 5: Run parser tests to verify**

```bash
make check FILTER="-R ShaderParser"
```

Expected: All pass.

- [ ] **Step 6: Commit**

```bash
git add src/render/shaders/ShaderUnit.h src/render/shaders/ShaderParser.h src/render/shaders/ShaderParser.cpp test/render/test_shader_parser.cpp
git commit -m "refactor(shader): add ShaderUnit, change ShaderParser to return by value"
```

---

### Task 2: CPU-side Shader refactor — Shader, RenderShader, ComputeShader

**Files:**
- Modify: `src/render/shaders/Shader.h`
- Modify: `src/render/shaders/Shader.cpp`
- Modify: `src/render/shaders/RenderShader.h`
- Modify: `src/render/shaders/RenderShader.cpp`
- Modify: `src/render/shaders/ComputeShader.h`
- Modify: `src/render/shaders/ComputeShader.cpp`

**Interfaces:**
- Consumes: `ShaderUnit` struct from Task 1
- Consumes: `ShaderStruct ShaderParser::ParseShaderFile(path, type)` from Task 1
- Consumes: `std::string ShaderGenerator::Generate(ShaderStruct&)` — unchanged
- Produces: `class Shader` — CPU-only base class with `m_stages` dict and `virtual bool ParseAndGenerate() = 0`
- Produces: `class RenderShader` — CPU-only, `ParseAndGenerate()` populates VERTEX+FRAGMENT stages
- Produces: `class ComputeShader` — CPU-only, `ParseAndGenerate()` populates COMPUTE stage

- [ ] **Step 1: Refactor `Shader.h` — CPU-only base class**

Remove:
- `vk::raii::Device` forward declaration
- `ShaderModule` forward declaration
- `m_source` member
- `m_modules` member
- `virtual Compile()`, `virtual CreateModule()`, `virtual IsValid()` methods
- `GetShaderModule()`, `GetSource()` methods
- `ShaderModule.h` include (not needed in .h, it was only for `shared_ptr<ShaderModule>`)

Add:
```cpp
#include "ShaderUnit.h"  // new

// Inside class Shader:
public:
    Shader(std::string name);  // simplified — no source string

    virtual bool ParseAndGenerate() = 0;  // CPU-only: parse → generate GLSL

    ShaderUnit& GetStage(ShaderType type);
    const ShaderUnit& GetStage(ShaderType type) const;

    ShaderStruct& GetParsedStruct(ShaderType type);
    const ShaderStruct& GetParsedStruct(ShaderType type) const;

    const std::string& GetGeneratedCode(ShaderType type) const;

    virtual ShaderType GetType() const = 0;

protected:
    std::string m_name;
    std::string m_errorMessage;
    std::unordered_map<ShaderType, ShaderUnit> m_stages;
```

- [ ] **Step 2: Update `Shader.cpp`**

```cpp
Shader::Shader(std::string name)
    : m_name(std::move(name))
{
}

ShaderUnit& Shader::GetStage(ShaderType type)
{
    return m_stages[type];
}

const ShaderUnit& Shader::GetStage(ShaderType type) const
{
    return m_stages.at(type);
}

ShaderStruct& Shader::GetParsedStruct(ShaderType type)
{
    return m_stages[type].parsed;
}

const ShaderStruct& Shader::GetParsedStruct(ShaderType type) const
{
    return m_stages.at(type).parsed;
}

const std::string& Shader::GetGeneratedCode(ShaderType type) const
{
    return m_stages.at(type).code;
}

std::string Shader::TypeToString(ShaderType type)
{
    // ... same as current implementation
}
```

- [ ] **Step 3: Refactor `RenderShader.h` — CPU-only**

Remove:
- `m_vertStruct`, `m_fragStruct` (replaced by `m_stages` entries)
- `m_vertPath`, `m_fragPath` (stored in ShaderUnit.path)
- `m_vertSpirv`, `m_fragSpirv` (SPIR-V is now intermediate/temporary)
- `m_device` pointer
- `Compile()`, `CreateModule()`, `IsValid()`, `GetVertexModule()`, `GetFragmentModule()`, `CreateModuleFromSpirv()`, `CompileStage()`
- `ShaderModule.h`, `ShaderCompiler.h` includes from .cpp
- `shaderc/shaderc.hpp` include from .cpp (moved to ShaderLibrary)

Add:
```cpp
class RenderShader : public Shader
{
public:
    RenderShader(std::string name, std::string vertPath, std::string fragPath);
    ~RenderShader() override = default;

    // Non-copyable, movable
    RenderShader(const RenderShader&) = delete;
    RenderShader& operator=(const RenderShader&) = delete;
    RenderShader(RenderShader&&) noexcept = default;
    RenderShader& operator=(RenderShader&&) noexcept = default;

    bool ParseAndGenerate() override;
    ShaderType GetType() const override { return ShaderType::VERTEX; }

    // Convenience accessors
    ShaderUnit& GetVertex() { return GetStage(ShaderType::VERTEX); }
    ShaderUnit& GetFragment() { return GetStage(ShaderType::FRAGMENT); }
    const std::string& GetVertPath() const;
    const std::string& GetFragPath() const;

    bool Recompile(ShaderType type);  // CPU-only: re-parse + re-generate

private:
    std::string m_vertPath;
    std::string m_fragPath;
};
```

- [ ] **Step 4: Implement `RenderShader.cpp`**

```cpp
RenderShader::RenderShader(std::string name, std::string vertPath, std::string fragPath)
    : Shader(std::move(name))
    , m_vertPath(std::move(vertPath))
    , m_fragPath(std::move(fragPath))
{
    m_stages[ShaderType::VERTEX]   = ShaderUnit{m_vertPath, {}, {}};
    m_stages[ShaderType::FRAGMENT] = ShaderUnit{m_fragPath, {}, {}};
    NEURUS_LOG("[RenderShader] Created '" << m_name << "'");
}

bool RenderShader::ParseAndGenerate()
{
    m_errorMessage.clear();

    // Parse vertex
    auto& vertUnit = m_stages[ShaderType::VERTEX];
    vertUnit.parsed = ShaderParser::ParseShaderFile(vertUnit.path, ShaderType::VERTEX);
    if (vertUnit.parsed.IsEmpty())
    {
        m_errorMessage = "Failed to parse vertex shader: " + vertUnit.path;
        NEURUS_ERR("[RenderShader] " << m_errorMessage);
        return false;
    }
    vertUnit.code = ShaderGenerator::Generate(vertUnit.parsed);

    // Parse fragment
    auto& fragUnit = m_stages[ShaderType::FRAGMENT];
    fragUnit.parsed = ShaderParser::ParseShaderFile(fragUnit.path, ShaderType::FRAGMENT);
    if (fragUnit.parsed.IsEmpty())
    {
        m_errorMessage = "Failed to parse fragment shader: " + fragUnit.path;
        NEURUS_ERR("[RenderShader] " << m_errorMessage);
        return false;
    }
    fragUnit.code = ShaderGenerator::Generate(fragUnit.parsed);

    NEURUS_LOG("[RenderShader] '" << m_name << "' parsed and generated");
    return true;
}

const std::string& RenderShader::GetVertPath() const { return m_vertPath; }
const std::string& RenderShader::GetFragPath() const { return m_fragPath; }

bool RenderShader::Recompile(ShaderType type)
{
    if (type != ShaderType::VERTEX && type != ShaderType::FRAGMENT)
    {
        NEURUS_ERR("[RenderShader] Recompile: unsupported type");
        return false;
    }

    auto& unit = m_stages[type];
    unit.parsed = ShaderParser::ParseShaderFile(unit.path, type);
    if (unit.parsed.IsEmpty())
    {
        m_errorMessage = "Failed to re-parse: " + unit.path;
        NEURUS_ERR("[RenderShader] " << m_errorMessage);
        return false;
    }
    unit.code = ShaderGenerator::Generate(unit.parsed);
    NEURUS_LOG("[RenderShader] Recompiled '" << m_name << "' stage " << TypeToString(type));
    return true;
}
```

- [ ] **Step 5: Refactor `ComputeShader.h` — CPU-only**

Remove:
- `m_struct`, `m_spirv`, `m_generatedSource`
- `Compile()`, `CreateModule()`, `IsValid()`, `GetModule()`, `Recompile()` (old versions)
- `ShaderCompiler.h`, `ShaderModule.h` includes from .cpp

Add:
```cpp
class ComputeShader : public Shader
{
public:
    ComputeShader(std::string name, std::string compPath);
    ~ComputeShader() override = default;

    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    ComputeShader(ComputeShader&&) noexcept = default;
    ComputeShader& operator=(ComputeShader&&) noexcept = default;

    bool ParseAndGenerate() override;
    ShaderType GetType() const override { return ShaderType::COMPUTE; }

    // Compute-specific
    void GetWorkgroupSize(uint32_t& x, uint32_t& y, uint32_t& z) const;
    bool Recompile();  // CPU-only

    // Default uniform configs
    struct Default { std::string name; std::string type; std::string value; };
    void SetDefault(const std::string& name, int value);
    void SetDefault(const std::string& name, float value);
    void SetDefault(const std::string& name, bool value);
    const std::vector<Default>& GetDefaults() const { return m_defaults; }

private:
    std::string m_compPath;
    std::vector<Default> m_defaults;
};
```

- [ ] **Step 6: Implement `ComputeShader.cpp`**

```cpp
ComputeShader::ComputeShader(std::string name, std::string compPath)
    : Shader(std::move(name))
    , m_compPath(std::move(compPath))
{
    m_stages[ShaderType::COMPUTE] = ShaderUnit{m_compPath, {}, {}};
    NEURUS_LOG("[ComputeShader] Created '" << m_name << "'");
}

bool ComputeShader::ParseAndGenerate()
{
    m_errorMessage.clear();

    auto& unit = m_stages[ShaderType::COMPUTE];
    unit.parsed = ShaderParser::ParseShaderFile(unit.path, ShaderType::COMPUTE);
    if (unit.parsed.IsEmpty())
    {
        m_errorMessage = "Failed to parse compute shader: " + unit.path;
        NEURUS_ERR("[ComputeShader] " << m_errorMessage);
        return false;
    }
    unit.code = ShaderGenerator::Generate(unit.parsed);

    NEURUS_LOG("[ComputeShader] '" << m_name << "' parsed and generated");
    return true;
}

void ComputeShader::GetWorkgroupSize(uint32_t& x, uint32_t& y, uint32_t& z) const
{
    const auto& s = m_stages.at(ShaderType::COMPUTE).parsed;
    x = s.local_size_x;
    y = s.local_size_y;
    z = s.local_size_z;
}

bool ComputeShader::Recompile()
{
    auto& unit = m_stages[ShaderType::COMPUTE];
    unit.parsed = ShaderParser::ParseShaderFile(unit.path, ShaderType::COMPUTE);
    if (unit.parsed.IsEmpty())
    {
        m_errorMessage = "Failed to re-parse: " + unit.path;
        NEURUS_ERR("[ComputeShader] " << m_errorMessage);
        return false;
    }
    unit.code = ShaderGenerator::Generate(unit.parsed);
    return true;
}

// SetDefault implementations — identical to current code
```

- [ ] **Step 7: Build and check for compile errors**

Temporarily comment out passes that use the old API. We'll fix them in Task 8.

```bash
cmake --build build --config Debug
```

Fix any errors in the core shader files. Pass errors are expected and will be fixed later.

- [ ] **Step 8: Commit**

```bash
git add src/render/shaders/Shader.h src/render/shaders/Shader.cpp
git add src/render/shaders/RenderShader.h src/render/shaders/RenderShader.cpp
git add src/render/shaders/ComputeShader.h src/render/shaders/ComputeShader.cpp
git commit -m "refactor(shader): CPU-only Shader with ShaderUnit dict, no GPU state"
```

---

### Task 3: ShaderLibrary — no-cache compilation service

**Files:**
- Modify: `src/render/shaders/ShaderLibrary.h`
- Modify: `src/render/shaders/ShaderLibrary.cpp`
- Modify: `src/render/shaders/ShaderCompiler.h` (minor — add `shaderc_shader_kind` mapping)
- Delete: `src/render/shaders/ShaderLib.h`
- Delete: `src/render/shaders/ShaderLib.cpp`

**Interfaces:**
- Consumes: CPU-only Shader classes from Task 2
- Produces: `unique_ptr<RenderShader> ShaderLibrary::ParseRenderShader(name, vertPath, fragPath)`
- Produces: `unique_ptr<ComputeShader> ShaderLibrary::ParseComputeShader(name, compPath)`
- Produces: `vector<uint32_t> ShaderLibrary::Compile(const ShaderUnit&, ShaderType, debugName)`
- Produces: `unordered_map<ShaderType, vector<uint32_t>> ShaderLibrary::CompileAll(const Shader&)`

- [ ] **Step 1: Rewrite `ShaderLibrary.h` — remove cache, add Compile API**

```cpp
#pragma once

#include "ShaderStruct.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neurus {

class Shader;
class RenderShader;
class ComputeShader;
struct ShaderUnit;

class ShaderLibrary
{
public:
    ShaderLibrary() = delete;
    ~ShaderLibrary() = delete;
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    // --- Parse (CPU side, no caching) ---
    static std::unique_ptr<RenderShader> ParseRenderShader(
        const std::string& name,
        const std::string& vertPath,
        const std::string& fragPath);

    static std::unique_ptr<ComputeShader> ParseComputeShader(
        const std::string& name,
        const std::string& compPath);

    // --- Compile (GLSL → SPIR-V) ---
    static std::vector<uint32_t> Compile(
        const ShaderUnit& stage,
        ShaderType type,
        const std::string& debugName);

    static std::unordered_map<ShaderType, std::vector<uint32_t>> CompileAll(
        const Shader& shader);

    // --- Build-in constants ---
    static const S_Const* GetBuildInConstant(const std::string& name);

private:
    static std::string ResolveShaderPath(const std::string& path);
};

} // namespace neurus
```

- [ ] **Step 2: Rewrite `ShaderLibrary.cpp`**

```cpp
#include "ShaderLibrary.h"
#include "Shader.h"
#include "ShaderCompiler.h"
#include "RenderShader.h"
#include "ComputeShader.h"
#include "core/Log.h"
#include <filesystem>

namespace neurus {

static ShaderCompiler& GetCompiler()
{
    static ShaderCompiler s_compiler;
    return s_compiler;
}

static std::string ResolveShaderPath(const std::string& path)
{
    // Same implementation as before — absolute check, NEURUS_SHADER_DIR, fallback
    // ... (copy from current ShaderLibrary.cpp, lines 130-179)
}

std::unique_ptr<RenderShader> ShaderLibrary::ParseRenderShader(
    const std::string& name,
    const std::string& vertPath,
    const std::string& fragPath)
{
    auto resolvedVert = ResolveShaderPath(vertPath);
    auto resolvedFrag = ResolveShaderPath(fragPath);

    auto shader = std::make_unique<RenderShader>(name, resolvedVert, resolvedFrag);
    if (!shader->ParseAndGenerate())
    {
        NEURUS_ERR("[ShaderLibrary] Failed to parse render shader '" << name << "'");
        return nullptr;
    }
    NEURUS_LOG("[ShaderLibrary] Parsed render shader '" << name << "'");
    return shader;
}

std::unique_ptr<ComputeShader> ShaderLibrary::ParseComputeShader(
    const std::string& name,
    const std::string& compPath)
{
    auto resolvedComp = ResolveShaderPath(compPath);

    auto shader = std::make_unique<ComputeShader>(name, resolvedComp);
    if (!shader->ParseAndGenerate())
    {
        NEURUS_ERR("[ShaderLibrary] Failed to parse compute shader '" << name << "'");
        return nullptr;
    }
    NEURUS_LOG("[ShaderLibrary] Parsed compute shader '" << name << "'");
    return shader;
}

std::vector<uint32_t> ShaderLibrary::Compile(
    const ShaderUnit& stage,
    ShaderType type,
    const std::string& debugName)
{
    auto& compiler = GetCompiler();
    shaderc_shader_kind kind;
    switch (type)
    {
        case ShaderType::VERTEX:   kind = shaderc_glsl_vertex_shader; break;
        case ShaderType::FRAGMENT: kind = shaderc_glsl_fragment_shader; break;
        case ShaderType::COMPUTE:  kind = shaderc_glsl_compute_shader; break;
        case ShaderType::GEOMETRY: kind = shaderc_glsl_geometry_shader; break;
        default:                   kind = shaderc_glsl_vertex_shader;
    }

    return compiler.CompileGlslToSpv(stage.code, kind, "main", debugName);
}

std::unordered_map<ShaderType, std::vector<uint32_t>> ShaderLibrary::CompileAll(
    const Shader& shader)
{
    std::unordered_map<ShaderType, std::vector<uint32_t>> results;
    // Iterate all stages in the shader
    // Since we don't know the types at compile time, we try known types
    for (int t = 0; t < 4; ++t)
    {
        auto type = static_cast<ShaderType>(t);
        // We can try-catch or check if stage exists
        // Simple approach: try to compile each known type
    }
    // Actually, simpler: just use the known shader type
    if (shader.GetType() == ShaderType::VERTEX)  // RenderShader
    {
        results[ShaderType::VERTEX] = Compile(
            shader.GetStage(ShaderType::VERTEX), ShaderType::VERTEX, shader.GetName() + "_vert");
        results[ShaderType::FRAGMENT] = Compile(
            shader.GetStage(ShaderType::FRAGMENT), ShaderType::FRAGMENT, shader.GetName() + "_frag");
    }
    else if (shader.GetType() == ShaderType::COMPUTE)
    {
        results[ShaderType::COMPUTE] = Compile(
            shader.GetStage(ShaderType::COMPUTE), ShaderType::COMPUTE, shader.GetName());
    }
    return results;
}

const S_Const* ShaderLibrary::GetBuildInConstant(const std::string& name)
{
    // Same static map as current code
    // ... (copy from current ShaderLibrary.cpp, lines 61-115)
}

} // namespace neurus
```

- [ ] **Step 3: Delete `ShaderLib.h` and `ShaderLib.cpp`**

Remove the files. Update `CMakeLists.txt` in Task 8.

- [ ] **Step 4: Build**

```bash
cmake --build build --config Debug
```

Fix any remaining compile issues in the shader core. Passes will still be broken.

- [ ] **Step 5: Commit**

```bash
git add src/render/shaders/ShaderLibrary.h src/render/shaders/ShaderLibrary.cpp
git rm src/render/shaders/ShaderLib.h src/render/shaders/ShaderLib.cpp
git commit -m "refactor(shader): ShaderLibrary as no-cache compile service, delete ShaderLib"
```

---

### Task 4: PipelineBuilder cleanup + remove ShaderModule dependency

**Files:**
- Modify: `src/render/PipelineBuilder.h`
- Modify: `src/render/PipelineBuilder.cpp`

**Interfaces:**
- Consumes: None from earlier tasks (independent)
- Produces: Removed `AddShaderStage(const ShaderModule&, ...)` overload
- Produces: Kept `AddShaderStage(const vk::PipelineShaderStageCreateInfo&)` overload

- [ ] **Step 1: Update `PipelineBuilder.h`**

Remove the `ShaderModule` overload declaration:
```cpp
// REMOVE this overload:
PipelineBuilder& AddShaderStage(const ShaderModule& module,
                                vk::ShaderStageFlagBits stage,
                                const char* entryPoint = "main");
```

Remove the `class ShaderModule;` forward declaration.

Keep only:
```cpp
PipelineBuilder& AddShaderStage(const vk::PipelineShaderStageCreateInfo& stageInfo);
```

- [ ] **Step 2: Update `PipelineBuilder.cpp`**

Remove the implementation of the ShaderModule overload (lines 14-21):
```cpp
// REMOVE the entire block:
PipelineBuilder& PipelineBuilder::AddShaderStage(
    const ShaderModule& module,
    vk::ShaderStageFlagBits stage,
    const char* entryPoint)
{
    p_stages.push_back(module.GetStageInfo(stage, entryPoint));
    return *this;
}
```

Remove the `#include "shaders/ShaderModule.h"` include.

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug
```

Expected: Some passes will fail to compile (they use the removed overload). We'll fix them in Task 7.

- [ ] **Step 4: Commit**

```bash
git add src/render/PipelineBuilder.h src/render/PipelineBuilder.cpp
git commit -m "refactor(render): remove ShaderModule overload from PipelineBuilder"
```

---

### Task 5: PipelineCache class

**Files:**
- Create: `src/render/resources/PipelineCache.h`
- Create: `src/render/resources/PipelineCache.cpp`

**Interfaces:**
- Produces: `class PipelineCache` with `Get/Store/Remove/Clear`
- Consumes: `struct Pipeline` from `src/render/Pipeline.h`

- [ ] **Step 1: Create `PipelineCache.h`**

```cpp
#pragma once

#include "Pipeline.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace neurus {

class PipelineCache
{
public:
    PipelineCache() = default;
    ~PipelineCache() = default;

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) noexcept = default;
    PipelineCache& operator=(PipelineCache&&) noexcept = default;

    Pipeline* Get(const std::string& key);
    const Pipeline* Get(const std::string& key) const;

    Pipeline& GetOrCreate(const std::string& key,
                          std::function<Pipeline()> factory);

    void Store(const std::string& key, Pipeline pipeline);
    void Remove(const std::string& key);
    void Clear();

private:
    std::unordered_map<std::string, Pipeline> m_pipelines;
};

} // namespace neurus
```

- [ ] **Step 2: Create `PipelineCache.cpp`**

```cpp
#include "PipelineCache.h"
#include "core/Log.h"

namespace neurus {

Pipeline* PipelineCache::Get(const std::string& key)
{
    auto it = m_pipelines.find(key);
    return (it != m_pipelines.end()) ? &it->second : nullptr;
}

const Pipeline* PipelineCache::Get(const std::string& key) const
{
    auto it = m_pipelines.find(key);
    return (it != m_pipelines.end()) ? &it->second : nullptr;
}

Pipeline& PipelineCache::GetOrCreate(const std::string& key,
                                     std::function<Pipeline()> factory)
{
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end())
        return it->second;

    auto [newIt, inserted] = m_pipelines.emplace(key, factory());
    NEURUS_LOG("[PipelineCache] Created pipeline '" << key << "'");
    return newIt->second;
}

void PipelineCache::Store(const std::string& key, Pipeline pipeline)
{
    m_pipelines[key] = std::move(pipeline);
    NEURUS_LOG("[PipelineCache] Stored pipeline '" << key << "'");
}

void PipelineCache::Remove(const std::string& key)
{
    m_pipelines.erase(key);
    NEURUS_LOG("[PipelineCache] Removed pipeline '" << key << "'");
}

void PipelineCache::Clear()
{
    m_pipelines.clear();
    NEURUS_LOG("[PipelineCache] Cleared all pipelines");
}

} // namespace neurus
```

- [ ] **Step 3: Commit**

```bash
git add src/render/resources/PipelineCache.h src/render/resources/PipelineCache.cpp
git commit -m "feat(render): add PipelineCache for per-UID pipeline storage"
```

---

### Task 6: RenderCache PipelineCache integration + UploadManager UploadShader

**Files:**
- Modify: `src/render/RenderCache.h`
- Modify: `src/render/RenderCache.cpp`
- Modify: `src/render/UploadManager.h`
- Modify: `src/render/UploadManager.cpp`

**Interfaces:**
- Consumes: `PipelineCache` from Task 5
- Consumes: CPU-only Shader classes from Task 2
- Consumes: `ShaderLibrary::CompileAll()` from Task 3
- Produces: `RenderCache::GetPipelineCache()`, `GetPipeline(uid)`, `UsePipeline(uid, pipeline)`, `RemovePipeline(uid)`
- Produces: `UploadManager::UploadShader(device, shader, viewMask) → Pipeline`

- [ ] **Step 1: Add PipelineCache to `RenderCache.h`**

Add include:
```cpp
#include "resources/PipelineCache.h"
```

Add public methods:
```cpp
PipelineCache& GetPipelineCache();
Pipeline* GetPipeline(int uid);
void UsePipeline(int uid, Pipeline pipeline);
void RemovePipeline(int uid);
```

Add private member:
```cpp
PipelineCache rc_pipelineCache;
```

Add `Clean()` update to clear the pipeline cache:
```cpp
void RenderCache::Clean()
{
    // ... existing clears ...
    rc_pipelineCache.Clear();
}
```

- [ ] **Step 2: Implement in `RenderCache.cpp`**

```cpp
PipelineCache& RenderCache::GetPipelineCache()
{
    return rc_pipelineCache;
}

Pipeline* RenderCache::GetPipeline(int uid)
{
    return rc_pipelineCache.Get(std::to_string(uid));
}

void RenderCache::UsePipeline(int uid, Pipeline pipeline)
{
    rc_pipelineCache.Store(std::to_string(uid), std::move(pipeline));
}

void RenderCache::RemovePipeline(int uid)
{
    rc_pipelineCache.Remove(std::to_string(uid));
}

void RenderCache::Clean()
{
    // ... existing clears ...
    rc_pipelineCache.Clear();
}
```

- [ ] **Step 3: Add `UploadShader` to `UploadManager.h`**

```cpp
#include "Pipeline.h"

// Forward declarations
class Shader;
class BufferLayout;

class UploadManager
{
public:
    // ... existing methods ...

    Pipeline UploadShader(const vk::raii::Device& device,
                          const Shader& shader,
                          uint32_t viewMask = 0,
                          const BufferLayout* vertexLayout = nullptr);

    // ... existing members ...
};
```

- [ ] **Step 4: Implement `UploadManager::UploadShader()` in `UploadManager.cpp`**

```cpp
#include "shaders/ShaderLibrary.h"
#include "shaders/Shader.h"
#include "buffers/BufferLayout.h"
#include "PipelineBuilder.h"

Pipeline UploadManager::UploadShader(
    const vk::raii::Device& device,
    const Shader& shader,
    uint32_t viewMask,
    const BufferLayout* vertexLayout)
{
    // 1. Compile all stages to SPIR-V
    auto spvs = ShaderLibrary::CompileAll(shader);

    if (spvs.empty())
    {
        throw std::runtime_error("UploadShader: no SPIR-V produced for '" + shader.GetName() + "'");
    }

    // 2. Create temporary modules and stage infos
    std::vector<vk::raii::ShaderModule> tempModules;
    std::vector<vk::PipelineShaderStageCreateInfo> stageInfos;
    tempModules.reserve(spvs.size());
    stageInfos.reserve(spvs.size());

    for (auto& [type, spirv] : spvs)
    {
        vk::ShaderStageFlagBits stageFlag;
        switch (type)
        {
            case ShaderType::VERTEX:   stageFlag = vk::ShaderStageFlagBits::eVertex; break;
            case ShaderType::FRAGMENT: stageFlag = vk::ShaderStageFlagBits::eFragment; break;
            case ShaderType::COMPUTE:  stageFlag = vk::ShaderStageFlagBits::eCompute; break;
            case ShaderType::GEOMETRY: stageFlag = vk::ShaderStageFlagBits::eGeometry; break;
            default: continue;
        }

        auto& mod = tempModules.emplace_back(
            device, vk::ShaderModuleCreateInfo({}, spirv));
        stageInfos.emplace_back(
            vk::PipelineShaderStageCreateInfo({}, stageFlag, *mod, "main"));
    }

    // 3. Build pipeline via PipelineBuilder
    PipelineBuilder builder;
    for (auto& si : stageInfos)
    {
        builder.AddShaderStage(si);
    }

    if (vertexLayout)
    {
        builder.SetVertexInput(*vertexLayout);
    }

    if (viewMask != 0)
    {
        // Shadow depth cubemap: multiview with depth-only
        builder.SetDepthFormat(vk::Format::eD32Sfloat);
        builder.SetViewMask(viewMask);
    }
    else
    {
        // Default: standard color + depth for GeometryPass
        builder.SetColorFormats({
            vk::Format::eR16G16B16A16Sfloat,  // Position
            vk::Format::eR16G16B16A16Sfloat,  // Normal
            vk::Format::eR8G8B8A8Srgb,        // Albedo
            vk::Format::eR8G8B8A8Unorm,       // MetallicRoughness
            vk::Format::eR32Uint,             // IDBuffer
        });
        builder.SetDepthFormat(vk::Format::eD32Sfloat);
    }

    if (shader.GetType() == ShaderType::COMPUTE)
    {
        return builder.BuildComputePipeline(device);
    }
    return builder.BuildGraphicsPipeline(device);
}
```

- [ ] **Step 5: Build**

```bash
cmake --build build --config Debug
```

Most passes will still be broken. Just verify RenderCache and UploadManager compile.

- [ ] **Step 6: Commit**

```bash
git add src/render/RenderCache.h src/render/RenderCache.cpp
git add src/render/UploadManager.h src/render/UploadManager.cpp
git commit -m "feat(render): add PipelineCache to RenderCache, UploadShader to UploadManager"
```

---

### Task 7: Pass-level shader lifecycle update (all 14 passes + Screenshot)

**Files:** Modify every pass's constructor + `BuildPipeline()` to use the new compile-inline-build pattern.

| Pass | Shader Type | Count |
|------|-------------|-------|
| GeometryPass | RenderShader | 1 |
| ShadowDepthPass | RenderShader ×2 | 2 |
| SSAOPass | ComputeShader | 1 |
| ShadowIntensityPass | ComputeShader ×2 | 2 |
| LightingPass | ComputeShader | 1 |
| IBLPass | ComputeShader ×2 | 2 |
| GizmoPass | ComputeShader | 1 |
| ComposePass | ComputeShader | 1 |
| FXAAPass | ComputeShader | 1 |
| Screenshot | ComputeShader | 1 (inline, no persistent member) |

**Interfaces:**
- Consumes: `ShaderLibrary::ParseRenderShader(name, vert, frag)` → `unique_ptr<RenderShader>`
- Consumes: `ShaderLibrary::ParseComputeShader(name, path)` → `unique_ptr<ComputeShader>`
- Consumes: `ShaderLibrary::Compile(const ShaderUnit&, type, debugName)` → `vector<uint32_t>`
- Consumes: PipelineBuilder without ShaderModule overload (from Task 4)

**Pattern** (apply to every pass):

Old:
```cpp
// Constructor:
, p_renderShader(ShaderLibrary::LoadRenderShader("Name", vertPath, fragPath))
{
    if (p_renderShader) { p_renderShader->CreateModule(device); }
    BuildPipeline(device, "Name");
}

// BuildPipeline:
auto& vertModule = *p_renderShader->GetVertexModule();
auto& fragModule = *p_renderShader->GetFragmentModule();
builder.AddShaderStage(vertModule, vk::ShaderStageFlagBits::eVertex)
       .AddShaderStage(fragModule, vk::ShaderStageFlagBits::eFragment)
       ...
       .BuildGraphicsPipeline(device);
```

New:
```cpp
// Constructor:
, m_shader(ShaderLibrary::ParseRenderShader("Name", vertPath, fragPath))
{
    BuildPipeline(device, "Name");
}

// BuildPipeline:
auto vertSpv = ShaderLibrary::Compile(m_shader->GetStage(VERTEX), VERTEX, "Name_vert");
auto fragSpv = ShaderLibrary::Compile(m_shader->GetStage(FRAGMENT), FRAGMENT, "Name_frag");

vk::raii::ShaderModule vertMod(device, vk::ShaderModuleCreateInfo({}, vertSpv));
vk::raii::ShaderModule fragMod(device, vk::ShaderModuleCreateInfo({}, fragSpv));

builder.AddShaderStage(vk::PipelineShaderStageCreateInfo({}, eVertex, *vertMod, "main"))
       .AddShaderStage(vk::PipelineShaderStageCreateInfo({}, eFragment, *fragMod, "main"))
       ...
       .BuildGraphicsPipeline(device);
```

For compute passes:
```cpp
// New:
, m_shader(ShaderLibrary::ParseComputeShader("Name", compPath))
{
    BuildPipeline(device, "Name");
}

// BuildPipeline:
auto spv = ShaderLibrary::Compile(m_shader->GetStage(COMPUTE), COMPUTE, "Name");
vk::raii::ShaderModule mod(device, vk::ShaderModuleCreateInfo({}, spv));

builder.AddShaderStage(vk::PipelineShaderStageCreateInfo({}, eCompute, *mod, "main"))
       ...
       .BuildComputePipeline(device);
```

**Member variable changes:**

Old member pattern:
```cpp
std::shared_ptr<RenderShader> p_renderShader;   // or p_computeShader
```

New member pattern:
```cpp
std::unique_ptr<RenderShader> m_shader;          // or std::unique_ptr<ComputeShader>
```

- [ ] **Step 1-14: Update each pass (one per micro-step)**

For each pass, rename `p_renderShader` / `p_computeShader` to `m_shader`, change to `unique_ptr`, and update constructor / BuildPipeline.

Key details per pass:

**GeometryPass**:
- `p_renderShader` → `std::unique_ptr<RenderShader> m_shader`
- Constructor: `m_shader = ShaderLibrary::ParseRenderShader(...)`
- BuildPipeline: compile inline, create temp modules, use `vk::PipelineShaderStageCreateInfo`

**ShadowDepthPass**:
- `m_multiviewShader` → `std::unique_ptr<RenderShader> m_multiviewShader`
- `m_sunShader` → `std::unique_ptr<RenderShader> m_sunShader`
- Two pipelines: multiview + sun, each compiled independently

**SSAOPass**:
- `p_computeShader` → `std::unique_ptr<ComputeShader> m_shader`
- Same for LightingPass, GizmoPass, ComposePass, FXAAPass

**ShadowIntensityPass**:
- `p_pointLightShader` → `std::unique_ptr<ComputeShader> m_pointLightShader`
- `p_sunLightShader` → `std::unique_ptr<ComputeShader> m_sunLightShader`
- Two pipelines

**IBLPass**:
- `p_irradianceShader` → `std::unique_ptr<ComputeShader> m_irradianceShader`
- `p_specularShader` → `std::unique_ptr<ComputeShader> m_specularShader`
- Two pipelines

**Screenshot** (no persistent member):
- Currently creates shader inline in `ExportShadowDepthEquirect()`
- Change: use `ShaderLibrary::ParseComputeShader()` + `ShaderLibrary::Compile()` inline instead of `ShaderLibrary::LoadComputeShader()`

- [ ] **Step 15: Remove `ShaderModule.h` include from all pass files**

Remove `#include "shaders/ShaderModule.h"` from all pass .h and .cpp files.

- [ ] **Step 16: Remove `ShaderModule.h` and `ShaderLib.h` includes from converted files**

Also remove `#include "shaders/ShaderLib.h"` from any converted files.

- [ ] **Step 17: Build and fix**

```bash
cmake --build build --config Debug
```

Fix any remaining errors:
- Missing includes for `ShaderModule.h` (replace with inline `vk::raii::ShaderModule`)
- Missing `shaderc/shaderc.hpp` includes (these were in Shader.cpp, now in ShaderLibrary.cpp)
- Any pass that still references old API

- [ ] **Step 18: Commit**

```bash
git add src/render/passes/ src/render/Screenshot.h src/render/Screenshot.cpp
git commit -m "refactor(render): update all passes to new CPU-shader lifecycle"
```

---

### Task 8: Scene + CMake + file cleanup

**Files:**
- Modify: `src/scene/Mesh.h`
- Modify: `src/render/CMakeLists.txt`

**Interfaces:**
- Consumes: all previous tasks
- Produces: `std::shared_ptr<Shader> Mesh::o_shader`

- [ ] **Step 1: Add `o_shader` to `Mesh.h`**

```cpp
#pragma once

#include "scene/Transform.h"
#include <memory>

namespace neurus {

class MeshData;
class Shader;  // forward declare

class Mesh
{
public:
    // ... existing members ...

    std::shared_ptr<Shader> o_shader;  // CPU-side shader (null = use default pass shader)
};

} // namespace neurus
```

- [ ] **Step 2: Update `src/render/CMakeLists.txt`**

Remove:
```
shaders/ShaderModule.h
shaders/ShaderModule.cpp
shaders/ShaderLib.h
shaders/ShaderLib.cpp
```

Add:
```
resources/PipelineCache.h
resources/PipelineCache.cpp
shaders/ShaderUnit.h
```

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug
```

Fix any CMake or compilation errors.

- [ ] **Step 4: Commit**

```bash
git add src/scene/Mesh.h
git add src/render/CMakeLists.txt
git commit -m "refactor: add o_shader to Mesh, update CMakeLists"
```

---

### Task 9: Test updates

**Files:**
- Modify: `test/render/test_shader_module.cpp` — rewrite to test inline module creation or delete
- Modify: `test/render/test_shader_classes.cpp` — update for CPU-only API
- Modify: `test/render/test_shader_parser.cpp` — update for return-by-value (verify Task 1 didn't break)

- [ ] **Step 1: Update `test_shader_parser.cpp`**

Change calls from:
```cpp
ShaderStruct out;
bool ok = ShaderParser::ParseShaderFile(path, VERTEX, out);
EXPECT_TRUE(ok);
```
to:
```cpp
auto out = ShaderParser::ParseShaderFile(path, VERTEX);
EXPECT_FALSE(out.IsEmpty());
```

- [ ] **Step 2: Rewrite `test_shader_module.cpp`**

Replace ShaderModule tests with a simpler test that verifies `vk::raii::ShaderModule` creation from SPIR-V works inline:

```cpp
TEST_F(InlineModuleTest, CreatesFromSpirV)
{
    if (!m_hasVulkan) GTEST_SKIP();
    
    std::vector<uint32_t> spirv(
        kMinimalCompSpv,
        kMinimalCompSpv + (kMinimalCompSpvSize / sizeof(uint32_t)));
    
    vk::raii::ShaderModule mod(*m_device, vk::ShaderModuleCreateInfo({}, spirv));
    EXPECT_NE(*mod, VK_NULL_HANDLE);
}
```

Remove all ShaderModule factory method tests (FromEmbedded, FromSpirV, etc.) unless they test functionality that doesn't exist elsewhere.

- [ ] **Step 3: Update `test_shader_classes.cpp`**

Changes:
- `RenderShader_CompileSucceeds` → `RenderShader_ParseAndGenerateSucceeds` — call `ParseAndGenerate()` instead of `Compile()`
- `RenderShader_IsValid_AfterCompile` → remove (no more IsValid)
- `RenderShader_GetStruct_NonEmpty` — keep, uses `GetParsedStruct(type)` instead of `GetStruct(type)`
- `RenderShader_GetVertexModule_NonNull` → remove (no more modules)
- `RenderShader_GetFragmentModule_NonNull` → remove
- `ComputeShader_CompileSucceeds` → `ComputeShader_ParseAndGenerateSucceeds`
- `ComputeShader_GetModule_NonNull` → remove
- `ComputeShader_GetWorkgroupSize_*` — keep (reads from ShaderUnit.parsed, same underlying data)
- `ShaderLibrary_LoadRenderShader_CacheHit` → remove (no caching)
- `ShaderLibrary_Clear_FlushesAll` → remove
- `ShaderLibrary_Reload_*` → remove
- `ShaderLibrary_ThreadSafety_*` → remove
- Keep: `GetBuildInConstant` tests, `SetDefault/GetDefaults`, `GetWorkgroupSize`
- Add: New test for `ShaderLibrary::ParseRenderShader` returns non-null with valid stages
- Add: New test for `ShaderLibrary::Compile` returns non-empty SPIR-V

- [ ] **Step 4: Build and run tests**

```bash
cmake --build build --config Debug
make check
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add test/render/test_shader_module.cpp test/render/test_shader_classes.cpp test/render/test_shader_parser.cpp
git commit -m "test(shader): update tests for CPU-only Shader API"
```

---

### Task 10: Final build & verification

- [ ] **Step 1: Full clean build**

```bash
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

Fix any remaining errors.

- [ ] **Step 2: Run full test suite**

```bash
make check
```

All tests must pass.

- [ ] **Step 3: Run Neurus.exe and check for validation errors**

```bash
$output = & "build/Debug/Neurus.exe" 2>&1; Start-Sleep -Seconds 3; $output | Select-String "VUID-"
```

Expected: Zero VUID violations.

- [ ] **Step 4: Verify ShaderModule.h and ShaderLib.h are fully removed**

```bash
Test-Path "src/render/shaders/ShaderModule.h"
Test-Path "src/render/shaders/ShaderLib.h"
```

Expected: False for both.

- [ ] **Step 5: Final commit with all remaining changes**

```bash
git add -A
git status  # verify no stale files
git commit -m "refactor(shader): complete CPU/GPU separation

- Shader, RenderShader, ComputeShader are now pure CPU (no GPU state)
- ShaderUnit struct holds path, generated code, and parsed IR per stage
- ShaderLibrary: no-cache Parse+Compile service
- ShaderModule deleted (inline vk::raii::ShaderModule usage)
- ShaderLib deleted (migrated to ShaderLibrary)
- PipelineCache added for per-UID pipeline storage
- UploadManager: UploadShader() compiles CPU Shader → Pipeline
- PipelineBuilder: removed ShaderModule overload
- Mesh: added o_shader (shared_ptr<Shader>)
- All 14 passes + Screenshot updated
- ShaderParser returns ShaderStruct by value

No validation error, no unreasonable reference image, all tests passed."
```
