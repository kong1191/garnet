// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_SHAPE_CACHE_H_
#define LIB_ESCHER_PAPER_PAPER_SHAPE_CACHE_H_

#include <functional>
#include <vector>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/util/hash_map.h"

namespace escher {

class BoundingBox;
struct RoundedRectSpec;

// Generates and caches clipped triangle meshes that match the requested shape
// specification.
class PaperShapeCache {
 public:
  static constexpr size_t kNumFramesBeforeEviction = 3;

  explicit PaperShapeCache(EscherWeakPtr escher);
  ~PaperShapeCache();

  // Return a (possibly cached) mesh that matches the spec, clipped to the list
  // of clip planes.
  Mesh* GetRoundedRectMesh(const RoundedRectSpec& spec,
                           const plane2* clip_planes, size_t num_clip_planes);
  Mesh* GetRoundedRectMesh(const RoundedRectSpec& spec,
                           const std::vector<plane2>& clip_planes);

  void BeginFrame(BatchGpuUploader* uploader, uint64_t frame_number);
  void EndFrame();

 private:
  // Args: array of planes to clip the generated mesh, and size of the array.
  using CacheMissMeshGenerator = std::function<MeshPtr(const plane2*, size_t)>;

  // Computes a lookup key by starting with |shape_hash| and then hashing the
  // list of |clip_planes|.  If no mesh is found with this key, a secondary key
  // is generated similarly, this time after culling the planes against
  // |bounding_box|.  If no mesh is found with the second key, a new mesh is
  // generated by invoking |mesh_generator|; this mesh is then cached using both
  // lookup keys, based on the following rationale:
  // - the mesh is cached with the first key to maximize performance in the
  //   case that it is looked up again with the exact same set of parameters.
  // - the mesh is cached with the second key because it will be common for the
  //   culled set of planes to be the same even though the original set isn't.
  //   For example, when an object is moving freely within a large clip region,
  //   the list of unculled planes will be empty; it would be a shame to
  //   continually regenerate the mesh in such a situation.
  Mesh* GetShapeMesh(const Hash& shape_hash, const BoundingBox& bounding_box,
                     const plane2* clip_planes, size_t num_clip_planes,
                     CacheMissMeshGenerator mesh_generator);

  // Populates |unculled_planes_out| with the planes that clip at least one of
  // the bounding box corners; other planes are culled, because they cannot
  // possibly intersect anything within the box.  |num_planes_inout| must
  // initially contain the number of planes in |planes|; when the function
  // returns it will contain the number of planes in |unculled_planes_out|.
  //
  // Returns true if any of the planes clips the entire bounding box, otherwise
  // return false.  If such a plane is encountered, iteration halts immediately
  // since there would be nothing left within the bounding box for subsequent
  // planes to clip (this final plane does appear in |unculled_planes_out|).
  //
  // Preserves the order of any unculled planes.
  bool CullPlanesAgainstBoundingBox(const BoundingBox& bounding_box,
                                    const plane2* planes,
                                    plane2* unculled_planes_out,
                                    size_t* num_planes_inout);

  // Called from EndFrame(); evicts all entries that have not been touched for
  // kNumFramesBeforeEviction.
  void TrimCache();

  struct CacheEntry {
    uint64_t last_touched_frame;
    MeshPtr mesh;
  };

  // Return the CacheEntry corresponding to the hash, or nullptr if none such
  // is present in the cache.
  CacheEntry* FindEntry(const Hash& hash);

  // Entry must not already exist.
  void AddEntry(const Hash& hash, MeshPtr mesh);

  const EscherWeakPtr escher_;
  HashMap<Hash, CacheEntry> cache_;
  BatchGpuUploader* uploader_ = nullptr;
  uint64_t frame_number_ = 0;

  // Reset every frame.
  uint64_t cache_hit_count_ = 0;
  uint64_t cache_hit_after_plane_culling_count_ = 0;
  uint64_t cache_miss_count_ = 0;
};

// Inline function definitions.

inline Mesh* PaperShapeCache::GetRoundedRectMesh(
    const RoundedRectSpec& spec, const std::vector<plane2>& clip_planes) {
  return GetRoundedRectMesh(spec, clip_planes.data(), clip_planes.size());
}

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_SHAPE_CACHE_H_
