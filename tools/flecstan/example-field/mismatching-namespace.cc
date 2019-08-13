/*
# Mismatching namespace
flecsi_register_field(mesh_t, example, pressure, double, dense, 1, cells);
auto f = flecsi_get_handle(m, exmaple, pressure, double, dense, 0);
*/

#include <flecsi-tutorial/specialization/mesh/mesh.h>
#include <flecsi/data/data.h>
#include <flecsi/execution/execution.h>
#include <iostream>

using namespace flecsi;
using namespace flecsi::tutorial;

flecsi_register_field(mesh_t, example, pressure, double, dense, 1, cells);

namespace flecsi {
namespace execution {

void
driver(int argc, char ** argv) {
  auto m = flecsi_get_client_handle(mesh_t, clients, mesh);

  // The error here is the typo: "exmaple" instead of "example"
  auto f = flecsi_get_handle(m, exmaple, pressure, double, dense, 0);
}

} // namespace execution
} // namespace flecsi
