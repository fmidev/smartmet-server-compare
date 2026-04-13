#include "MainWindow.h"

#include <gtkmm/application.h>

int main(int argc, char* argv[])
{
  auto app = Gtk::Application::create(argc, argv,
                                      "fi.fmi.smartmet.server-compare",
                                      Gio::APPLICATION_NON_UNIQUE);
  MainWindow window;
  return app->run(window);
}
