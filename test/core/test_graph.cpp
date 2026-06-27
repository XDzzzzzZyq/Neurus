/**
 * @file test_graph.cpp
 * @brief Unit tests for the DAG node-graph template classes (Graph.h).
 */

#include <gtest/gtest.h>

#include "core/Graph.h"

using namespace neurus;

// ============================================================================
// Test type aliases — use simple types for clarity
// ============================================================================

using SData    = float;          ///< Socket payload is a scalar float.
using NData    = std::string;    ///< Node payload is an operation label.
using SockIn   = SocketIn<SData, NData>;
using SockOut  = SocketOut<SData, NData>;
using NodeT    = Node<SData, NData>;
using GraphT   = Graph<SData, NData>;

// ============================================================================
// Node tests (independent of Graph)
// ============================================================================

TEST(NodeTest, ConstructEmpty)
{
	NodeT node;
	EXPECT_EQ(node.InputCount(), 0u);
	EXPECT_EQ(node.OutputCount(), 0u);
	EXPECT_TRUE(node.name.empty());
}

TEST(NodeTest, AddSockets)
{
	NodeT node;
	node.name = "TestNode";

	auto* in = node.AddInput("a");
	auto* out = node.AddOutput("b");

	ASSERT_NE(in, nullptr);
	ASSERT_NE(out, nullptr);
	EXPECT_EQ(in->name, "a");
	EXPECT_EQ(out->name, "b");
	EXPECT_EQ(in->owner, &node);
	EXPECT_EQ(out->owner, &node);
	EXPECT_EQ(node.InputCount(), 1u);
	EXPECT_EQ(node.OutputCount(), 1u);
	EXPECT_EQ(in->data, 0.0f);      // default SData
	EXPECT_FALSE(in->IsConnected());
}

TEST(NodeTest, SocketData)
{
	NodeT node;
	auto* in  = node.AddInput("x");
	auto* out = node.AddOutput("y");

	in->data  = 3.14f;
	out->data = 2.71f;

	EXPECT_FLOAT_EQ(in->data, 3.14f);
	EXPECT_FLOAT_EQ(out->data, 2.71f);
}

TEST(NodeTest, OwnsSockets)
{
	// Verify that sockets are destroyed when the node is destroyed.
	std::unique_ptr<NodeT> node = std::make_unique<NodeT>();
	auto* in  = node->AddInput("a");
	auto* out = node->AddOutput("b");

	node.reset(); // destroy node + its sockets

	// The raw pointers are now dangling — this test just ensures no
	// heap-use-after-free in the destructor path (valgrind/asan would catch).
	(void)in;
	(void)out;
	SUCCEED();
}

// ============================================================================
// Preset node → graph adoption
// ============================================================================

TEST(PresetTest, MoveNodeIntoGraph)
{
	auto preset = std::make_unique<NodeT>();
	preset->name = "Multiply";
	preset->data = "x * y";
	auto* inRaw  = preset->AddInput("lhs");
	auto* outRaw = preset->AddOutput("product");

	ASSERT_EQ(inRaw->owner, preset.get());
	ASSERT_EQ(outRaw->owner, preset.get());

	GraphT g;
	auto* adopted = g.AddNode(std::move(preset));

	EXPECT_EQ(adopted->name, "Multiply");
	EXPECT_EQ(adopted->data, "x * y");
	EXPECT_EQ(adopted->InputCount(), 1u);
	EXPECT_EQ(adopted->OutputCount(), 1u);

	// Owner pointers should still be valid (same heap address)
	EXPECT_EQ(inRaw->owner, adopted);
	EXPECT_EQ(outRaw->owner, adopted);
}

// ============================================================================
// Graph — connection tests
// ============================================================================

class GraphTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_a = m_graph.AddNode("A", "node A");
		m_b = m_graph.AddNode("B", "node B");
		m_c = m_graph.AddNode("C", "node C");

		m_aOut = m_a->AddOutput("a→");
		m_bIn  = m_b->AddInput("→b");
		m_bOut = m_b->AddOutput("b→");
		m_cIn  = m_c->AddInput("→c");
	}

	GraphT    m_graph;
	NodeT*    m_a = nullptr;
	NodeT*    m_b = nullptr;
	NodeT*    m_c = nullptr;
	SockOut*  m_aOut = nullptr;
	SockIn*   m_bIn  = nullptr;
	SockOut*  m_bOut = nullptr;
	SockIn*   m_cIn  = nullptr;
};

TEST_F(GraphTest, ConnectSingle)
{
	EXPECT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	EXPECT_EQ(m_bIn->source, m_aOut);
	EXPECT_EQ(m_aOut->targets.size(), 1u);
	EXPECT_EQ(m_aOut->targets[0], m_bIn);
}

TEST_F(GraphTest, ConnectSelfLoopRejected)
{
	auto* aIn = m_a->AddInput("self");
	EXPECT_FALSE(m_graph.Connect(m_aOut, aIn)); // same owner → self-loop
}

TEST_F(GraphTest, ConnectDuplicateRejected)
{
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	EXPECT_FALSE(m_graph.Connect(m_aOut, m_bIn)); // already connected
	EXPECT_EQ(m_aOut->targets.size(), 1u);         // no duplicate
}

TEST_F(GraphTest, ConnectAutoBreakOldSource)
{
	// m_bIn gets connected to m_aOut
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));

	// Create another output and connect it to m_bIn — old connection should break
	auto* cOut = m_c->AddOutput("c→");
	EXPECT_TRUE(m_graph.Connect(cOut, m_bIn));

	EXPECT_EQ(m_bIn->source, cOut);
	EXPECT_EQ(m_aOut->targets.size(), 0u);  // old edge removed
}

TEST_F(GraphTest, Disconnect)
{
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	m_graph.Disconnect(m_aOut, m_bIn);

	EXPECT_EQ(m_bIn->source, nullptr);
	EXPECT_EQ(m_aOut->targets.size(), 0u);
}

TEST_F(GraphTest, FanOut)
{
	// aOut → bIn, aOut → cIn
	EXPECT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	EXPECT_TRUE(m_graph.Connect(m_aOut, m_cIn));

	EXPECT_EQ(m_aOut->FanOut(), 2u);
	EXPECT_NE(std::find(m_aOut->targets.begin(), m_aOut->targets.end(), m_bIn), m_aOut->targets.end());
	EXPECT_NE(std::find(m_aOut->targets.begin(), m_aOut->targets.end(), m_cIn), m_aOut->targets.end());
}

TEST_F(GraphTest, RemoveNodeBreaksConnections)
{
	// a → b → c
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	ASSERT_TRUE(m_graph.Connect(m_bOut, m_cIn));

	// Remove middle node b
	EXPECT_TRUE(m_graph.RemoveNode(m_b));
	EXPECT_EQ(m_graph.NodeCount(), 2u);

	// a's output should be disconnected from b (b is gone)
	EXPECT_EQ(m_aOut->targets.size(), 0u);

	// c's input should be disconnected
	EXPECT_EQ(m_cIn->source, nullptr);
}

// ============================================================================
// Topological sort
// ============================================================================

TEST_F(GraphTest, TopologicalSortLinearChain)
{
	// a → b → c
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	ASSERT_TRUE(m_graph.Connect(m_bOut, m_cIn));

	auto sorted = m_graph.TopologicalSort();
	ASSERT_EQ(sorted.size(), 3u);
	EXPECT_EQ(sorted[0], m_a);
	EXPECT_EQ(sorted[1], m_b);
	EXPECT_EQ(sorted[2], m_c);
}

