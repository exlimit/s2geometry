// Copyright 2017 Google Inc. All Rights Reserved.
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

#include "s2/s2textformat.h"

#include <vector>
#include <gtest/gtest.h>
#include "s2/third_party/absl/strings/str_split.h"
#include "s2/mutable_s2shapeindex.h"
#include "s2/s1angle.h"
#include "s2/s2latlng.h"
#include "s2/s2loop.h"
#include "s2/s2polyline.h"
#include "s2/s2shapeutil.h"
#include "s2/s2testing.h"

using std::unique_ptr;
using std::vector;

namespace {

static const int kIters = 10000;

// Verify that s2textformat::ToString() formats the given lat/lng with at most
// "max_digits" after the decimal point and has no trailing zeros.
void ExpectMaxDigits(const S2LatLng& ll, int max_digits) {
  string result = s2textformat::ToString(ll.ToPoint());
  vector<string> values = absl::StrSplit(result, ':', absl::SkipEmpty());
  EXPECT_EQ(2, values.size()) << result;
  for (const auto& value : values) {
    int num_digits = 0;
    if (value.find('.') != string::npos) {
      num_digits = value.size() - value.find('.') - 1;
      EXPECT_NE('0', value.back());
    }
    EXPECT_LE(num_digits, max_digits) << value;
  }
}

void ExpectString(const string& expected, const S2LatLng& ll) {
  EXPECT_EQ(expected, s2textformat::ToString(ll.ToPoint()));
}

TEST(ToString, SpecialCases) {
  ExpectString("0:0", S2LatLng::FromDegrees(0, 0));
  ExpectString("1e-20:1e-30", S2LatLng::FromDegrees(1e-20, 1e-30));
}

TEST(ToString, MinimalDigitsE5) {
  for (int iter = 0; iter < kIters; ++iter) {
    S2LatLng ll(S2Testing::RandomPoint());
    S2LatLng ll_e5 = S2LatLng::FromE5(ll.lat().e5(), ll.lng().e5());
    ExpectMaxDigits(ll_e5, 5);
  }
}

TEST(ToString, MinimalDigitsE6) {
  for (int iter = 0; iter < kIters; ++iter) {
    S2LatLng ll(S2Testing::RandomPoint());
    S2LatLng ll_e6 = S2LatLng::FromE6(ll.lat().e6(), ll.lng().e6());
    ExpectMaxDigits(ll_e6, 6);
  }
}

TEST(ToString, MinimalDigitsE7) {
  ExpectMaxDigits(S2LatLng::FromDegrees(0, 0), 7);
  for (int iter = 0; iter < kIters; ++iter) {
    S2LatLng ll(S2Testing::RandomPoint());
    S2LatLng ll_e7 = S2LatLng::FromE7(ll.lat().e7(), ll.lng().e7());
    ExpectMaxDigits(ll_e7, 7);
  }
}

TEST(ToString, MinimalDigitsDoubleConstants) {
  // Verify that points specified as floating-point literals in degrees using
  // up to 10 digits after the decimal point are formatted with the minimal
  // number of digits.
  for (int iter = 0; iter < kIters; ++iter) {
    int max_digits = S2Testing::rnd.Uniform(11);
    int64 scale = MathUtil::FastInt64Round(pow(10, max_digits));
    int64 lat = MathUtil::FastInt64Round(
        S2Testing::rnd.UniformDouble(-90 * scale, 90 * scale));
    int64 lng = MathUtil::FastInt64Round(
        S2Testing::rnd.UniformDouble(-180 * scale, 180 * scale));
    S2LatLng ll = S2LatLng::FromDegrees(lat / static_cast<double>(scale),
                                        lng / static_cast<double>(scale));
    ExpectMaxDigits(ll, max_digits);
  }
}

TEST(ToString, UninitializedLoop) {
  S2Loop loop;
  EXPECT_EQ("", s2textformat::ToString(loop));
}

TEST(ToString, EmptyLoop) {
  S2Loop empty(S2Loop::kEmpty());
  EXPECT_EQ("empty", s2textformat::ToString(empty));
}

TEST(ToString, FullLoop) {
  S2Loop full(S2Loop::kFull());
  EXPECT_EQ("full", s2textformat::ToString(full));
}

TEST(ToString, EmptyPolyline) {
  S2Polyline polyline;
  EXPECT_EQ("", s2textformat::ToString(polyline));
}

TEST(ToString, EmptyPointVector) {
  vector<S2Point> points;
  EXPECT_EQ("", s2textformat::ToString(points));
}

TEST(ToString, EmptyPolygon) {
  S2Polygon empty;
  EXPECT_EQ("empty", s2textformat::ToString(empty));
}

TEST(ToString, FullPolygon) {
  S2Polygon full(absl::make_unique<S2Loop>(S2Loop::kFull()));
  EXPECT_EQ("full", s2textformat::ToString(full));
}

TEST(MakeLaxPolygon, Empty) {
  // Verify that "" and "empty" both create empty polygons.
  auto shape = s2textformat::MakeLaxPolygonOrDie("");
  EXPECT_EQ(0, shape->num_loops());
  shape = s2textformat::MakeLaxPolygonOrDie("empty");
  EXPECT_EQ(0, shape->num_loops());
}

TEST(MakeLaxPolygon, Full) {
  auto shape = s2textformat::MakeLaxPolygonOrDie("full");
  EXPECT_EQ(1, shape->num_loops());
  EXPECT_EQ(0, shape->num_loop_vertices(0));
}

TEST(MakeLaxPolygon, FullWithHole) {
  auto shape = s2textformat::MakeLaxPolygonOrDie("full; 0:0");
  EXPECT_EQ(2, shape->num_loops());
  EXPECT_EQ(0, shape->num_loop_vertices(0));
  EXPECT_EQ(1, shape->num_loop_vertices(1));
  EXPECT_EQ(1, shape->num_edges());
}

void TestS2ShapeIndex(const string& str) {
  EXPECT_EQ(str, s2textformat::ToString(*s2textformat::MakeIndexOrDie(str)));
}

TEST(ToString, S2ShapeIndex) {
  TestS2ShapeIndex("# #");
  TestS2ShapeIndex("0:0 # #");
  TestS2ShapeIndex("0:0 | 1:0 # #");
  TestS2ShapeIndex("0:0 | 1:0 # #");
  TestS2ShapeIndex("# 0:0, 0:0 #");
  TestS2ShapeIndex("# 0:0, 0:0 | 1:0, 2:0 #");
  TestS2ShapeIndex("# # 0:0");
  TestS2ShapeIndex("# # 0:0, 0:1");
  TestS2ShapeIndex("# # 0:0, 0:1, 1:0");
  TestS2ShapeIndex("# # 0:0, 0:1, 1:0; 2:2");
}

TEST(MakePoint, ValidInput) {
  S2Point point;
  EXPECT_TRUE(s2textformat::MakePoint("-20:150", &point));
  EXPECT_EQ(S2LatLng::FromDegrees(-20, 150).ToPoint(), point);
}

TEST(MakePoint, InvalidInput) {
  S2Point point;
  EXPECT_FALSE(s2textformat::MakePoint("blah", &point));
}

TEST(SafeParseLatLngs, ValidInput) {
  std::vector<S2LatLng> latlngs;
  EXPECT_TRUE(
      s2textformat::ParseLatLngs("-20:150, -20:151, -19:150", &latlngs));
  ASSERT_EQ(3, latlngs.size());
  EXPECT_EQ(latlngs[0], S2LatLng::FromDegrees(-20, 150));
  EXPECT_EQ(latlngs[1], S2LatLng::FromDegrees(-20, 151));
  EXPECT_EQ(latlngs[2], S2LatLng::FromDegrees(-19, 150));
}

TEST(SafeParseLatLngs, InvalidInput) {
  std::vector<S2LatLng> latlngs;
  EXPECT_FALSE(s2textformat::ParseLatLngs("blah", &latlngs));
}

TEST(SafeParsePoints, ValidInput) {
  std::vector<S2Point> vertices;
  EXPECT_TRUE(
      s2textformat::ParsePoints("-20:150, -20:151, -19:150", &vertices));
  ASSERT_EQ(3, vertices.size());
  EXPECT_EQ(vertices[0], S2LatLng::FromDegrees(-20, 150).ToPoint());
  EXPECT_EQ(vertices[1], S2LatLng::FromDegrees(-20, 151).ToPoint());
  EXPECT_EQ(vertices[2], S2LatLng::FromDegrees(-19, 150).ToPoint());
}

TEST(SafeParsePoints, InvalidInput) {
  std::vector<S2Point> vertices;
  EXPECT_FALSE(s2textformat::ParsePoints("blah", &vertices));
}

TEST(SafeMakeLatLngRect, ValidInput) {
  S2LatLngRect rect;
  EXPECT_TRUE(s2textformat::MakeLatLngRect("-10:-10, 10:10", &rect));
  EXPECT_EQ(rect, S2LatLngRect(S2LatLng::FromDegrees(-10, -10),
                               S2LatLng::FromDegrees(10, 10)));
}

TEST(SafeMakeLatLngRect, InvalidInput) {
  S2LatLngRect rect;
  EXPECT_FALSE(s2textformat::MakeLatLngRect("blah", &rect));
}

TEST(SafeMakeLatLng, ValidInput) {
  S2LatLng latlng;
  EXPECT_TRUE(s2textformat::MakeLatLng("-12.3:45.6", &latlng));
  EXPECT_EQ(latlng, S2LatLng(S2LatLng::FromDegrees(-12.3, 45.6)));
}

TEST(SafeMakeLatLng, InvalidInput) {
  S2LatLng latlng;
  EXPECT_FALSE(s2textformat::MakeLatLng("blah", &latlng));
}

TEST(SafeMakeLoop, ValidInput) {
  std::unique_ptr<S2Loop> loop;
  EXPECT_TRUE(s2textformat::MakeLoop("-20:150, -20:151, -19:150", &loop));
  EXPECT_TRUE(loop->BoundaryApproxEquals(
      S2Loop({S2LatLng::FromDegrees(-20, 150).ToPoint(),
              S2LatLng::FromDegrees(-20, 151).ToPoint(),
              S2LatLng::FromDegrees(-19, 150).ToPoint()})));
}

TEST(SafeMakeLoop, InvalidInput) {
  std::unique_ptr<S2Loop> loop;
  EXPECT_FALSE(s2textformat::MakeLoop("blah", &loop));
}

TEST(SafeMakePolyline, ValidInput) {
  std::unique_ptr<S2Polyline> polyline;
  EXPECT_TRUE(
      s2textformat::MakePolyline("-20:150, -20:151, -19:150", &polyline));
  S2Polyline expected({S2LatLng::FromDegrees(-20, 150).ToPoint(),
                       S2LatLng::FromDegrees(-20, 151).ToPoint(),
                       S2LatLng::FromDegrees(-19, 150).ToPoint()});
  EXPECT_TRUE(polyline->Equals(&expected));
}

TEST(SafeMakePolyline, InvalidInput) {
  std::unique_ptr<S2Polyline> polyline;
  EXPECT_FALSE(s2textformat::MakePolyline("blah", &polyline));
}

TEST(SafeMakeLaxPolyline, ValidInput) {
  std::unique_ptr<S2LaxPolylineShape> lax_polyline;
  EXPECT_TRUE(s2textformat::MakeLaxPolyline("-20:150, -20:151, -19:150",
                                            &lax_polyline));
  // No easy equality check for LaxPolylines; check vertices instead.
  ASSERT_EQ(3, lax_polyline->num_vertices());
  EXPECT_TRUE(S2LatLng(lax_polyline->vertex(0))
                  .ApproxEquals(S2LatLng::FromDegrees(-20, 150)));
  EXPECT_TRUE(S2LatLng(lax_polyline->vertex(1))
                  .ApproxEquals(S2LatLng::FromDegrees(-20, 151)));
  EXPECT_TRUE(S2LatLng(lax_polyline->vertex(2))
                  .ApproxEquals(S2LatLng::FromDegrees(-19, 150)));
}

TEST(SafeMakeLaxPolyline, InvalidInput) {
  std::unique_ptr<S2LaxPolylineShape> lax_polyline;
  EXPECT_FALSE(s2textformat::MakeLaxPolyline("blah", &lax_polyline));
}

TEST(SafeMakePolygon, ValidInput) {
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_TRUE(s2textformat::MakePolygon("-20:150, -20:151, -19:150", &polygon));
  std::vector<S2Point> vertices({S2LatLng::FromDegrees(-20, 150).ToPoint(),
                                 S2LatLng::FromDegrees(-20, 151).ToPoint(),
                                 S2LatLng::FromDegrees(-19, 150).ToPoint()});
  S2Polygon expected(absl::make_unique<S2Loop>(vertices));
  EXPECT_TRUE(polygon->Equals(&expected));
}

TEST(SafeMakePolygon, InvalidInput) {
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_FALSE(s2textformat::MakePolygon("blah", &polygon));
}

TEST(SafeMakePolygon, Empty) {
  // Verify that "" and "empty" both create empty polygons.
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_TRUE(s2textformat::MakePolygon("", &polygon));
  EXPECT_TRUE(polygon->is_empty());
  EXPECT_TRUE(s2textformat::MakePolygon("empty", &polygon));
  EXPECT_TRUE(polygon->is_empty());
}

TEST(SafeMakePolygon, Full) {
  // Verify that "full" creates the full polygon.
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_TRUE(s2textformat::MakePolygon("full", &polygon));
  EXPECT_TRUE(polygon->is_full());
}

TEST(SafeMakeVerbatimPolygon, ValidInput) {
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_TRUE(
      s2textformat::MakeVerbatimPolygon("-20:150, -20:151, -19:150", &polygon));
  std::vector<S2Point> vertices({S2LatLng::FromDegrees(-20, 150).ToPoint(),
                                 S2LatLng::FromDegrees(-20, 151).ToPoint(),
                                 S2LatLng::FromDegrees(-19, 150).ToPoint()});
  S2Polygon expected(absl::make_unique<S2Loop>(vertices));
  EXPECT_TRUE(polygon->Equals(&expected));
}

TEST(SafeMakeVerbatimPolygon, InvalidInput) {
  std::unique_ptr<S2Polygon> polygon;
  EXPECT_FALSE(s2textformat::MakeVerbatimPolygon("blah", &polygon));
}

TEST(SafeMakeLaxPolygon, ValidInput) {
  std::unique_ptr<S2LaxPolygonShape> lax_polygon;
  EXPECT_TRUE(
      s2textformat::MakeLaxPolygon("-20:150, -20:151, -19:150", &lax_polygon));
  // No easy equality check for LaxPolygons; check vertices instead.
  ASSERT_EQ(1, lax_polygon->num_loops());
  ASSERT_EQ(3, lax_polygon->num_vertices());
  EXPECT_TRUE(S2LatLng(lax_polygon->loop_vertex(0, 0))
                  .ApproxEquals(S2LatLng::FromDegrees(-20, 150)));
  EXPECT_TRUE(S2LatLng(lax_polygon->loop_vertex(0, 1))
                  .ApproxEquals(S2LatLng::FromDegrees(-20, 151)));
  EXPECT_TRUE(S2LatLng(lax_polygon->loop_vertex(0, 2))
                  .ApproxEquals(S2LatLng::FromDegrees(-19, 150)));
}

TEST(SafeMakeLaxPolygon, InvalidInput) {
  std::unique_ptr<S2LaxPolygonShape> lax_polygon;
  EXPECT_FALSE(s2textformat::MakeLaxPolygon("blah", &lax_polygon));
}

TEST(SafeMakeIndex, ValidInput) {
  auto index = absl::make_unique<MutableS2ShapeIndex>();
  EXPECT_TRUE(s2textformat::MakeIndex("# 0:0, 0:0 | 1:0, 2:0 #", &index));
  EXPECT_EQ("# 0:0, 0:0 | 1:0, 2:0 #", s2textformat::ToString(*index));
}

TEST(SafeMakeIndex, InvalidInput) {
  auto index = absl::make_unique<MutableS2ShapeIndex>();
  EXPECT_FALSE(s2textformat::MakeIndex("# blah #", &index));
}

}  // namespace
