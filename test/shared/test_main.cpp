// GTest entry point with Qt event loop support.
// Required because UIEvents uses QObject signals/slots.
//
// QApplication (not QGuiApplication) so tests can instantiate QWidgets
// (e.g. ShaderEditorPanel offscreen integration tests). QApplication is a
// superset of QGuiApplication, so non-widget tests are unaffected.

#include <gtest/gtest.h>
#include <QApplication>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
