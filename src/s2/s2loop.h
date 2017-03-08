// Copyright 2005 Google Inc. All Rights Reserved.
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

#ifndef S2_S2LOOP_H_
#define S2_S2LOOP_H_

#include <bitset>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

#include <glog/logging.h>

#include "s2/base/macros.h"
#include "s2/fpcontractoff.h"
#include "s2/s1angle.h"
#include "s2/s2.h"
#include "s2/s2edgeutil.h"
#include "s2/s2latlngrect.h"
#include "s2/s2region.h"
#include "s2/s2shapeindex.h"
#include "s2/third_party/absl/base/integral_types.h"
#include "s2/util/math/vector.h"

class Decoder;
class Encoder;
class LoopCrosser;
class LoopRelation;
class MergingIterator;
class S2Cap;
class S2Cell;
class S2CrossingEdgeQuery;
class S2Error;
class S2Loop;
class S2XYZFaceSiTi;
namespace s2builderutil {
class S2PolygonLayer;
}  // namespace s2builderutil

// An S2Loop represents a simple spherical polygon.  It consists of a single
// chain of vertices where the first vertex is implicitly connected to the
// last. All loops are defined to have a CCW orientation, i.e. the interior of
// the loop is on the left side of the edges.  This implies that a clockwise
// loop enclosing a small area is interpreted to be a CCW loop enclosing a
// very large area.
//
// Loops are not allowed to have any duplicate vertices (whether adjacent or
// not), and non-adjacent edges are not allowed to intersect.  Loops must have
// at least 3 vertices (except for the "empty" and "full" loops discussed
// below).  Although these restrictions are not enforced in optimized code,
// you may get unexpected results if they are violated.
//
// There are two special loops: the "empty" loop contains no points, while the
// "full" loop contains all points.  These loops do not have any edges, but to
// preserve the invariant that every loop can be represented as a vertex
// chain, they are defined as having exactly one vertex each (see kEmpty and
// kFull).
//
// Point containment of loops is defined such that if the sphere is subdivided
// into faces (loops), every point is contained by exactly one face.  This
// implies that loops do not necessarily contain their vertices.
//
// Note: The reason that duplicate vertices and intersecting edges are not
// allowed is that they make it harder to define and implement loop
// relationships, e.g. whether one loop contains another.  If your data does
// not satisfy these restrictions, you can use S2Builder to normalize it.
class S2Loop : public S2Region {
 public:
  // Default constructor.  The loop must be initialized by calling Init() or
  // Decode() before it is used.
  S2Loop();

  // Convenience constructor that calls Init() with the given vertices.
  explicit S2Loop(std::vector<S2Point> const& vertices);

  // Convenience constructor to disable the automatic validity checking
  // controlled by the --s2debug flag.  Example:
  //
  //   S2Loop* loop = new S2Loop(vertices, S2Debug::DISABLE);
  //
  // This is equivalent to:
  //
  //   S2Loop* loop = new S2Loop;
  //   loop->set_s2debug_override(S2Debug::DISABLE);
  //   loop->Init(vertices);
  //
  // The main reason to use this constructor is if you intend to call
  // IsValid() explicitly.  See set_s2debug_override() for details.
  S2Loop(std::vector<S2Point> const& vertices, S2Debug override);

  // Initialize a loop with given vertices.  The last vertex is implicitly
  // connected to the first.  All points should be unit length.  Loops must
  // have at least 3 vertices (except for the "empty" and "full" loops, see
  // kEmpty and kFull).  This method may be called multiple times.
  void Init(std::vector<S2Point> const& vertices);

  // A special vertex chain of length 1 that creates an empty loop (i.e., a
  // loop with no edges that contains no points).  Example usage:
  //
  //    S2Loop empty(S2Loop::kEmpty());
  //
  // The loop may be safely encoded lossily (e.g. by snapping it to an S2Cell
  // center) as long as its position does not move by 90 degrees or more.
  static std::vector<S2Point> kEmpty();

