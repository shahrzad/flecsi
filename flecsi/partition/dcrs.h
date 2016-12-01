/*~--------------------------------------------------------------------------~*
 * Copyright (c) 2015 Los Alamos National Security, LLC
 * All rights reserved.
 *~--------------------------------------------------------------------------~*/

#ifndef flecsi_dmp_dcrs_h
#define flecsi_dmp_dcrs_h

#include <vector>

///
// \file dcrs.h
// \authors bergen
// \date Initial file creation: Nov 24, 2016
///

namespace flecsi {
namespace dmp {

///
// Convenience macro to avoid having to reimplement this for each member.
///
#define define_dcrs_as(member)                                                 \
  template<                                                                    \
    typename T                                                                 \
  >                                                                            \
  std::vector<T>                                                               \
  member ## _as() const                                                        \
  {                                                                            \
    std::vector<T> asvec(member.begin(), member.end());                        \
    return asvec;                                                              \
  } // member ## _as

struct dcrs_t
{
  define_dcrs_as(offsets)
  define_dcrs_as(indices)
  define_dcrs_as(distribution)

  size_t
  size() const
  {
    return offsets.size()-1;
  } // size

  std::vector<size_t> offsets;
  std::vector<size_t> indices;
  std::vector<size_t> distribution;
}; // struct dcrs_t

std::ostream &
operator <<
(
  std::ostream & stream,
  const dcrs_t & dcrs
)
{
  stream << "offsets: ";
  for(auto i: dcrs.offsets) {
    stream << i << " ";
  } // for
  stream << std::endl;

  stream << "indices: ";
  for(auto i: dcrs.indices) {
    stream << i << " ";
  } // for
  stream << std::endl;

  stream << "distribution: ";
  for(auto i: dcrs.distribution) {
    stream << i << " ";
  } // for

  return stream;
} // operator <<

} // namespace dmp
} // namespace flecsi

#endif // flecsi_dmp_dcrs_h

/*~-------------------------------------------------------------------------~-*
 * Formatting options for vim.
 * vim: set tabstop=2 shiftwidth=2 expandtab :
 *~-------------------------------------------------------------------------~-*/
