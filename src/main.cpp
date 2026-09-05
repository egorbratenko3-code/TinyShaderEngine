#include "core/Application.h"
#include <cstdio>

int main() {
    Application app;
    if (!app.Init()) {
        fprintf(stderr, "TinyShaderEngine: initialization failed.\n");
        return 1;
    }
    app.Run();
    app.Shutdown();
    return 0;
}