  // A special vertex chain of length 1 that creates a full loop (i.e., a loop
  // with no edges that contains all points).  See kEmpty() for details.
  static std::vector<S2Point> kFull();

  // Construct a loop corresponding to the given cell.
  //
  // Note that the loop and cell *do not* contain exactly the same set of
  // points, because S2Loop and S2Cell have slightly different definitions of
  // point containment.  For example, an S2Cell vertex is contained by all
  // four neighboring S2Cells, but it is contained by exactly one of four
  // S2Loops constructed from those cells.  As another example, the S2Cell
  // coverings of "cell" and "S2Loop(cell)" will be different, because the
  // loop contains points on its boundary that actually belong to other cells
  // (i.e., the covering will include a layer of neighboring cells).
  explicit S2Loop(S2Cell const& cell);

  ~S2Loop() override;

  // Allows overriding the automatic validity checks controlled by the
  // --s2debug flag.  If this flag is true, then loops are automatically
  // checked for validity as they are initialized.  The main reason to disable
  // this flag is if you intend to call IsValid() explicitly, like this:
  //
  //   S2Loop loop;
  //   loop.set_s2debug_override(S2Debug::DISABLE);
  //   loop.Init(...);
  //   if (!loop.IsValid()) { ... }
  //
  // Without the call to set_s2debug_override(), invalid data would cause a
  // fatal error in Init() whenever the --s2debug flag is enabled.
  //
  // This setting is preserved across calls to Init() and Decode().
  void set_s2debug_override(S2Debug override);
  S2Debug s2debug_override() const;

  // Returns true if this is a valid loop.  Note that validity is checked
  // automatically during initialization when --s2debug is enabled (true by
  // default in debug binaries).
  bool IsValid() const;

  // Returns true if this is *not* a valid loop and sets "error"
  // appropriately.  Otherwise returns false and leaves "error" unchanged.
  //
  // REQUIRES: error != nullptr
  bool FindValidationError(S2Error* error) const;

  int num_vertices() const { return num_vertices_; }

  // For convenience, we make two entire copies of the vertex list available:
  // vertex(n..2*n-1) is mapped to vertex(0..n-1), where n == num_vertices().
  //
  // REQUIRES: 0 <= i < 2 * num_vertices()
  S2Point const& vertex(int i) const {
    DCHECK_GE(i, 0);
    DCHECK_LT(i, 2 * num_vertices());
    int j = i - num_vertices();
    return vertices_[j < 0 ? i : j];
  }

  // Like vertex(), but this method returns vertices in reverse order if the
  // loop represents a polygon hole.  For example, arguments 0, 1, 2 are
  // mapped to vertices n-1, n-2, n-3, where n == num_vertices().  This
  // ensures that the interior of the polygon is always to the left of the
  // vertex chain.
  //
  // REQUIRES: 0 <= i < 2 * num_vertices()
  S2Point const& oriented_vertex(int i) const {
    if (is_hole()) i = (2 * num_vertices() - 1) - i;
    return vertex(i);
  }

  // Return true if this is the special "empty" loop that contains no points.
  bool is_empty() const;

  // Return true if this is the special "full" loop that contains all points.
  bool is_full() const;

  // Return true if this loop is either "empty" or "full".
  bool is_empty_or_full() const;

  // The depth of a loop is defined as its nesting level within its containing
  // polygon.  "Outer shell" loops have depth 0, holes within those loops have
  // depth 1, shells within those holes have depth 2, etc.  This field is only
  // used by the S2Polygon implementation.
  int depth() const { return depth_; }
  void set_depth(int depth) { depth_ = depth; }

  // Return true if this loop represents a hole in its containing polygon.
  bool is_hole() const { return (depth_ & 1) != 0; }

  // The sign of a loop is -1 if the loop represents a hole in its containing
  // polygon, and +1 otherwise.
  int sign() const { return is_hole() ? -1 : 1; }

