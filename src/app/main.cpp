#include "Application.h"

int main(int argc, char *argv[])
{
    // High-DPI is on by default in Qt 6; keep native title bar (spec).
    Application app(argc, argv);
    return app.run();
}
