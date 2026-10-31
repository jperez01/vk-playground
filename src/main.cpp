#include <vk_engine.h>

#include "app/application.h"
#include <cstdio>
#include <exception>

int main(int argc, char* argv[])
{
    try {
        Application app;
        app.run();
        app.cleanup();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Application failed: %s\n", error.what());
        return 1;
    }
}
