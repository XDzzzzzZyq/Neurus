#include <gtest/gtest.h>
#include <QString>

#include "core/Log.h"
#include "ui/items/LogModel.h"
#include "ui/items/LogFilterProxy.h"

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

class LogFilterProxyTest : public ::testing::Test
{
protected:
	void SetUp() override { LogBuffer::instance().Clear(); }
	void TearDown() override { LogBuffer::instance().Clear(); }

	LogModel m_model;
	LogFilterProxy m_proxy;

	void PopulateAndRefresh()
	{
		LogBuffer& buf = LogBuffer::instance();
		buf.Append(LogEntry{LogLevel::Info, {}, "f", 1, "geometry loaded", 0});
		buf.Append(LogEntry{LogLevel::Error, {}, "f", 2, "shader failed", 0});
		m_model.Refresh(&buf);
		m_proxy.setSourceModel(&m_model);
	}
};

TEST_F(LogFilterProxyTest, LevelFilter)
{
	PopulateAndRefresh();

	m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::InfoOnly);
	EXPECT_EQ(m_proxy.rowCount(), 1);
	EXPECT_EQ(m_proxy.index(0, 0).data(LogModel::LevelRole).toInt(),
	          static_cast<int>(LogLevel::Info));

	m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::ErrorsOnly);
	EXPECT_EQ(m_proxy.rowCount(), 1);
	EXPECT_EQ(m_proxy.index(0, 0).data(LogModel::LevelRole).toInt(),
	          static_cast<int>(LogLevel::Error));

	m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::All);
	EXPECT_EQ(m_proxy.rowCount(), 2);
}

TEST_F(LogFilterProxyTest, SearchFilterIsCaseInsensitive)
{
	PopulateAndRefresh();

	m_proxy.setSearchText(QStringLiteral("FAILED"));
	EXPECT_EQ(m_proxy.rowCount(), 1);
	EXPECT_EQ(m_proxy.index(0, 0).data(LogModel::MessageRole).toString(),
	          QStringLiteral("shader failed"));

	m_proxy.setSearchText(QStringLiteral("nonexistent"));
	EXPECT_EQ(m_proxy.rowCount(), 0);
}

TEST_F(LogFilterProxyTest, LevelAndSearchCombine)
{
	PopulateAndRefresh();

	m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::ErrorsOnly);
	m_proxy.setSearchText(QStringLiteral("geometry"));
	EXPECT_EQ(m_proxy.rowCount(), 0); // geometry is Info, filter is ErrorsOnly

	m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::All);
	EXPECT_EQ(m_proxy.rowCount(), 1); // geometry only
}

} // namespace neurus
