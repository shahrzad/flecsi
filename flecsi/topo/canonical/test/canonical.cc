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

#define __FLECSI_PRIVATE__
#include "flecsi/topo/canonical/interface.hh"
#include "flecsi/util/unit.hh"
#include <flecsi/data.hh>
#include <flecsi/execution.hh>

#include <tuple>

using namespace flecsi;

struct canon : topo::specialization<topo::canonical, canon> {
  enum index_space { vertices, cells };
  using index_spaces = has<cells, vertices>;
  using connectivities = list<entity<cells, has<vertices>>>;

  static coloring color(std::string const &) {
    return {2, {16, 17}, {{10}}};
  } // color

  static void init_cells_to_vertices(field<util::id, data::ragged>::mutator m,
    util::id f) {
    // This could provide meaningful initial values, but we want to exercise
    // writable topology/ragged accessors.
    for(int i = 0; i < 4; ++i) {
      m[i].resize(i + 1);
    }

    m[3].back() = f;
  }

  static void init_fields(canon::accessor<wo> t, int m) {
    t.mine_(0) = m;
    t.meta_ = {6, 3};
  }

  static void initialize(data::topology_slot<canon> & s, int m, util::id f) {
    auto & cf = s->connect_.get<canon::cells>().get<canon::vertices>();
    execute<init_cells_to_vertices, mpi>(cf(s), f);
    execute<init_fields, mpi>(s, m);
  }
};

canon::slot canonical;
canon::cslot coloring;

const field<double>::definition<canon, canon::cells> pressure;

const int mine = 35;
const util::id favorite = 3;
const double p0 = 3.5;

int
init(canon::accessor<ro> t, field<double>::accessor<wo> c) {
  UNIT {
    util::id last = -1;
    for(const auto v : t.entities<canon::vertices>()) {
      static_assert(
        std::is_same_v<decltype(v), const topo::id<canon::vertices>>);
      last = v;
    }
    EXPECT_EQ((last + 1) / 2, 4u);
    c(0) = p0;
  };
} // init

// Exercise the std::vector-like interface:
int
permute(field<util::id, data::ragged>::mutator m) {
  UNIT {
    const auto &&src = m[3], &&dst = m[0], &&two = m[1];
    // Intermediate sizes can exceed the capacity of the underlying raw field:
    dst.insert(dst.begin(), 10, 3);
    EXPECT_EQ(dst.end()[-1], 0u);
    EXPECT_EQ(dst.end()[-2], 3u);
    src.erase(src.begin(), src.end() - 1); // keep the one real value
    src.insert(src.begin(), dst.begin(), dst.end());
    two.resize(3);
    ASSERT_EQ(two.size(), 3u);
    EXPECT_GT(two.size(), two.capacity());
    ASSERT_EQ(&two[0] + 1, &two[1]);
    EXPECT_NE(&two[1] + 1, &two[2]); // the latter is in the overflow
    two.erase(two.begin() + 1);
    EXPECT_NE(&two[0] + 1, &two[1]); // [1] now refers to the overflow
    two.pop_back();
    EXPECT_EQ(two.size(), 1u);
    two.push_back(0);
    EXPECT_EQ(&two[0] + 1, &two[1]); // TODO: test shrink_to_fit
    dst.clear();
    two.clear();
    dst.push_back(src.back());
    src.clear();
  };
}

int
check(canon::accessor<ro> t, field<double>::accessor<ro> c) {
  UNIT {
    auto & r = t.mine_(0);
    static_assert(std::is_same_v<decltype(r), const int &>);
    EXPECT_EQ(r, mine);
    auto & m = t.meta_.get();
    static_assert(std::is_same_v<decltype(m), const canon::core::Meta &>);
    EXPECT_EQ(m.column_size, 2 * m.column_offset);
    const auto cv =
      t.entities<canon::vertices>(topo::id<canon::cells>(0)).front();
    static_assert(
      std::is_same_v<decltype(cv), const topo::id<canon::vertices>>);
    EXPECT_EQ(cv - decltype(cv)(favorite), 0);
    EXPECT_EQ(c(0), p0);
  };
} // check

// Making the partition wider would require initializing the new elements.
void
shrink(topo::resize::Field::accessor<rw> a) {
  a = data::partition::make_row(color(), data::partition::row_size(a) - 1);
}

int
canonical_driver() {
  UNIT {
    const std::string filename = "input.txt";
    coloring.allocate(filename);
    canonical.allocate(coloring.get(), mine, favorite);

    auto & cf = canonical->connect_.get<canon::cells>().get<canon::vertices>();
    // execute<allocate>(cf(canonical));
    auto pc = pressure(canonical);
    EXPECT_EQ(test<init>(canonical, pc), 0);
    EXPECT_EQ(test<permute>(cf(canonical)), 0);
    EXPECT_EQ(test<check>(canonical, pc), 0);

    auto & c = canonical.get().part_.get<canon::cells>();
    execute<shrink>(c.sizes());
    c.resize();
    EXPECT_EQ(test<check>(canonical, pc), 0);
  };
} // index

flecsi::unit::driver<canonical_driver> driver;