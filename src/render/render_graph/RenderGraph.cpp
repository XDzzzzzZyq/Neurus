/**
 * @file RenderGraph.cpp
 * @brief Implementation of the Wave 1 RenderGraph scaffolding.
 *
 * See RenderGraph.h for the scope of this wave. This file provides no
 * barrier injection and no descriptor management — those arrive in Wave 2+.
 */

#include "RenderGraph.h"

#include "../RenderContext.h"

#include <chrono>
#include <stdexcept>
#include <string>

namespace neurus {

namespace {

/** @brief Stable, human-readable per-socket key derived from the attachment enum. */
std::string ResourceKey(AttachmentName name)
{
	return AttachmentNameToString(name);
}

} // anonymous namespace

RenderGraph::NodeT* RenderGraph::AddPass(Pass* pass)
{
	if (!pass)
		throw std::runtime_error("RenderGraph::AddPass: pass must be non-null");

	PassEntry entry;
	entry.pass = pass;
	// Ask the pass to describe its image I/O once, up-front, so subsequent
	// Compile() can validate connectivity without re-invoking user code.
	entry.io = pass->GetIO();

	std::string nodeName = entry.io.name;
	auto* node = m_graph.AddNode(std::move(nodeName), std::move(entry));

	// Materialize one SocketIn per declared read.
	for (const auto& b : node->data.io.reads)
	{
		auto* in = node->AddInput(ResourceKey(b.resource));
		in->data = b.resource;
	}

	// Materialize one SocketOut per declared write.
	for (const auto& b : node->data.io.writes)
	{
		auto* out = node->AddOutput(ResourceKey(b.resource));
		out->data = b.resource;
	}

	m_isCompiled = false;
	m_compiled.clear();
	return node;
}

bool RenderGraph::Connect(NodeT* producer, AttachmentName resource, NodeT* consumer)
{
	if (!producer || !consumer) return false;

	const std::string key = ResourceKey(resource);

	// Locate the producer's output socket for this resource.
	GraphT::SocketOutT* out = nullptr;
	for (const auto& o : producer->outputs)
		if (o->name == key) { out = o.get(); break; }
	if (!out) return false;

	// Locate the consumer's input socket for this resource.
	GraphT::SocketInT* in = nullptr;
	for (const auto& i : consumer->inputs)
		if (i->name == key) { in = i.get(); break; }
	if (!in) return false;

	const bool ok = m_graph.Connect(out, in);
	if (ok)
	{
		m_isCompiled = false;
		m_compiled.clear();
	}
	return ok;
}

void RenderGraph::Compile()
{
	// Inputs with no in-graph producer are "external": they are produced by
	// a resource outside this graph (RenderCache attachments, or a pass still
	// on the legacy path during incremental migration). They are valid and
	// simply do not contribute an ordering edge. Only cycles are rejected.
	//
	// Kahn's algorithm — throws std::runtime_error on cycle.
	m_compiled   = m_graph.TopologicalSort();
	m_isCompiled = true;
}

void RenderGraph::Execute(vk::CommandBuffer cmd,
                          RenderCache& cache,
                          const RenderContext& ctx,
                          GPUProfiler& profiler,
                          FrameProfile& frameProfile) const
{
	if (!m_isCompiled)
		throw std::runtime_error("RenderGraph::Execute: graph not compiled");

	// The graph drives profiling but stays a pure DAG executor: it brackets
	// each pass with GPU timestamps (profiler.BeginPass/EndPass), times the
	// CPU-side Record() call, and folds the PassStats the pass returns into the
	// per-frame profile. Passes remain unaware of profiling entirely.
	frameProfile.passes.clear();
	frameProfile.passes.reserve(m_compiled.size());
	frameProfile.drawCalls  = 0;
	frameProfile.dispatches = 0;

	double cpuRecordMs = 0.0;
	for (auto* node : m_compiled)
	{
		const std::string& name = node->data.io.name;

		profiler.BeginPass(cmd, name);
		const auto cpuBegin = std::chrono::steady_clock::now();

		const PassStats stats = node->data.pass->Record(cmd, cache, ctx);

		const auto cpuEnd = std::chrono::steady_clock::now();
		profiler.EndPass(cmd);

		const double passCpuMs =
			std::chrono::duration<double, std::milli>(cpuEnd - cpuBegin).count();
		cpuRecordMs += passCpuMs;

		PassProfile profile;
		profile.name       = name;
		profile.cpuMs      = passCpuMs;
		profile.gpuMs      = 0.0; // Back-filled by DeferredRenderer after Resolve().
		profile.drawCalls  = stats.drawCalls;
		profile.dispatches = stats.dispatches;
		frameProfile.passes.push_back(std::move(profile));

		frameProfile.drawCalls  += stats.drawCalls;
		frameProfile.dispatches += stats.dispatches;
	}

	frameProfile.passCount   = static_cast<uint32_t>(frameProfile.passes.size());
	frameProfile.cpuRecordMs = cpuRecordMs;
}

void RenderGraph::Clear()
{
	m_graph.nodes.clear();
	m_compiled.clear();
	m_isCompiled = false;
}

void RenderGraph::Reset()
{
	m_compiled.clear();
	m_isCompiled = false;
}

} // namespace neurus
