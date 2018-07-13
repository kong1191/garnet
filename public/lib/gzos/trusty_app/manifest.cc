// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/gzos/trusty_app/manifest.h"

namespace trusty_app {

constexpr char kManifestPath[] = "/pkg/data/manifest.json";
constexpr char kUuid[] = "uuid";

Manifest* Manifest::Instance() {
  static fbl::Mutex instance_lock;
  static Manifest* instance = nullptr;

  fbl::AutoLock lock(&instance_lock);

  if (!instance) {
    std::string data;
    if (files::ReadFileToString(kManifestPath, &data)) {
      instance = CreateFrom(data);
    }
  }

  return instance;
}

Manifest* Manifest::CreateFrom(std::string data) {
  auto parser = fbl::unique_ptr<Manifest>(new Manifest());
  if (parser) {
    if (parser->Parse(data)) {
      return parser.release();
    }
  }

  return nullptr;
}

bool Manifest::ParseUuid(rapidjson::Document& document) {
  auto it = document.FindMember(kUuid);
  if (it == document.MemberEnd()) {
    return false;
  }

  const auto& value = it->value;
  if (!value.IsString()) {
    return false;
  }

  uuid_ = value.GetString();
  return true;
}

bool Manifest::Parse(const std::string& string) {
  rapidjson::Document document;
  document.Parse(string);

  FXL_CHECK(!document.HasParseError()) << "Failed to parse manifest file";

  if (!(document.IsObject() && ParseUuid(document)))
    return false;

  return true;
}

std::string Manifest::GetUuid() { return uuid_; }

}  // namespace trusty_app
