#include <cstring>
#include <iostream>

#include <FL/Fl.H>

#include "manager/manager_info.h"
#include "manager/ui/application.h"

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << eosr::manager::build_id() << '\n';
        return 0;
    }
    const bool smoke_test = argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0;
    Fl::scheme("gtk+");
    Fl::lock();
    eosr::manager::ui::manager_window window(smoke_test);
    if (!window.ready()) return 2;
    window.show();
    if (smoke_test) {
        Fl::wait(0.05);
        window.hide();
        return 0;
    }
    return Fl::run();
}
