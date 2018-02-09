// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/camera.h"
#include "garnet/lib/ui/scenic/util/unwrap.h"
#include "garnet/public/lib/escher/util/type_utils.h"

namespace scene_manager {

const ResourceTypeInfo Camera::kTypeInfo = {ResourceType::kCamera, "Camera"};

Camera::Camera(Session* session, scenic::ResourceId id, ScenePtr scene)
    : Resource(session, id, Camera::kTypeInfo), scene_(std::move(scene)) {}

void Camera::SetProjection(const glm::vec3& eye_position,
                           const glm::vec3& eye_look_at,
                           const glm::vec3& eye_up,
                           float fovy) {
  eye_position_ = eye_position;
  eye_look_at_ = eye_look_at;
  eye_up_ = eye_up;
  fovy_ = fovy;
}

escher::Camera Camera::GetEscherCamera(
    const escher::ViewingVolume& volume) const {
  if (fovy_ == 0.f) {
    return escher::Camera::NewOrtho(volume);
  } else {
    return escher::Camera::NewPerspective(
        volume, glm::lookAt(eye_position_, eye_look_at_, eye_up_), fovy_);
  }
}

std::pair<escher::ray4, escher::mat4> Camera::ProjectRayIntoScene(
    const escher::ray4& ray,
    const escher::ViewingVolume& viewing_volume) const {
  auto camera = GetEscherCamera(viewing_volume);

  // This screen transform shifts the x, y from [-1, 1] to [0, 1]. Therefore,
  // when we invert it below, it takes the input coords into Vulkan normalized
  // device coordinates.
  auto scale = glm::scale(glm::vec3(0.5f, 0.5f, 1.f));
  auto translate = glm::translate(glm::vec3(1.f, 1.f, 0.f));
  auto device_transform = scale * translate;

  // This operation can be thought of as constructing the ray originating at the
  // camera's position, that passes through a point on the near plane.
  //
  // First the constructed near/mid points get passed through the inverse of the
  // device transform. After that the projection gets "undone," and finally the
  // camera transform is taken into account.
  //
  // For more information about world, view, and projection matrices, and how
  // they can be used for ray picking, see:
  // http://www.codinglabs.net/article_world_view_projection_matrix.aspx
  // https://stackoverflow.com/questions/2093096/implementing-ray-picking
  auto inverse_vp =
      glm::inverse(device_transform * camera.projection() * camera.transform());
  auto inverse_scene_transform =
      glm::inverse(static_cast<escher::mat4>(scene()->transform()));

  return {inverse_scene_transform * inverse_vp * ray,
          inverse_scene_transform * inverse_vp};
}

}  // namespace scene_manager
