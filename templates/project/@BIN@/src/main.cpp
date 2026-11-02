#include "App.hpp"

// Thin bootstrap (CANON). Everything real is in App, the forms and the core.
int main(int argc, char** argv) {
    return @NS@::App::create()->run(argc, argv);
}