TEST_F(GraphTest, TopologicalSortDiamond)
{
	// a → b, a → c, b → d, c → d
	auto* d     = m_graph.AddNode("D", "node D");
	auto* bOut  = m_b->AddOutput("b→");
	auto* cOut  = m_c->AddOutput("c→");
	auto* dIn1  = d->AddInput("→d1");
	auto* dIn2  = d->AddInput("→d2");

	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));   // a → b
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_cIn));   // a → c  (fan-out)
	ASSERT_TRUE(m_graph.Connect(bOut, dIn1));       // b → d
	ASSERT_TRUE(m_graph.Connect(cOut, dIn2));       // c → d

	auto sorted = m_graph.TopologicalSort();
	ASSERT_EQ(sorted.size(), 4u);

	// a must come first (zero in-degree)
	EXPECT_EQ(sorted[0], m_a);

	// b and c can come in either order after a, but both before d
	auto posA = std::find(sorted.begin(), sorted.end(), m_a);
	auto posB = std::find(sorted.begin(), sorted.end(), m_b);
	auto posC = std::find(sorted.begin(), sorted.end(), m_c);
	auto posD = std::find(sorted.begin(), sorted.end(), d);

	EXPECT_LT(posA, posB);
	EXPECT_LT(posA, posC);
	EXPECT_LT(posB, posD);
	EXPECT_LT(posC, posD);
	EXPECT_EQ(sorted.back(), d);
}

TEST_F(GraphTest, TopologicalSortDisconnectedNodesAnyOrder)
{
	// Three nodes with no connections — all have in-degree 0.
	// Any order is valid.
	auto sorted = m_graph.TopologicalSort();
	ASSERT_EQ(sorted.size(), 3u);
	// All three must be present
	for (auto* n : {m_a, m_b, m_c})
		EXPECT_NE(std::find(sorted.begin(), sorted.end(), n), sorted.end());
}

TEST_F(GraphTest, TopologicalSortSingleNode)
{
	GraphT g;
	auto* single = g.AddNode("alone");
	auto sorted  = g.TopologicalSort();
	ASSERT_EQ(sorted.size(), 1u);
	EXPECT_EQ(sorted[0], single);
}

TEST_F(GraphTest, TopologicalSortEmptyGraph)
{
	GraphT g;
	auto sorted = g.TopologicalSort();
	EXPECT_TRUE(sorted.empty());
}

// ============================================================================
// Cycle detection
// ============================================================================

TEST_F(GraphTest, HasCycleDirectCycle)
{
	// a → b → a (simple cycle)
	auto* aIn = m_a->AddInput("aIn");
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));   // a → b
	ASSERT_TRUE(m_graph.Connect(m_bOut, aIn));      // b → a (cycle!)

	EXPECT_TRUE(m_graph.HasCycle());
	EXPECT_THROW(m_graph.TopologicalSort(), std::runtime_error);
}

TEST_F(GraphTest, HasCycleSelfLoopRejectedAtConnect)
{
	// Self-loop blocked by Connect(), so no cycle should exist
	auto* aIn = m_a->AddInput("aIn");
	EXPECT_FALSE(m_graph.Connect(m_aOut, aIn));
	EXPECT_FALSE(m_graph.HasCycle());
}

TEST_F(GraphTest, HasCycleNoCycleDiamond)
{
	auto* d     = m_graph.AddNode("D");
	auto* bOut2 = m_b->AddOutput("b→");
	auto* cOut  = m_c->AddOutput("c→");
	auto* dIn1  = d->AddInput("→d1");
	auto* dIn2  = d->AddInput("→d2");

	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));
	ASSERT_TRUE(m_graph.Connect(m_aOut, m_cIn));
	ASSERT_TRUE(m_graph.Connect(bOut2, dIn1));
	ASSERT_TRUE(m_graph.Connect(cOut, dIn2));

	EXPECT_FALSE(m_graph.HasCycle());
}

TEST_F(GraphTest, HasCycleThreeNodeCycle)
{
	// a → b → c → a
	auto* aIn = m_a->AddInput("aIn");
	auto* cOut = m_c->AddOutput("c→");  // was m_bOut

	ASSERT_TRUE(m_graph.Connect(m_aOut, m_bIn));   // a → b
	ASSERT_TRUE(m_graph.Connect(m_bOut, m_cIn));   // b → c
	ASSERT_TRUE(m_graph.Connect(cOut, aIn));        // c → a (cycle!)

	EXPECT_TRUE(m_graph.HasCycle());
}