  // Return true if the loop area is at most 2*Pi.  Degenerate loops are
  // handled consistently with s2pred::Sign(), i.e., if a loop can be
  // expressed as the union of degenerate or nearly-degenerate CCW triangles,
  // then it will always be considered normalized.
  bool IsNormalized() const;

  // Invert the loop if necessary so that the area enclosed by the loop is at
  // most 2*Pi.
  void Normalize();

  // Reverse the order of the loop vertices, effectively complementing the
  // region represented by the loop.  For example, the loop ABCD (with edges
  // AB, BC, CD, DA) becomes the loop DCBA (with edges DC, CB, BA, AD).
  // Notice that the last edge is the same in both cases except that its
  // direction has been reversed.
  void Invert();

  // Return the area of the loop interior, i.e. the region on the left side of
  // the loop.  The return value is between 0 and 4*Pi.  (Note that the return
  // value is not affected by whether this loop is a "hole" or a "shell".)
  double GetArea() const;

  // Return the true centroid of the loop multiplied by the area of the loop
  // (see s2.h for details on centroids).  The result is not unit length, so
  // you may want to normalize it.  Also note that in general, the centroid
  // may not be contained by the loop.
  //
  // We prescale by the loop area for two reasons: (1) it is cheaper to
  // compute this way, and (2) it makes it easier to compute the centroid of
  // more complicated shapes (by splitting them into disjoint regions and
  // adding their centroids).
  //
  // Note that the return value is not affected by whether this loop is a
  // "hole" or a "shell".
  S2Point GetCentroid() const;

  // Return the sum of the turning angles at each vertex.  The return value is
  // positive if the loop is counter-clockwise, negative if the loop is
  // clockwise, and zero if the loop is a great circle.  Degenerate and
  // nearly-degenerate loops are handled consistently with s2pred::Sign().
  // So for example, if a loop has zero area (i.e., it is a very small CCW
  // loop) then the turning angle will always be negative.
  //
  // This quantity is also called the "geodesic curvature" of the loop.
  double GetTurningAngle() const;

  // Return the maximum error in GetTurningAngle().  The return value is not
  // constant; it depends on the loop.
  double GetTurningAngleMaxError() const;

  // Return the distance from the given point to the loop interior.  If the
  // loop is empty, return S1Angle::Infinity().  "x" should be unit length.
  S1Angle GetDistance(S2Point const& x) const;

  // Return the distance from the given point to the loop boundary.  If the
  // loop is empty or full, return S1Angle::Infinity() (since the loop has no
  // boundary).  "x" should be unit length.
  S1Angle GetDistanceToBoundary(S2Point const& x) const;

  // If the given point is contained by the loop, return it.  Otherwise return
  // the closest point on the loop boundary.  If the loop is empty, return the
  // input argument.  Note that the result may or may not be contained by the
  // loop.  "x" should be unit length.
  S2Point Project(S2Point const& x) const;

  // Return the closest point on the loop boundary to the given point.  If the
  // loop is empty or full, return the input argument (since the loop has no
  // boundary).  "x" should be unit length.
  S2Point ProjectToBoundary(S2Point const& x) const;

  // Return true if the region contained by this loop is a superset of the
  // region contained by the given other loop.
  bool Contains(S2Loop const* b) const;

  // Return true if the region contained by this loop intersects the region
  // contained by the given other loop.
  bool Intersects(S2Loop const* b) const;

  // Return true if two loops have the same vertices in the same linear order
  // (i.e., cyclic rotations are not allowed).
  bool Equals(S2Loop const* b) const;

  // Return true if two loops have the same boundary.  This is true if and
  // only if the loops have the same vertices in the same cyclic order (i.e.,
  // the vertices may be cyclically rotated).  The empty and full loops are
  // considered to have different boundaries.
  bool BoundaryEquals(S2Loop const* b) const;

