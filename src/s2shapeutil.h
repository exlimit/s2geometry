// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)
//
// Useful functions and classes related to S2ShapeIndex.

#ifndef S2_GEOMETRY_S2SHAPEUTIL_H_
#define S2_GEOMETRY_S2SHAPEUTIL_H_

#include <utility>  // pair<>
#include <vector>

#include "fpcontractoff.h"
#include "s2.h"
#include "s2shapeindex.h"

class S2Loop;
class S2Error;

namespace s2shapeutil {

// S2EdgeVectorShape is an S2Shape representing a set of unrelated edges.
// It is mainly used for testing, but it can also be useful if you have, say,
// a collection of polylines and don't care about memory efficiency (since
// this class would store most of the vertices twice).
//
// If the vertices are already stored somewhere else, you would be better off
// writing your own subclass of S2Shape that points to the existing vertex
// data rather than copying it.
//
// Note that when an S2EdgeVectorShape is inserted into an S2ShapeIndex, the
// index takes ownership.  The object will be deleted automatically when the
// index no longer needs it (see Release().)
class S2EdgeVectorShape : public S2Shape {
 public:
  S2EdgeVectorShape() {}
  // Convenience constructor for creating a vector of length 1.
  S2EdgeVectorShape(S2Point const& a, S2Point const& b) {
    edges_.push_back(std::make_pair(a, b));
  }
  // Add an edge to the vector.
  void Add(S2Point const& a, S2Point const& b) {
    edges_.push_back(std::make_pair(a, b));
  }
  int num_edges() const { return edges_.size(); }
  void GetEdge(int i, S2Point const** a, S2Point const** b) const {
    *a = &edges_[i].first;
    *b = &edges_[i].second;
  }
  bool has_interior() const { return false; }
  bool contains_origin() const { return false; }
  void Release() const { delete this; }
 private:
  std::vector<std::pair<S2Point, S2Point> > edges_;
};


// Given an S2ShapeIndex containing a single loop, return true if the loop has
// a self-intersection (including duplicate vertices) and set "error" to a
// human-readable error message.  Otherwise return false and leave "error"
// unchanged.
bool FindSelfIntersection(S2ShapeIndex const& index, S2Loop const& loop,
                          S2Error* error);

// Given an S2ShapeIndex containing a set of loops, return true if any loop
// has a self-intersection (including duplicate vertices) or crosses any other
// loop (including vertex crossings and duplicate edges) and set "error" to a
// human-readable error message.  Otherwise return false and leave "error"
// unchanged.
bool FindAnyCrossing(S2ShapeIndex const& index,
                     std::vector<S2Loop*> const& loops,
                     S2Error* error);

}  // namespace s2shapeutil

#endif  // S2_GEOMETRY_S2SHAPEUTIL_H_