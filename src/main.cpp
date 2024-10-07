#include <vk_engine.h>

#include "app/application.h"

int main(int argc, char* argv[])
{
    Application app;
    app.run();
    app.cleanup();

    return 0;
}
