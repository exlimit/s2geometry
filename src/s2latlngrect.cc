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

#include "s2latlngrect.h"

#include <algorithm>
#include <iosfwd>
#include <iostream>

#include <glog/logging.h>

#include "util/coding/coder.h"
#include "s2cap.h"
#include "s2cell.h"
#include "s2edgeutil.h"

using std::max;
using std::min;

static const unsigned char kCurrentLosslessEncodingVersionNumber = 1;

S2LatLngRect S2LatLngRect::FromCenterSize(S2LatLng const& center,
                                          S2LatLng const& size) {
  return FromPoint(center).Expanded(0.5 * size);
}

S2LatLngRect S2LatLngRect::FromPoint(S2LatLng const& p) {
  DLOG_IF(ERROR, !p.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::GetDistance: " << p;

  return S2LatLngRect(p, p);
}

S2LatLngRect S2LatLngRect::FromPointPair(S2LatLng const& p1,
                                         S2LatLng const& p2) {
  DLOG_IF(ERROR, !p1.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::FromPointPair: " << p1;

  DLOG_IF(ERROR, !p2.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::FromPointPair: " << p2;

  return S2LatLngRect(R1Interval::FromPointPair(p1.lat().radians(),
                                                p2.lat().radians()),
                      S1Interval::FromPointPair(p1.lng().radians(),
                                                p2.lng().radians()));
}

S2LatLngRect* S2LatLngRect::Clone() const {
  return new S2LatLngRect(*this);
}

S2LatLng S2LatLngRect::GetVertex(int k) const {
  // Twiddle bits to return the points in CCW order (lower left, lower right,
  // upper right, upper left).
  return S2LatLng::FromRadians(lat_[k>>1], lng_[(k>>1) ^ (k&1)]);
}

S2LatLng S2LatLngRect::GetCenter() const {
  return S2LatLng::FromRadians(lat_.GetCenter(), lng_.GetCenter());
}

S2LatLng S2LatLngRect::GetSize() const {
  return S2LatLng::FromRadians(lat_.GetLength(), lng_.GetLength());
}

double S2LatLngRect::Area() const {
  if (is_empty()) return 0.0;
  // This is the size difference of the two spherical caps, multiplied by
  // the longitude ratio.
  return lng().GetLength() * (sin(lat_hi()) - sin(lat_lo()));
}

S2Point S2LatLngRect::GetCentroid() const {
  // When a sphere is divided into slices of constant thickness by a set of
  // parallel planes, all slices have the same surface area.  This implies
  // that the z-component of the centroid is simply the midpoint of the
  // z-interval spanned by the S2LatLngRect.
  //
  // Similarly, it is easy to see that the (x,y) of the centroid lies in the
  // plane through the midpoint of the rectangle's longitude interval.  We
  // only need to determine the distance "d" of this point from the z-axis.
  //
  // Let's restrict our attention to a particular z-value.  In this z-plane,
  // the S2LatLngRect is a circular arc.  The centroid of this arc lies on a
  // radial line through the midpoint of the arc, and at a distance from the
  // z-axis of
  //
  //     r * (sin(alpha) / alpha)
  //
  // where r = sqrt(1-z^2) is the radius of the arc, and "alpha" is half of
  // the arc length (i.e., the arc covers longitudes [-alpha, alpha]).
  //
  // To find the centroid distance from the z-axis for the entire rectangle,
  // we just need to integrate over the z-interval.  This gives
  //
  //    d = Integrate[sqrt(1-z^2)*sin(alpha)/alpha, z1..z2] / (z2 - z1)
  //
  // where [z1, z2] is the range of z-values covered by the rectangle.  This
  // simplifies to
  //
  //    d = sin(alpha)/(2*alpha*(z2-z1))*(z2*r2 - z1*r1 + theta2 - theta1)
  //
  // where [theta1, theta2] is the latitude interval, z1=sin(theta1),
  // z2=sin(theta2), r1=cos(theta1), and r2=cos(theta2).
  //
  // Finally, we want to return not the centroid itself, but the centroid
  // scaled by the area of the rectangle.  The area of the rectangle is
  //
  //    A = 2 * alpha * (z2 - z1)
  //
  // which fortunately appears in the denominator of "d".

  if (is_empty()) return S2Point();
  double z1 = sin(lat_lo()), z2 = sin(lat_hi());
  double r1 = cos(lat_lo()), r2 = cos(lat_hi());
  double alpha = 0.5 * lng_.GetLength();
  double r = sin(alpha) * (r2 * z2 - r1 * z1 + lat_.GetLength());
  double lng = lng_.GetCenter();
  double z = alpha * (z2 + z1) * (z2 - z1);  // scaled by the area
  return S2Point(r * cos(lng), r * sin(lng), z);
}

bool S2LatLngRect::Contains(S2LatLng const& ll) const {
  DLOG_IF(ERROR, !ll.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::Contains: " << ll;

  return (lat_.Contains(ll.lat().radians()) &&
          lng_.Contains(ll.lng().radians()));
}

bool S2LatLngRect::InteriorContains(S2Point const& p) const {
  return InteriorContains(S2LatLng(p));
}

bool S2LatLngRect::InteriorContains(S2LatLng const& ll) const {
  DLOG_IF(ERROR, !ll.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::InteriorContains: " << ll;

  return (lat_.InteriorContains(ll.lat().radians()) &&
          lng_.InteriorContains(ll.lng().radians()));
}

bool S2LatLngRect::Contains(S2LatLngRect const& other) const {
  return lat_.Contains(other.lat_) && lng_.Contains(other.lng_);
}

bool S2LatLngRect::InteriorContains(S2LatLngRect const& other) const {
  return (lat_.InteriorContains(other.lat_) &&
          lng_.InteriorContains(other.lng_));
}

bool S2LatLngRect::Intersects(S2LatLngRect const& other) const {
  return lat_.Intersects(other.lat_) && lng_.Intersects(other.lng_);
}

bool S2LatLngRect::InteriorIntersects(S2LatLngRect const& other) const {
  return (lat_.InteriorIntersects(other.lat_) &&
          lng_.InteriorIntersects(other.lng_));
}

void S2LatLngRect::AddPoint(S2Point const& p) {
  AddPoint(S2LatLng(p));
}

void S2LatLngRect::AddPoint(S2LatLng const& ll) {
  DLOG_IF(ERROR, !ll.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::AddPoint: " << ll;

  lat_.AddPoint(ll.lat().radians());
  lng_.AddPoint(ll.lng().radians());
}

S2LatLngRect S2LatLngRect::Expanded(S2LatLng const& margin) const {
  R1Interval lat = lat_.Expanded(margin.lat().radians());
  S1Interval lng = lng_.Expanded(margin.lng().radians());
  if (lat.is_empty() || lng.is_empty()) return Empty();
  return S2LatLngRect(lat.Intersection(FullLat()), lng);
}

S2LatLngRect S2LatLngRect::PolarClosure() const {
  if (lat_.lo() == -M_PI_2 || lat_.hi() == M_PI_2) {
    return S2LatLngRect(lat_, S1Interval::Full());
  }
  return *this;
}

S2LatLngRect S2LatLngRect::Union(S2LatLngRect const& other) const {
  return S2LatLngRect(lat_.Union(other.lat_),
                      lng_.Union(other.lng_));
}

S2LatLngRect S2LatLngRect::Intersection(S2LatLngRect const& other) const {
  R1Interval lat = lat_.Intersection(other.lat_);
  S1Interval lng = lng_.Intersection(other.lng_);
  if (lat.is_empty() || lng.is_empty()) {
    // The lat/lng ranges must either be both empty or both non-empty.
    return Empty();
  }
  return S2LatLngRect(lat, lng);
}

S2LatLngRect S2LatLngRect::ExpandedByDistance(S1Angle distance) const {
  if (distance >= S1Angle::Zero()) {
    // The most straightforward approach is to build a cap centered on each
    // vertex and take the union of all the bounding rectangles (including the
    // original rectangle; this is necessary for very large rectangles).

    // TODO(ericv): Update this code to use an algorithm similar to the one
    // below.
    double height = S2Cap::RadiusToHeight(distance);
    S2LatLngRect r = *this;
    for (int k = 0; k < 4; ++k) {
      S2Cap vertex_cap =
          S2Cap::FromCenterHeight(GetVertex(k).ToPoint(), height);
      r = r.Union(vertex_cap.GetRectBound());
    }
    return r;
  } else {
    // Shrink the latitude interval unless the latitude interval contains a pole
    // and the longitude interval is full, in which case the rectangle has no
    // boundary at that pole.
    R1Interval lat_result(
        lat().lo() <= FullLat().lo() && lng().is_full() ?
            FullLat().lo() : lat().lo() - distance.radians(),
        lat().hi() >= FullLat().hi() && lng().is_full() ?
            FullLat().hi() : lat().hi() + distance.radians());
    if (lat_result.is_empty()) {
      return S2LatLngRect::Empty();
    }

    // Maximum absolute value of a latitude in lat_result. At this latitude,
    // the cap occupies the largest longitude interval.
    double max_abs_lat = max(-lat_result.lo(), lat_result.hi());

    // Compute the largest longitude interval that the cap occupies. We use the
    // law of sines for spherical triangles. For the details, see the comment in
    // S2Cap::GetRectBound().
    //
    // When sin_a >= sin_c, the cap covers all the latitude.
    double sin_a = sin(-distance.radians());
    double sin_c = cos(max_abs_lat);
    double max_lng_margin = sin_a < sin_c ? asin(sin_a / sin_c) : M_PI_2;

    S1Interval lng_result = lng().Expanded(-max_lng_margin);
    if (lng_result.is_empty()) {
      return S2LatLngRect::Empty();
    }
    return S2LatLngRect(lat_result, lng_result);
  }
}

S2Cap S2LatLngRect::GetCapBound() const {
  // We consider two possible bounding caps, one whose axis passes
  // through the center of the lat-long rectangle and one whose axis
  // is the north or south pole.  We return the smaller of the two caps.

  if (is_empty()) return S2Cap::Empty();

  double pole_z, pole_angle;
  if (lat_.lo() + lat_.hi() < 0) {
    // South pole axis yields smaller cap.
    pole_z = -1;
    pole_angle = M_PI_2 + lat_.hi();
  } else {
    pole_z = 1;
    pole_angle = M_PI_2 - lat_.lo();
  }
  S2Cap pole_cap(S2Point(0, 0, pole_z), S1Angle::Radians(pole_angle));

  // For bounding rectangles that span 180 degrees or less in longitude, the
  // maximum cap size is achieved at one of the rectangle vertices.  For
  // rectangles that are larger than 180 degrees, we punt and always return a
  // bounding cap centered at one of the two poles.
  double lng_span = lng_.hi() - lng_.lo();
  if (remainder(lng_span, 2 * M_PI) >= 0 && lng_span < 2 * M_PI) {
    S2Cap mid_cap(GetCenter().ToPoint(), S1Angle::Radians(0));
    for (int k = 0; k < 4; ++k) {
      mid_cap.AddPoint(GetVertex(k).ToPoint());
    }
    if (mid_cap.height() < pole_cap.height())
      return mid_cap;
  }
  return pole_cap;
}

S2LatLngRect S2LatLngRect::GetRectBound() const {
  return *this;
}

bool S2LatLngRect::Contains(S2Cell const& cell) const {
  // A latitude-longitude rectangle contains a cell if and only if it contains
  // the cell's bounding rectangle.  This test is exact from a mathematical
  // point of view, assuming that the bounds returned by S2Cell::GetRectBound()
  // are tight.  However, note that there can be a loss of precision when
  // converting between representations -- for example, if an S2Cell is
  // converted to a polygon, the polygon's bounding rectangle may not contain
  // the cell's bounding rectangle.  This has some slightly unexpected side
  // effects; for instance, if one creates an S2Polygon from an S2Cell, the
  // polygon will contain the cell, but the polygon's bounding box will not.
  return Contains(cell.GetRectBound());
}

bool S2LatLngRect::MayIntersect(S2Cell const& cell) const {
  // This test is cheap but is NOT exact (see s2latlngrect.h).
  return Intersects(cell.GetRectBound());
}

void S2LatLngRect::Encode(Encoder* encoder) const {
  encoder->Ensure(40);  // sufficient

  encoder->put8(kCurrentLosslessEncodingVersionNumber);
  encoder->putdouble(lat_.lo());
  encoder->putdouble(lat_.hi());
  encoder->putdouble(lng_.lo());
  encoder->putdouble(lng_.hi());

  DCHECK_GE(encoder->avail(), 0);
}

bool S2LatLngRect::Decode(Decoder* decoder) {
  if (decoder->avail() < sizeof(unsigned char) + 4 * sizeof(double))
    return false;
  unsigned char version = decoder->get8();
  if (version > kCurrentLosslessEncodingVersionNumber) return false;

  double lat_lo = decoder->getdouble();
  double lat_hi = decoder->getdouble();
  lat_ = R1Interval(lat_lo, lat_hi);
  double lng_lo = decoder->getdouble();
  double lng_hi = decoder->getdouble();
  lng_ = S1Interval(lng_lo, lng_hi);

  if (!is_valid()) {
    DLOG_IF(ERROR, FLAGS_s2debug)
        << "Invalid result in S2LatLngRect::Decode: " << *this;
    return false;
  }

  return true;
}

bool S2LatLngRect::IntersectsLngEdge(S2Point const& a, S2Point const& b,
                                     R1Interval const& lat, double lng) {
  // Return true if the segment AB intersects the given edge of constant
  // longitude.  The nice thing about edges of constant longitude is that
  // they are straight lines on the sphere (geodesics).

  return S2EdgeUtil::SimpleCrossing(
      a, b, S2LatLng::FromRadians(lat.lo(), lng).ToPoint(),
      S2LatLng::FromRadians(lat.hi(), lng).ToPoint());
}

bool S2LatLngRect::IntersectsLatEdge(S2Point const& a, S2Point const& b,
                                     double lat, S1Interval const& lng) {
  // Return true if the segment AB intersects the given edge of constant
  // latitude.  Unfortunately, lines of constant latitude are curves on
  // the sphere.  They can intersect a straight edge in 0, 1, or 2 points.
  DCHECK(S2::IsUnitLength(a));
  DCHECK(S2::IsUnitLength(b));

  // First, compute the normal to the plane AB that points vaguely north.
  Vector3_d z = S2::RobustCrossProd(a, b).Normalize();
  if (z[2] < 0) z = -z;

  // Extend this to an orthonormal frame (x,y,z) where x is the direction
  // where the great circle through AB achieves its maximium latitude.
  Vector3_d y = S2::RobustCrossProd(z, S2Point(0, 0, 1)).Normalize();
  Vector3_d x = y.CrossProd(z);
  DCHECK(S2::IsUnitLength(x));
  DCHECK_GE(x[2], 0);

  // Compute the angle "theta" from the x-axis (in the x-y plane defined
  // above) where the great circle intersects the given line of latitude.
  double sin_lat = sin(lat);
  if (fabs(sin_lat) >= x[2]) {
    return false;  // The great circle does not reach the given latitude.
  }
  DCHECK_GT(x[2], 0);
  double cos_theta = sin_lat / x[2];
  double sin_theta = sqrt(1 - cos_theta * cos_theta);
  double theta = atan2(sin_theta, cos_theta);

  // The candidate intersection points are located +/- theta in the x-y
  // plane.  For an intersection to be valid, we need to check that the
  // intersection point is contained in the interior of the edge AB and
  // also that it is contained within the given longitude interval "lng".

  // Compute the range of theta values spanned by the edge AB.
  S1Interval ab_theta = S1Interval::FromPointPair(
      atan2(a.DotProd(y), a.DotProd(x)),
      atan2(b.DotProd(y), b.DotProd(x)));

  if (ab_theta.Contains(theta)) {
    // Check if the intersection point is also in the given "lng" interval.
    S2Point isect = x * cos_theta + y * sin_theta;
    if (lng.Contains(atan2(isect[1], isect[0]))) return true;
  }
  if (ab_theta.Contains(-theta)) {
    // Check if the intersection point is also in the given "lng" interval.
    S2Point isect = x * cos_theta - y * sin_theta;
    if (lng.Contains(atan2(isect[1], isect[0]))) return true;
  }
  return false;
}

bool S2LatLngRect::Intersects(S2Cell const& cell) const {
  // First we eliminate the cases where one region completely contains the
  // other.  Once these are disposed of, then the regions will intersect
  // if and only if their boundaries intersect.

  if (is_empty()) return false;
  if (Contains(cell.GetCenterRaw())) return true;
  if (cell.Contains(GetCenter().ToPoint())) return true;

  // Quick rejection test (not required for correctness).
  if (!Intersects(cell.GetRectBound())) return false;

  // Precompute the cell vertices as points and latitude-longitudes.  We also
  // check whether the S2Cell contains any corner of the rectangle, or
  // vice-versa, since the edge-crossing tests only check the edge interiors.

  S2Point cell_v[4];
  S2LatLng cell_ll[4];
  for (int i = 0; i < 4; ++i) {
    cell_v[i] = cell.GetVertex(i);  // Must be normalized.
    cell_ll[i] = S2LatLng(cell_v[i]);
    if (Contains(cell_ll[i])) return true;
    if (cell.Contains(GetVertex(i).ToPoint())) return true;
  }

  // Now check whether the boundaries intersect.  Unfortunately, a
  // latitude-longitude rectangle does not have straight edges -- two edges
  // are curved, and at least one of them is concave.

  for (int i = 0; i < 4; ++i) {
    S1Interval edge_lng = S1Interval::FromPointPair(
        cell_ll[i].lng().radians(), cell_ll[(i+1)&3].lng().radians());
    if (!lng_.Intersects(edge_lng)) continue;

    S2Point const& a = cell_v[i];
    S2Point const& b = cell_v[(i+1)&3];
    if (edge_lng.Contains(lng_.lo())) {
      if (IntersectsLngEdge(a, b, lat_, lng_.lo())) return true;
    }
    if (edge_lng.Contains(lng_.hi())) {
      if (IntersectsLngEdge(a, b, lat_, lng_.hi())) return true;
    }
    if (IntersectsLatEdge(a, b, lat_.lo(), lng_)) return true;
    if (IntersectsLatEdge(a, b, lat_.hi(), lng_)) return true;
  }
  return false;
}

S1Angle S2LatLngRect::GetDistance(S2LatLngRect const& other) const {
  S2LatLngRect const& a = *this;
  S2LatLngRect const& b = other;
  DCHECK(!a.is_empty());
  DCHECK(!b.is_empty());

  // First, handle the trivial cases where the longitude intervals overlap.
  if (a.lng().Intersects(b.lng())) {
    if (a.lat().Intersects(b.lat()))
      return S1Angle::Radians(0);  // Intersection between a and b.

    // We found an overlap in the longitude interval, but not in the latitude
    // interval. This means the shortest path travels along some line of
    // longitude connecting the high-latitude of the lower rect with the
    // low-latitude of the higher rect.
    S1Angle lo, hi;
    if (a.lat().lo() > b.lat().hi()) {
      lo = b.lat_hi();
      hi = a.lat_lo();
    } else {
      lo = a.lat_hi();
      hi = b.lat_lo();
    }
    return hi - lo;
  }

  // The longitude intervals don't overlap. In this case, the closest points
  // occur somewhere on the pair of longitudinal edges which are nearest in
  // longitude-space.
  S1Angle a_lng, b_lng;
  S1Interval lo_hi = S1Interval::FromPointPair(a.lng().lo(), b.lng().hi());
  S1Interval hi_lo = S1Interval::FromPointPair(a.lng().hi(), b.lng().lo());
  if (lo_hi.GetLength() < hi_lo.GetLength()) {
    a_lng = a.lng_lo();
    b_lng = b.lng_hi();
  } else {
    a_lng = a.lng_hi();
    b_lng = b.lng_lo();
  }

  // The shortest distance between the two longitudinal segments will include at
  // least one segment endpoint. We could probably narrow this down further to a
  // single point-edge distance by comparing the relative latitudes of the
  // endpoints, but for the sake of clarity, we'll do all four point-edge
  // distance tests.
  S2Point a_lo = S2LatLng(a.lat_lo(), a_lng).ToPoint();
  S2Point a_hi = S2LatLng(a.lat_hi(), a_lng).ToPoint();
  S2Point b_lo = S2LatLng(b.lat_lo(), b_lng).ToPoint();
  S2Point b_hi = S2LatLng(b.lat_hi(), b_lng).ToPoint();
  return min(S2EdgeUtil::GetDistance(a_lo, b_lo, b_hi),
         min(S2EdgeUtil::GetDistance(a_hi, b_lo, b_hi),
         min(S2EdgeUtil::GetDistance(b_lo, a_lo, a_hi),
             S2EdgeUtil::GetDistance(b_hi, a_lo, a_hi))));
}

S1Angle S2LatLngRect::GetDistance(S2LatLng const& p) const {
  // The algorithm here is the same as in GetDistance(S2LatLngRect), only
  // with simplified calculations.
  S2LatLngRect const& a = *this;
  DLOG_IF(ERROR, a.is_empty())
      << "Empty S2LatLngRect in S2LatLngRect::GetDistance: " << a;

  DLOG_IF(ERROR, !p.is_valid())
      << "Invalid S2LatLng in S2LatLngRect::GetDistance: " << p;

  if (a.lng().Contains(p.lng().radians())) {
    return S1Angle::Radians(max(0.0, max(p.lat().radians() - a.lat().hi(),
                                         a.lat().lo() - p.lat().radians())));
  }

  S1Interval interval(a.lng().hi(), a.lng().GetComplementCenter());
  double a_lng;
  if (interval.Contains(p.lng().radians())) {
    a_lng = a.lng().hi();
  } else {
    a_lng = a.lng().lo();
  }
  S2Point lo = S2LatLng::FromRadians(a.lat().lo(), a_lng).ToPoint();
  S2Point hi = S2LatLng::FromRadians(a.lat().hi(), a_lng).ToPoint();
  return S2EdgeUtil::GetDistance(p.ToPoint(), lo, hi);
}

S1Angle S2LatLngRect::GetHausdorffDistance(S2LatLngRect const& other) const {
  return max(GetDirectedHausdorffDistance(other),
             other.GetDirectedHausdorffDistance(*this));
}

S1Angle S2LatLngRect::GetDirectedHausdorffDistance(
    S2LatLngRect const& other) const {
  if (is_empty()) {
    return S1Angle::Radians(0);
  }
  if (other.is_empty()) {
    return S1Angle::Radians(M_PI);  // maximum possible distance on S2
  }

  double lng_distance = lng().GetDirectedHausdorffDistance(other.lng());
  DCHECK_GE(lng_distance, 0);
  return GetDirectedHausdorffDistance(lng_distance, lat(), other.lat());
}

// Return the directed Hausdorff distance from one longitudinal edge spanning
// latitude range 'a_lat' to the other longitudinal edge spanning latitude
// range 'b_lat', with their longitudinal difference given by 'lng_diff'.
S1Angle S2LatLngRect::GetDirectedHausdorffDistance(
    double lng_diff, R1Interval const& a, R1Interval const& b) {
  // By symmetry, we can assume a's longtitude is 0 and b's longtitude is
  // lng_diff. Call b's two endpoints b_lo and b_hi. Let H be the hemisphere
  // containing a and delimited by the longitude line of b. The Voronoi diagram
  // of b on H has three edges (portions of great circles) all orthogonal to b
  // and meeting at b_lo cross b_hi.
  // E1: (b_lo, b_lo cross b_hi)
  // E2: (b_hi, b_lo cross b_hi)
  // E3: (-b_mid, b_lo cross b_hi), where b_mid is the midpoint of b
  //
  // They subdivide H into three Voronoi regions. Depending on how longitude 0
  // (which contains edge a) intersects these regions, we distinguish two cases:
  // Case 1: it intersects three regions. This occurs when lng_diff <= M_PI_2.
  // Case 2: it intersects only two regions. This occurs when lng_diff > M_PI_2.
  //
  // In the first case, the directed Hausdorff distance to edge b can only be
  // realized by the following points on a:
  // A1: two endpoints of a.
  // A2: intersection of a with the equator, if b also intersects the equator.
  //
  // In the second case, the directed Hausdorff distance to edge b can only be
  // realized by the following points on a:
  // B1: two endpoints of a.
  // B2: intersection of a with E3
  // B3: farthest point from b_lo to the interior of D, and farthest point from
  //     b_hi to the interior of U, if any, where D (resp. U) is the portion
  //     of edge a below (resp. above) the intersection point from B2.

  DCHECK_GE(lng_diff, 0);
  DCHECK_LE(lng_diff, M_PI);

  if (lng_diff == 0) {
    return S1Angle::Radians(a.GetDirectedHausdorffDistance(b));
  }

  // Assumed longtitude of b.
  double b_lng = lng_diff;
  // Two endpoints of b.
  S2Point b_lo = S2LatLng::FromRadians(b.lo(), b_lng).ToPoint();
  S2Point b_hi = S2LatLng::FromRadians(b.hi(), b_lng).ToPoint();

  // Handling of each case outlined at the top of the function starts here.
  // This is initialized a few lines below.
  S1Angle max_distance;

  // Cases A1 and B1.
  S2Point a_lo = S2LatLng::FromRadians(a.lo(), 0).ToPoint();
  S2Point a_hi = S2LatLng::FromRadians(a.hi(), 0).ToPoint();
  max_distance = S2EdgeUtil::GetDistance(a_lo, b_lo, b_hi);
  max_distance = max(
      max_distance, S2EdgeUtil::GetDistance(a_hi, b_lo, b_hi));

  if (lng_diff <= M_PI_2) {
    // Case A2.
    if (a.Contains(0) && b.Contains(0)) {
      max_distance = max(max_distance, S1Angle::Radians(lng_diff));
    }
  } else {
    // Case B2.
    const S2Point& p = GetBisectorIntersection(b, b_lng);
    double p_lat = S2LatLng::Latitude(p).radians();
    if (a.Contains(p_lat)) {
      max_distance = max(max_distance, S1Angle(p, b_lo));
    }

    // Case B3.
    if (p_lat > a.lo()) {
      max_distance = max(max_distance, GetInteriorMaxDistance(
          R1Interval(a.lo(), min(p_lat, a.hi())), b_lo));
    }
    if (p_lat < a.hi()) {
      max_distance = max(max_distance, GetInteriorMaxDistance(
          R1Interval(max(p_lat, a.lo()), a.hi()), b_hi));
    }
  }

  return max_distance;
}

// Return the intersection of longitude 0 with the bisector of an edge
// on longitude 'lng' and spanning latitude range 'lat'.
S2Point S2LatLngRect::GetBisectorIntersection(R1Interval const& lat,
                                              double lng) {
  lng = fabs(lng);
  double lat_center = lat.GetCenter();
  // A vector orthogonal to the bisector of the given longitudinal edge.
  S2LatLng ortho_bisector;
  if (lat_center >= 0) {
    ortho_bisector = S2LatLng::FromRadians(lat_center - M_PI_2, lng);
  } else {
    ortho_bisector = S2LatLng::FromRadians(-lat_center - M_PI_2, lng - M_PI);
  }
  // A vector orthogonal to longitude 0.
  static const S2Point ortho_lng = S2Point(0, -1, 0);
  return S2::RobustCrossProd(ortho_lng, ortho_bisector.ToPoint());
}

// Return max distance from a point b to the segment spanning latitude range
// a_lat on longitude 0, if the max occurs in the interior of a_lat. Otherwise
// return -1.
S1Angle S2LatLngRect::GetInteriorMaxDistance(R1Interval const& a_lat,
                                             S2Point const& b) {
  // Longitude 0 is in the y=0 plane. b.x() >= 0 implies that the maximum
  // does not occur in the interior of a_lat.
  if (a_lat.is_empty() || b.x() >= 0) return S1Angle::Radians(-1);

  // Project b to the y=0 plane. The antipodal of the normalized projection is
  // the point at which the maxium distance from b occurs, if it is contained
  // in a_lat.
  S2Point intersection_point = S2Point(-b.x(), 0, -b.z()).Normalize();
  if (a_lat.InteriorContains(
      S2LatLng::Latitude(intersection_point).radians())) {
    return S1Angle(b, intersection_point);
  } else {
    return S1Angle::Radians(-1);
  }
}

bool S2LatLngRect::Contains(S2Point const& p) const {
  return Contains(S2LatLng(p));
}

bool S2LatLngRect::ApproxEquals(S2LatLngRect const& other,
                                double max_error) const {
  return (lat_.ApproxEquals(other.lat_, max_error) &&
          lng_.ApproxEquals(other.lng_, max_error));
}

bool S2LatLngRect::ApproxEquals(S2LatLngRect const& other,
                                S2LatLng const& max_error) const {
  return (lat_.ApproxEquals(other.lat_, max_error.lat().radians()) &&
          lng_.ApproxEquals(other.lng_, max_error.lng().radians()));
}

std::ostream& operator<<(std::ostream& os, S2LatLngRect const& r) {
  return os << "[Lo" << r.lo() << ", Hi" << r.hi() << "]";
}
