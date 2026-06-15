// GTest entry point with Qt event loop support.
// Required because EventBus and EditorContext use QObject signals/slots.

#include <gtest/gtest.h>
#include <QGuiApplication>

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