  // Return true if two loops have the same boundary except for vertex
  // perturbations.  More precisely, the vertices in the two loops must be in
  // the same cyclic order, and corresponding vertex pairs must be separated
  // by no more than "max_error".
  bool BoundaryApproxEquals(S2Loop const& b,
                            S1Angle max_error = S1Angle::Radians(1e-15)) const;

  // Return true if the two loop boundaries are within "max_error" of each
  // other along their entire lengths.  The two loops may have different
  // numbers of vertices.  More precisely, this method returns true if the two
  // loops have parameterizations a:[0,1] -> S^2, b:[0,1] -> S^2 such that
  // distance(a(t), b(t)) <= max_error for all t.  You can think of this as
  // testing whether it is possible to drive two cars all the way around the
  // two loops such that no car ever goes backward and the cars are always
  // within "max_error" of each other.
  bool BoundaryNear(S2Loop const& b,
                    S1Angle max_error = S1Angle::Radians(1e-15)) const;

  // This method computes the oriented surface integral of some quantity f(x)
  // over the loop interior, given a function f_tri(A,B,C) that returns the
  // corresponding integral over the spherical triangle ABC.  Here "oriented
  // surface integral" means:
  //
  // (1) f_tri(A,B,C) must be the integral of f if ABC is counterclockwise,
  //     and the integral of -f if ABC is clockwise.
  //
  // (2) The result of this function is *either* the integral of f over the
  //     loop interior, or the integral of (-f) over the loop exterior.
  //
  // Note that there are at least two common situations where it easy to work
  // around property (2) above:
  //
  //  - If the integral of f over the entire sphere is zero, then it doesn't
  //    matter which case is returned because they are always equal.
  //
  //  - If f is non-negative, then it is easy to detect when the integral over
  //    the loop exterior has been returned, and the integral over the loop
  //    interior can be obtained by adding the integral of f over the entire
  //    unit sphere (a constant) to the result.
  //
  // Also requires that the default constructor for T must initialize the
  // value to zero.  (This is true for built-in types such as "double".)
  template <class T>
  T GetSurfaceIntegral(T f_tri(S2Point const&, S2Point const&, S2Point const&))
      const;

  // Constructs a regular polygon with the given number of vertices, all
  // located on a circle of the specified radius around "center".  The radius
  // is the actual distance from "center" to each vertex.
  static std::unique_ptr<S2Loop> MakeRegularLoop(S2Point const& center,
                                                 S1Angle radius,
                                                 int num_vertices);

  // Like the function above, but this version constructs a loop centered
  // around the z-axis of the given coordinate frame, with the first vertex in
  // the direction of the positive x-axis.  (This allows the loop to be
  // rotated for testing purposes.)
  static std::unique_ptr<S2Loop> MakeRegularLoop(Matrix3x3_d const& frame,
                                                 S1Angle radius,
                                                 int num_vertices);

  // Return the total number of bytes used by the loop.
  size_t BytesUsed() const;

  ////////////////////////////////////////////////////////////////////////
  // S2Region interface (see s2region.h for details):

  S2Loop* Clone() const override;

  // GetRectBound() returns essentially tight results, while GetCapBound()
  // might have a lot of extra padding.  Both bounds are conservative in that
  // if the loop contains a point P, then the bound contains P also.
  S2Cap GetCapBound() const override;
  S2LatLngRect GetRectBound() const override { return bound_; }

  bool Contains(S2Cell const& cell) const override;
  bool MayIntersect(S2Cell const& cell) const override;
  bool VirtualContainsPoint(S2Point const& p) const override {
    return Contains(p);  // The same as Contains() below, just virtual.
  }

  // The point 'p' does not need to be normalized.
  bool Contains(S2Point const& p) const;

  // Generally clients should not use S2Loop::Encode().  Instead they should
  // encode an S2Polygon, which unlike this method supports (lossless)
  // compression.
  //
  // REQUIRES: the loop is initialized and valid.
  void Encode(Encoder* const encoder) const override;

  // Decode a loop encoded with Encode() or EncodeCompressed().  These methods
  // may be called with loops that have already been initialized.
  bool Decode(Decoder* const decoder) override;
  bool DecodeWithinScope(Decoder* const decoder) override;

