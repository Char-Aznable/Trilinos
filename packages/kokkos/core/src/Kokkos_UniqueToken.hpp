/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_UNIQUE_TOKEN_HPP
#define KOKKOS_UNIQUE_TOKEN_HPP

#include <Kokkos_Macros.hpp>

namespace Kokkos {
namespace Experimental {

enum class UniqueTokenScope : int { Instance, Global };

/// \brief class to generate unique ids base on the required amount of
/// concurrency
///
/// This object should behave like a ref-counted object, so that when the last
/// instance is destroy resources are free if needed
template <typename ExecutionSpace,
          UniqueTokenScope = UniqueTokenScope::Instance>
class UniqueToken {
 public:
  using execution_space = ExecutionSpace;
  using size_type       = typename execution_space::size_type;

  /// \brief create object size for concurrency on the given instance
  ///
  /// This object should not be shared between instances
  UniqueToken(execution_space const& = execution_space());

  /// \brief upper bound for acquired values, i.e. 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  size_type size() const;

  /// \brief acquire value such that 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  size_type acquire() const;

  /// \brief release a value acquired by generate
  KOKKOS_INLINE_FUNCTION
  void release(size_type) const;
};

}  // namespace Experimental
}  // namespace Kokkos

#endif  // KOKKOS_UNIQUE_TOKEN_HPP
