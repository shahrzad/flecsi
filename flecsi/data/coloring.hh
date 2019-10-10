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
#pragma once

/*! @file */

#if !defined(__FLECSI_PRIVATE__)
#error Do not include this file directly!
#endif

#include <flecsi/data/backend.hh>
#include <flecsi/data/reference.hh>
#include <flecsi/execution.hh>
#include <flecsi/runtime/types.hh>

#include <mpi.h>
#include <optional>

namespace flecsi {
namespace data {

template<class Topo>
struct coloring_slot {
  using color_type = typename Topo::coloring;

  /*!
    @note The MPI task launched by this method is always mapped to the process
    launch domain.
  */

  template<typename... ARGS>
  color_type & allocate(ARGS &&... args) {
    constexpr auto f = [](coloring_slot & s, ARGS &&... aa) {
      s.coloring.emplace(std::forward<ARGS>(aa)...);
    };
    execute<*f, index, mpi>(*this, std::forward<ARGS>(args)...);
    return get();
  } // allocate

  void deallocate() {
    coloring.reset();
  } // deallocate

  color_type & get() {
    return *coloring;
  }

private:
  std::optional<color_type> coloring;
};

} // namespace data
} // namespace flecsi
