/*
    @@@@@@@@  @@           @@@@@@   @@@@@@@@ @@
   /@@/////  /@@          @@////@@ @@////// /@@
   /@@       /@@  @@@@@  @@    // /@@       /@@
   /@@@@@@@  /@@ @@///@@/@@       /@@@@@@@@@/@@
   /@@////   /@@/@@@@@@@/@@       ////////@@/@@
   /@@       /@@/@@//// //@@    @@       /@@/@@
   /@@       @@@//@@@@@@ //@@@@@@  @@@@@@@@ /@@
   //       ///  //////   //////  ////////  //

   Copyright (c) 2016, Triad National Security, LLC
   All rights reserved.
                                                                              */

#include "flecsi/util/unit.hh"

#define __FLECSI_PRIVATE__
#include <flecsi/execution.hh>

using namespace flecsi;

log::devel_tag task_tag("task");

template<std::size_t M,
  exec::task_processor_type_t T,
  bool F(const exec::task_attributes_bitset_t &)>
constexpr void
test() {
  static_assert(exec::mask_to_processor_type(M) == T);
  static_assert(F(M));
}

template<task_attributes_mask_t P, exec::task_processor_type_t T>
constexpr bool
test() {
  test<P | leaf, T, exec::leaf_task>();
  test<P | inner, T, exec::inner_task>();
  test<P | idempotent, T, exec::idempotent_task>();
  return true;
}

static_assert(test<loc, exec::task_processor_type_t::loc>());
static_assert(test<toc, exec::task_processor_type_t::toc>());

// ---------------
namespace hydro {

template<typename TYPE>
void
simple(TYPE arg) {
  log::devel_guard guard(task_tag);
  flog(info) << "arg(" << arg << ")\n";
} // simple

template<class T>
void
seq(const T & s) {
  log::devel_guard guard(task_tag);
  [&s](auto && log) {
    bool first = true;
    for(auto & x : s) {
      if(first)
        first = false;
      else
        log << ',';
      log << x;
    }
    log << ")\n";
  }(flog_info("s(")); // keep temporary alive throughout
}

void
mpi(int * p) {
  *p = 1;
}

} // namespace hydro

log::devel_tag color_tag("color");

int
test_driver() {
  UNIT {
    {
      auto & c = run::context::instance();
      flog(info) << "task depth: " << c.task_depth() << std::endl;
      ASSERT_EQ(c.task_depth(), 0u);

      auto process = c.process();
      auto processes = c.processes();
      auto tpp = c.threads_per_process();

      {
        log::devel_guard guard(color_tag);
        flog(info) << "(raw)" << std::endl
                   << "\tprocess: " << process << std::endl
                   << "\tprocesses: " << processes << std::endl
                   << "\tthreads_per_process: " << tpp << std::endl;
      }

      ASSERT_EQ(processes, 4u);
      ASSERT_LT(process, processes);
    }

    execute<hydro::simple<float>>(6.2);
    execute<hydro::simple<double>>(5.3);
    execute<hydro::simple<const float &>>(4.4);
    execute<hydro::simple<const double &>>(3.5);
    using V = std::vector<std::string>;
    execute<hydro::seq<V>>(V{"Elementary", " Dear Data"});

    int x = 0;
    execute<hydro::mpi, mpi>(&x);
    ASSERT_EQ(x, 1); // NB: MPI calls are synchronous
  };
} // test_driver

flecsi::unit::driver<test_driver> driver;
