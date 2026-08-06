#include <gtest/gtest.h>
#include <QString>

#include "core/Log.h"
#include "ui/items/LogModel.h"

namespace neurus
{

class LogModelTest : public ::testing::Test
{
protected:
	void SetUp() override { LogBuffer::instance().Clear(); }
	void TearDown() override { LogBuffer::instance().Clear(); }
};

TEST_F(LogModelTest, RowCountMatchesBuffer)
{
	LogBuffer::instance().Append(LogEntry{LogLevel::Info, {}, "f", 1, "a", 0});
	LogBuffer::instance().Append(LogEntry{LogLevel::Error, {}, "f", 2, "b", 0});

	LogModel model;
	model.Refresh(&LogBuffer::instance());
	EXPECT_EQ(model.rowCount(), 2);
}

TEST_F(LogModelTest, DataRoles)
{
	LogBuffer::instance().Append(
		LogEntry{LogLevel::Info, {}, "myFunc", 42, "hello", 0});
	LogModel model;
	model.Refresh(&LogBuffer::instance());

	const QModelIndex idx = model.index(0, 0);
	EXPECT_EQ(idx.data(LogModel::MessageRole).toString(), QStringLiteral("hello"));
	EXPECT_EQ(idx.data(LogModel::LevelRole).toInt(), static_cast<int>(LogLevel::Info));
	EXPECT_EQ(idx.data(LogModel::SourceRole).toString(), QStringLiteral("myFunc:42"));
	EXPECT_TRUE(idx.data(Qt::DisplayRole).toString().contains(QStringLiteral("hello")));
	EXPECT_GT(idx.data(LogModel::TimestampRole).toString().length(), 0);
	EXPECT_EQ(idx.data(LogModel::SeqRole).toULongLong(), 1u);
}

TEST_F(LogModelTest, RefreshAppendsNewRows)
{
	LogBuffer& buf = LogBuffer::instance();
	buf.Append(LogEntry{LogLevel::Info, {}, "f", 1, "a", 0});
	LogModel model;
	model.Refresh(&buf);
	EXPECT_EQ(model.rowCount(), 1);

	buf.Append(LogEntry{LogLevel::Info, {}, "f", 2, "b", 0});
	model.Refresh(&buf);
	EXPECT_EQ(model.rowCount(), 2);
	EXPECT_EQ(model.index(1, 0).data(LogModel::MessageRole).toString(),
	          QStringLiteral("b"));
}

TEST_F(LogModelTest, ClearResetsModel)
{
	LogBuffer& buf = LogBuffer::instance();
	buf.Append(LogEntry{LogLevel::Info, {}, "f", 1, "a", 0});
	LogModel model;
	model.Refresh(&buf);
	EXPECT_EQ(model.rowCount(), 1);

	buf.Clear();
	model.Refresh(&buf);
	EXPECT_EQ(model.rowCount(), 0);
}

} // namespace neurus
