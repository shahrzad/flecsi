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

/*!
  @file

  This file is really just a conduit to capture the different
  specializations for accessors and topologies.
 */

#include <flecsi/data/legion/topology_traits.hh>

namespace flecsi {
namespace data {

struct legion_policy_t {

  /*--------------------------------------------------------------------------*
    Topology Instance Interface.
   *--------------------------------------------------------------------------*/

  template<typename TOPOLOGY_TYPE>
  static void allocate(reference_base const & topology_reference,
    const typename TOPOLOGY_TYPE::coloring & coloring_reference) {
    legion::topology_traits<TOPOLOGY_TYPE>::allocate(
      topology_reference, coloring_reference);
  } // create

  template<typename TOPOLOGY_TYPE>
  static void deallocate(reference_base const & topology_reference) {
    legion::topology_traits<TOPOLOGY_TYPE>::deallocate(topology_reference);
  } // create

}; // struct legion_policy_t

} // namespace data
} // namespace flecsi
