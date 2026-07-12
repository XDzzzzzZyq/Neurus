// GTest entry point with Qt event loop support.
// Required because UIEvents uses QObject signals/slots.

#include <gtest/gtest.h>
#include <QGuiApplication>

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
