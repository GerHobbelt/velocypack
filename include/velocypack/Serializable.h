////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Lars Maier
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

#include "velocypack/velocypack-common.h"

namespace arangodb::velocypack {
class Builder;

class Serializable {
 public:
  virtual void toVelocyPack(Builder&) const = 0;

  // convenience method
  std::shared_ptr<Builder> toVelocyPack() const;

  virtual ~Serializable() = default;
};

struct Serialize {
  Serialize(Serializable const& sable) : _sable(sable) {}
  Serializable const& _sable;
};

}  // namespace arangodb::velocypack

using VPackSerializable = arangodb::velocypack::Serializable;
using VPackSerialize = arangodb::velocypack::Serialize;