  ////////////////////////////////////////////////////////////////////////
  // Methods intended primarily for use by the S2Polygon implementation:

  // Given two loops of a polygon, return true if A contains B.  This version
  // of Contains() is cheap because it does not test for edge intersections.
  // The loops must meet all the S2Polygon requirements; for example this
  // implies that their boundaries may not cross or have any shared edges
  // (although they may have shared vertices).
  bool ContainsNested(S2Loop const* b) const;

  // Return +1 if A contains the boundary of B, -1 if A excludes the boundary
  // of B, and 0 if the boundaries of A and B cross.  Shared edges are handled
  // as follows: If XY is a shared edge, define Reversed(XY) to be true if XY
  // appears in opposite directions in A and B.  Then A contains XY if and
  // only if Reversed(XY) == B->is_hole().  (Intuitively, this checks whether
  // A contains a vanishingly small region extending from the boundary of B
  // toward the interior of the polygon to which loop B belongs.)
  //
  // This method is used for testing containment and intersection of
  // multi-loop polygons.  Note that this method is not symmetric, since the
  // result depends on the direction of loop A but not on the direction of
  // loop B (in the absence of shared edges).
  //
  // REQUIRES: neither loop is empty.
  // REQUIRES: if b->is_full(), then !b->is_hole().
  int CompareBoundary(S2Loop const* b) const;

  // Given two loops whose boundaries do not cross (see CompareBoundary),
  // return true if A contains the boundary of B.  If "reverse_b" is true, the
  // boundary of B is reversed first (which only affects the result when there
  // are shared edges).  This method is cheaper than CompareBoundary() because
  // it does not test for edge intersections.
  //
  // REQUIRES: neither loop is empty.
  // REQUIRES: if b->is_full(), then reverse_b == false.
  bool ContainsNonCrossingBoundary(S2Loop const* b, bool reverse_b) const;

  // Wrapper class for indexing a loop (see S2ShapeIndex).  Once this object
  // is inserted into an S2ShapeIndex it is owned by that index, and will be
  // automatically deleted when no longer needed by the index.  Note that this
  // class does not take ownership of the loop; if you want this behavior, see
  // s2shapeutil::S2LoopOwningShape.  You can also subtype this class to store
  // additional data (see S2Shape for details).
#ifndef SWIG
  class Shape : public S2Shape {
   public:
    Shape() : loop_(nullptr) {}  // Must call Init().

    // Initialize the shape.  Does not take ownership of "loop".
    explicit Shape(S2Loop const* loop) { Init(loop); }
    void Init(S2Loop const* loop) { loop_ = loop; }

    S2Loop const* loop() const { return loop_; }

    // S2Shape interface:
    int num_edges() const override {
      return loop_->is_empty_or_full() ? 0 : loop_->num_vertices();
    }
    void GetEdge(int e, S2Point const** a, S2Point const** b) const override {
      *a = &loop_->vertex(e);
      *b = &loop_->vertex(e + 1);
    }
    int dimension() const override { return 2; }
    bool contains_origin() const override { return loop_->contains_origin(); }
    int num_chains() const override;
    int chain_start(int i) const override;

   private:
    S2Loop const* loop_;
  };
#endif  // SWIG

 private:
  // All of the following need access to contains_origin().  Possibly this
  // method should be public.
  friend class Shape;
  friend class S2Polygon;
  friend class S2Stats;
  friend class S2LoopTestBase;
  friend class LoopCrosser;
  friend class s2builderutil::S2PolygonLayer;

  // Internal copy constructor used only by Clone() that makes a deep copy of
  // its argument.
  S2Loop(S2Loop const& src);

  // Return true if this loop contains S2::Origin().
  bool contains_origin() const { return origin_inside_; }

  // The single vertex in the "empty loop" vertex chain.
  static S2Point kEmptyVertex();

