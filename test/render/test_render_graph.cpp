/**
 * @file test_render_graph.cpp
 * @brief Wave 1 unit tests for the RenderGraph scaffolding.
 *
 * Scope: pure topology / declaration validation. No Vulkan calls: MockPass
 * inherits `Pass` and overrides `GetIO()` to describe its image I/O; its
 * Record() is a no-op. Barrier and descriptor auto-injection are Wave 2+
 * concerns and covered by GPU tests there.
 */

#include <gtest/gtest.h>

#include "render/render_graph/RenderGraph.h"
#include "render/passes/Pass.h"
#include "render/RenderCache.h"

#include <vector>

using namespace neurus;

namespace {

// ---------------------------------------------------------------------------
// MockPass — CPU-only stub satisfying Pass and returning a hand-built PassIO.
// ---------------------------------------------------------------------------
class MockPass : public Pass
{
public:
	MockPass(std::string name,
	         std::vector<AttachmentName> reads,
	         std::vector<AttachmentName> writes)
	    : Pass()
	{
		m_io.name = std::move(name);
		uint32_t binding = 0;
		for (auto r : reads)  m_io.reads .push_back({r, binding++});
		binding = 0;
		for (auto w : writes) m_io.writes.push_back({w, binding++});
	}

	// --- Pass ---
	void   Record(vk::CommandBuffer, RenderCache&, const RenderContext&) override {}
	PassIO GetIO() const override { return m_io; }

private:
	PassIO m_io;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// AddPass — materializes one socket per declared resource.
// ---------------------------------------------------------------------------

TEST(RenderGraphTest, AddPassDeclaresSockets)
{
	MockPass p("mock",
	           /*reads=*/  {AttachmentName::Position, AttachmentName::Normal},
	           /*writes=*/ {AttachmentName::HDRColor});

	RenderGraph g;
	auto* node = g.AddPass(&p);

	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->InputCount(),  2u);
	EXPECT_EQ(node->OutputCount(), 1u);
	EXPECT_EQ(node->data.pass, static_cast<Pass*>(&p));
}

TEST(RenderGraphTest, AddPassRejectsNullptr)
{
	RenderGraph g;
	EXPECT_THROW(g.AddPass(nullptr), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Connect — wires a producer's write into a consumer's read.
// ---------------------------------------------------------------------------

TEST(RenderGraphTest, ConnectWiresMatchingResource)
{
	MockPass producer("prod", {}, {AttachmentName::Position});
	MockPass consumer("cons", {AttachmentName::Position}, {});

	RenderGraph g;
	auto* pn = g.AddPass(&producer);
	auto* cn = g.AddPass(&consumer);

	EXPECT_TRUE(g.Connect(pn, AttachmentName::Position, cn));

	// Duplicate connection is rejected by the underlying Graph<>.
	EXPECT_FALSE(g.Connect(pn, AttachmentName::Position, cn));
}

TEST(RenderGraphTest, ConnectRejectsMissingSocket)
{
	MockPass producer("prod", {}, {AttachmentName::Position});
	MockPass consumer("cons", {AttachmentName::Normal}, {});

	RenderGraph g;
	auto* pn = g.AddPass(&producer);
	auto* cn = g.AddPass(&consumer);

	// Producer never declared Normal; consumer never declared Position.
	// Either direction fails without side effects.
	EXPECT_FALSE(g.Connect(pn, AttachmentName::Normal,   cn));
	EXPECT_FALSE(g.Connect(pn, AttachmentName::Position, cn));
}

// ---------------------------------------------------------------------------
// Compile — validates connectivity and produces a topological order.
// ---------------------------------------------------------------------------

TEST(RenderGraphTest, CompileLinearChain)
{
	// A(writes X) → B(reads X, writes Y) → C(reads Y)
	MockPass a("A", {}, {AttachmentName::Position});
	MockPass b("B", {AttachmentName::Position}, {AttachmentName::HDRColor});
	MockPass c("C", {AttachmentName::HDRColor}, {});

	RenderGraph g;
	auto* na = g.AddPass(&a);
	auto* nb = g.AddPass(&b);
	auto* nc = g.AddPass(&c);

	ASSERT_TRUE(g.Connect(na, AttachmentName::Position, nb));
	ASSERT_TRUE(g.Connect(nb, AttachmentName::HDRColor, nc));

	g.Compile();

	const auto& order = g.CompiledOrder();
	ASSERT_EQ(order.size(), 3u);
	EXPECT_EQ(order[0], na);
	EXPECT_EQ(order[1], nb);
	EXPECT_EQ(order[2], nc);
}

TEST(RenderGraphTest, CompileAllowsExternalInputs)
{
	// An input with no in-graph producer is "external" (produced by a
	// RenderCache attachment or a legacy pass during incremental migration).
	// Compile() must accept it rather than treating it as an error.
	MockPass consumer("cons", {AttachmentName::Position}, {});

	RenderGraph g;
	auto* cn = g.AddPass(&consumer);

	g.Compile();

	const auto& order = g.CompiledOrder();
	ASSERT_EQ(order.size(), 1u);
	EXPECT_EQ(order[0], cn);
}

TEST(RenderGraphTest, CompileRejectsCycle)
{
	// Build a 2-node cycle: A reads X and writes Y; B reads Y and writes X.
	MockPass a("A", {AttachmentName::Position}, {AttachmentName::HDRColor});
	MockPass b("B", {AttachmentName::HDRColor}, {AttachmentName::Position});

	RenderGraph g;
	auto* na = g.AddPass(&a);
	auto* nb = g.AddPass(&b);

	// A → B via HDRColor; B → A via Position.
	ASSERT_TRUE(g.Connect(na, AttachmentName::HDRColor, nb));
	ASSERT_TRUE(g.Connect(nb, AttachmentName::Position, na));

	EXPECT_THROW(g.Compile(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Execute preconditions.
// ---------------------------------------------------------------------------

TEST(RenderGraphTest, ExecuteThrowsIfNotCompiled)
{
	RenderGraph g;
	MockPass p("solo", {}, {});
	g.AddPass(&p);

	// Passing a null command buffer is fine here because Execute() throws
	// before any pass is invoked. This is a precondition-check test.
	RenderCache*         nullCache = nullptr;
	const RenderContext* nullCtx   = nullptr;
	EXPECT_THROW(g.Execute(vk::CommandBuffer{}, *nullCache, *nullCtx),
	             std::runtime_error);
}

// ---------------------------------------------------------------------------
// Reset — invalidates the compiled order without dropping nodes.
// ---------------------------------------------------------------------------

TEST(RenderGraphTest, ResetInvalidatesCompiledOrder)
{
	MockPass a("A", {}, {});
	RenderGraph g;
	g.AddPass(&a);
	g.Compile();

	EXPECT_EQ(g.CompiledOrder().size(), 1u);

	g.Reset();
	EXPECT_TRUE(g.CompiledOrder().empty());
}
