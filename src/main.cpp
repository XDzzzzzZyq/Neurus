/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Delegates all lifecycle management to neurus::Application.
 * See Application.h for the initialization and shutdown sequence.
 */

#include "app/Application.h"

int main(int argc, char* argv[])
{
	neurus::Application app(argc, argv);
	return app.Run();
}