  // The single vertex in the "full loop" vertex chain.
  static S2Point kFullVertex();

  void InitOriginAndBound();
  void InitBound();
  void InitIndex();

  // A version of Contains(S2Point) that does not use the S2ShapeIndex.
  // Used by the S2Polygon implementation.
  bool BruteForceContains(S2Point const& p) const;

  // Like FindValidationError(), but skips any checks that would require
  // building the S2ShapeIndex (i.e., self-intersection tests).  This is used
  // by the S2Polygon implementation, which uses its own index to check for
  // loop self-intersections.
  bool FindValidationErrorNoIndex(S2Error* error) const;

  // Internal implementation of the Decode and DecodeWithinScope methods above.
  // If within_scope is true, memory is allocated for vertices_ and data
  // is copied from the decoder using std::copy. If it is false, vertices_
  // will point to the memory area inside the decoder, and the field
  // owns_vertices_ is set to false.
  bool DecodeInternal(Decoder* const decoder, bool within_scope);

  // Converts the loop vertices to the S2XYZFaceSiTi format and store the result
  // in the given array, which must be large enough to store all the vertices.
  void GetXYZFaceSiTiVertices(S2XYZFaceSiTi* vertices) const;

  // Encode the loop's vertices using S2EncodePointsCompressed.  Uses
  // approximately 8 bytes for the first vertex, going down to less than 4 bytes
  // per vertex on Google's geographic repository, plus 24 bytes per vertex that
  // does not correspond to the center of a cell at level 'snap_level'. The loop
  // vertices must first be converted to the S2XYZFaceSiTi format with
  // GetXYZFaceSiTiVertices.
  //
  // REQUIRES: the loop is initialized and valid.
  void EncodeCompressed(Encoder* encoder, S2XYZFaceSiTi const* vertices,
                        int snap_level) const;

  // Decode a loop encoded with EncodeCompressed. The parameters must be the
  // same as the one used when EncodeCompressed was called.
  bool DecodeCompressed(Decoder* decoder, int snap_level);

  // Returns a bitset of properties used by EncodeCompressed
  // to efficiently encode boolean values.  Properties are
  // origin_inside and whether the bound was encoded.
  std::bitset<2> GetCompressedEncodingProperties() const;

  // Given an iterator that is already positioned at the S2ShapeIndexCell
  // containing "p", returns Contains(p).
  bool Contains(S2ShapeIndex::Iterator const& it, S2Point const& p) const;

  // Return true if the loop boundary intersects "target".  It may also
  // return true when the loop boundary does not intersect "target" but
  // some edge comes within the worst-case error tolerance.
  //
  // REQUIRES: it.id().contains(target.id())
  // [This condition is true whenever it.Locate(target) returns INDEXED.]
  bool BoundaryApproxIntersects(S2ShapeIndex::Iterator const& it,
                                S2Cell const& target) const;

  // Return an index "first" and a direction "dir" (either +1 or -1) such that
  // the vertex sequence (first, first+dir, ..., first+(n-1)*dir) does not
  // change when the loop vertex order is rotated or inverted.  This allows
  // the loop vertices to be traversed in a canonical order.  The return
  // values are chosen such that (first, ..., first+n*dir) are in the range
  // [0, 2*n-1] as expected by the vertex() method.
  int GetCanonicalFirstVertex(int* dir) const;

  // Return the index of a vertex at point "p", or -1 if not found.
  // The return value is in the range 1..num_vertices_ if found.
  int FindVertex(S2Point const& p) const;

  // This method checks all edges of loop A for intersection against all edges
  // of loop B.  If there is any shared vertex, the wedges centered at this
  // vertex are sent to "relation".
  //
  // If the two loop boundaries cross, this method is guaranteed to return
  // true.  It also returns true in certain cases if the loop relationship is
  // equivalent to crossing.  For example, if the relation is Contains() and a
  // point P is found such that B contains P but A does not contain P, this
  // method will return true to indicate that the result is the same as though
  // a pair of crossing edges were found (since Contains() returns false in
  // both cases).
  //
  // See Contains(), Intersects() and CompareBoundary() for the three uses of
  // this function.
  static bool HasCrossingRelation(S2Loop const& a, S2Loop const& b,
                                  LoopRelation* relation);

