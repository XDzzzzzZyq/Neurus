/**
 * @file test_uid.cpp
 * @brief Tests for UID and ObjectID base classes.
 *
 * Covers:
 * - Unique ID generation across 1000 instances
 * - ObjectID default construction (name, type, visibility)
 * - Counter increments correctly
 * - GetObjectID() returns assigned ID
 * - GetTotalAllocated() returns global count
 */

#include <gtest/gtest.h>
#include <scene/UID.h>

// --- UID Tests -----------------------------------------------------------

/**
 * @test UIDs are unique across multiple instances.
 */
TEST(UIDTest, UniqueIDs)
{
	constexpr int kCount = 1000;
	int ids[kCount];

	for (int i = 0; i < kCount; ++i)
	{
		neurus::UID uid;
		ids[i] = uid.GetObjectID();
	}

	// Verify all IDs are unique
	for (int i = 0; i < kCount; ++i)
	{
		for (int j = i + 1; j < kCount; ++j)
		{
			EXPECT_NE(ids[i], ids[j]) << "IDs [" << i << "] and [" << j << "] collide";
		}
	}
}

/**
 * @test GetTotalAllocated returns non-zero after creating UIDs.
 */
TEST(UIDTest, TotalAllocatedIncrements)
{
	int before = neurus::UID::GetTotalAllocated();

	{
		neurus::UID a;
		neurus::UID b;
		neurus::UID c;
		(void)a;
		(void)b;
		(void)c;
	}

	int afterScope = neurus::UID::GetTotalAllocated();
	EXPECT_GE(afterScope, before + 3);
}

/**
 * @test UID GetObjectID returns a valid (non-negative) ID.
 */
TEST(UIDTest, GetObjectIDValid)
{
	neurus::UID uid;
	EXPECT_GE(uid.GetObjectID(), 0);
}

/**
 * @test Two consecutive UIDs have increasing IDs.
 */
TEST(UIDTest, IDsIncrease)
{
	neurus::UID first;
	neurus::UID second;
	EXPECT_LT(first.GetObjectID(), second.GetObjectID());
}

// --- ObjectID Tests ------------------------------------------------------

/**
 * @test ObjectID default-constructs with expected defaults.
 */
TEST(ObjectIDTest, DefaultConstruction)
{
	neurus::ObjectID obj;

	// Default name should be empty
	EXPECT_TRUE(obj.o_name.empty());

	// Default type should be NONE_GO
	EXPECT_EQ(obj.o_type, neurus::ObjectID::GOType::NONE_GO);

	// Default visibility flags
	EXPECT_TRUE(obj.is_viewport);
	EXPECT_TRUE(obj.is_rendered);
}

/**
 * @test ObjectID inherits valid UID.
 */
TEST(ObjectIDTest, InheritsValidUID)
{
	neurus::ObjectID obj;
	EXPECT_GE(obj.GetObjectID(), 0);
}

/**
 * @test SetVisible modifies both flags.
 */
TEST(ObjectIDTest, SetVisible)
{
	neurus::ObjectID obj;
	obj.SetVisible(false, false);
	EXPECT_FALSE(obj.is_viewport);
	EXPECT_FALSE(obj.is_rendered);

	obj.SetVisible(true, false);
	EXPECT_TRUE(obj.is_viewport);
	EXPECT_FALSE(obj.is_rendered);
}

/**
 * @test Virtual accessors return nullptr by default.
 */
TEST(ObjectIDTest, VirtualAccessorsReturnNull)
{
	neurus::ObjectID obj;
	EXPECT_EQ(obj.GetShader(), nullptr);
	EXPECT_EQ(obj.GetTransform(), nullptr);
	EXPECT_EQ(obj.GetMaterial(), nullptr);
}

/**
 * @test ObjectID can have name and type assigned.
 */
TEST(ObjectIDTest, AssignedNameAndType)
{
	neurus::ObjectID obj;
	obj.o_name = "TestCamera";
	obj.o_type = neurus::ObjectID::GOType::GO_CAM;

	EXPECT_EQ(obj.o_name, "TestCamera");
	EXPECT_EQ(obj.o_type, neurus::ObjectID::GOType::GO_CAM);
}

/**
 * @test UID counter increments across UID and ObjectID instances.
 */
TEST(ObjectIDTest, CounterIncrementsMixed)
{
	int before = neurus::UID::GetTotalAllocated();

	{
		neurus::UID a;
		neurus::ObjectID b;
		neurus::UID c;
		(void)a;
		(void)b;
		(void)c;
	}

	int after = neurus::UID::GetTotalAllocated();
	EXPECT_GE(after, before + 3);
}