  // When the loop is modified (Invert(), or Init() called again) then the
  // indexing structures need to be deleted as they become invalid.
  void ResetMutableFields();

  // The nesting depth, if this field belongs to an S2Polygon.  We define it
  // here to optimize field packing.
  int depth_;

  // We store the vertices in an array rather than a vector because we don't
  // need any STL methods, and computing the number of vertices using size()
  // would be relatively expensive (due to division by sizeof(S2Point) == 24).
  // When DecodeWithinScope is used to initialize the loop, we do not
  // take ownership of the memory for vertices_, and the owns_vertices_ field
  // is used to prevent deallocation and overwriting.
  int num_vertices_;
  S2Point* vertices_;
  bool owns_vertices_;

  S2Debug s2debug_override_;
  bool origin_inside_;      // Does the loop contains S2::Origin()?

  // In general we build the index the first time it is needed, but we make an
  // exception for Contains(S2Point) because this method has a simple brute
  // force implementation that is also relatively cheap.  For this one method
  // we keep track of the number of calls made and only build the index once
  // enough calls have been made that we think an index would be worthwhile.
  mutable Atomic32 unindexed_contains_calls_;

  // "bound_" is a conservative bound on all points contained by this loop:
  // if A.Contains(P), then A.bound_.Contains(S2LatLng(P)).
  S2LatLngRect bound_;

  // Since "bound_" is not exact, it is possible that a loop A contains
  // another loop B whose bounds are slightly larger.  "subregion_bound_"
  // has been expanded sufficiently to account for this error, i.e.
  // if A.Contains(B), then A.subregion_bound_.Contains(B.bound_).
  S2LatLngRect subregion_bound_;

  // Every S2Loop has a "shape_" member that is used to index the loop.  This
  // shape belongs to the S2Loop and does not need to be freed separately.
  Shape shape_;

  // Spatial index for this loop.
  S2ShapeIndex index_;

  // SWIG doesn't understand "= delete".
#ifndef SWIG
  void operator=(S2Loop const&) = delete;
#endif  // SWIG
};


//////////////////// Implementation Details Follow ////////////////////////


// Any single-vertex loop is interpreted as being either the empty loop or the
// full loop, depending on whether the vertex is in the northern or southern
// hemisphere respectively.
inline S2Point S2Loop::kEmptyVertex() { return S2Point(0, 0, 1); }
inline S2Point S2Loop::kFullVertex() { return S2Point(0, 0, -1); }

inline std::vector<S2Point> S2Loop::kEmpty() {
  return std::vector<S2Point>(1, kEmptyVertex());
}

inline std::vector<S2Point> S2Loop::kFull() {
  return std::vector<S2Point>(1, kFullVertex());
}

inline bool S2Loop::is_empty() const {
  return is_empty_or_full() && !contains_origin();
}

inline bool S2Loop::is_full() const {
  return is_empty_or_full() && contains_origin();
}

inline bool S2Loop::is_empty_or_full() const {
  return num_vertices() == 1;
}

// Since this method is templatized and public, the implementation needs to be
// in the .h file.

template <class T>
T S2Loop::GetSurfaceIntegral(T f_tri(S2Point const&, S2Point const&,
                                     S2Point const&)) const {
  // We sum "f_tri" over a collection T of oriented triangles, possibly
  // overlapping.  Let the sign of a triangle be +1 if it is CCW and -1
  // otherwise, and let the sign of a point "x" be the sum of the signs of the
  // triangles containing "x".  Then the collection of triangles T is chosen
  // such that either:
  //
  //  (1) Each point in the loop interior has sign +1, and sign 0 otherwise; or
  //  (2) Each point in the loop exterior has sign -1, and sign 0 otherwise.
  //
  // The triangles basically consist of a "fan" from vertex 0 to every loop
  // edge that does not include vertex 0.  These triangles will always satisfy
  // either (1) or (2).  However, what makes this a bit tricky is that
  // spherical edges become numerically unstable as their length approaches
  // 180 degrees.  Of course there is not much we can do if the loop itself
  // contains such edges, but we would like to make sure that all the triangle
  // edges under our control (i.e., the non-loop edges) are stable.  For
  // example, consider a loop around the equator consisting of four equally
  // spaced points.  This is a well-defined loop, but we cannot just split it
  // into two triangles by connecting vertex 0 to vertex 2.
  //
  // We handle this type of situation by moving the origin of the triangle fan
  // whenever we are about to create an unstable edge.  We choose a new
  // location for the origin such that all relevant edges are stable.  We also
  // create extra triangles with the appropriate orientation so that the sum
  // of the triangle signs is still correct at every point.

  // The maximum length of an edge for it to be considered numerically stable.
  // The exact value is fairly arbitrary since it depends on the stability of
  // the "f_tri" function.  The value below is quite conservative but could be
  // reduced further if desired.
  static double const kMaxLength = M_PI - 1e-5;

  // The default constructor for T must initialize the value to zero.
  // (This is true for built-in types such as "double".)
  T sum = T();
  S2Point origin = vertex(0);
  for (int i = 1; i + 1 < num_vertices(); ++i) {
    // Let V_i be vertex(i), let O be the current origin, and let length(A,B)
    // be the length of edge (A,B).  At the start of each loop iteration, the
    // "leading edge" of the triangle fan is (O,V_i), and we want to extend
    // the triangle fan so that the leading edge is (O,V_i+1).
    //
    // Invariants:
    //  1. length(O,V_i) < kMaxLength for all (i > 1).
    //  2. Either O == V_0, or O is approximately perpendicular to V_0.
    //  3. "sum" is the oriented integral of f over the area defined by
    //     (O, V_0, V_1, ..., V_i).
    DCHECK(i == 1 || origin.Angle(vertex(i)) < kMaxLength);
    DCHECK(origin == vertex(0) || std::fabs(origin.DotProd(vertex(0))) < 1e-15);

    if (vertex(i+1).Angle(origin) > kMaxLength) {
      // We are about to create an unstable edge, so choose a new origin O'
      // for the triangle fan.
      S2Point old_origin = origin;
      if (origin == vertex(0)) {
        // The following point is well-separated from V_i and V_0 (and
        // therefore V_i+1 as well).
        origin = S2::RobustCrossProd(vertex(0), vertex(i)).Normalize();
      } else if (vertex(i).Angle(vertex(0)) < kMaxLength) {
        // All edges of the triangle (O, V_0, V_i) are stable, so we can
        // revert to using V_0 as the origin.
        origin = vertex(0);
      } else {
        // (O, V_i+1) and (V_0, V_i) are antipodal pairs, and O and V_0 are
        // perpendicular.  Therefore V_0.CrossProd(O) is approximately
        // perpendicular to all of {O, V_0, V_i, V_i+1}, and we can choose
        // this point O' as the new origin.
        origin = vertex(0).CrossProd(old_origin);

        // Advance the edge (V_0,O) to (V_0,O').
        sum += f_tri(vertex(0), old_origin, origin);
      }
      // Advance the edge (O,V_i) to (O',V_i).
      sum += f_tri(old_origin, vertex(i), origin);
    }
    // Advance the edge (O,V_i) to (O,V_i+1).
    sum += f_tri(origin, vertex(i), vertex(i+1));
  }
  // If the origin is not V_0, we need to sum one more triangle.
  if (origin != vertex(0)) {
    // Advance the edge (O,V_n-1) to (O,V_0).
    sum += f_tri(origin, vertex(num_vertices() - 1), vertex(0));
  }
  return sum;
}

#endif  // S2_S2LOOP_H_