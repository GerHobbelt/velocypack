////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2022 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Manuel Pöter
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "velocypack/Builder.h"
#include "velocypack/Inspect/Inspector.h"
#include "velocypack/Inspect/InspectorAccess.h"
#include "velocypack/Iterator.h"
#include "velocypack/Slice.h"
#include "velocypack/Value.h"

namespace arangodb::velocypack {

struct LoadInspector {
  static constexpr bool isLoading = true;

  explicit LoadInspector(Builder& builder) : _slice(builder.slice()) {}
  explicit LoadInspector(Slice slice) : _slice(slice) {}

  template<class T>
  [[nodiscard]] inspection::Result apply(T& x) {
    return inspection::load(*this, x);
  }

  template<class T>
  requires std::is_integral_v<T>
  [[nodiscard]] inspection::Result value(T& v) {
    try {
      v = _slice.getNumber<T>();
      return {};
    } catch (Exception& e) {
      return {e.what()};
    }
  }

  [[nodiscard]] inspection::Result value(double& v) {
    try {
      v = _slice.getNumber<double>();
      return {};
    } catch (Exception& e) {
      return {e.what()};
    }
  }

  [[nodiscard]] inspection::Result value(std::string& v) {
    if (!_slice.isString()) {
      return {"Expecting type String"};
    }
    v = _slice.copyString();
    return {};
  }

  [[nodiscard]] inspection::Result value(bool& v) {
    if (!_slice.isBool()) {
      return {"Expecting type Bool"};
    }
    v = _slice.isTrue();
    return {};
  }

  [[nodiscard]] inspection::Result beginObject() {
    if (!_slice.isObject()) {
      return {"Expecting type Object"};
    }
    return {};
  }

  [[nodiscard]] inspection::Result endObject() { return {}; }

  [[nodiscard]] inspection::Result beginArray() {
    if (!_slice.isArray()) {
      return {"Expecting type Array"};
    }
    return {};
  }

  [[nodiscard]] inspection::Result endArray() { return {}; }

  template<class T>
  [[nodiscard]] inspection::Result list(T& list) {
    if (auto res = beginArray(); !res.ok()) {
      return res;
    }
    for (auto&& s : VPackArrayIterator(_slice)) {
      LoadInspector ff(s);
      typename T::value_type val;
      if (auto res = inspection::load(ff, val); !res.ok()) {
        return res;
      }
      list.push_back(std::move(val));
    }
    return endArray();
  }

  template<class T>
  [[nodiscard]] inspection::Result map(T& map) {
    if (auto res = beginObject(); !res.ok()) {
      return res;
    }
    for (auto&& pair : VPackObjectIterator(_slice)) {
      LoadInspector ff(pair.value);
      typename T::mapped_type val;
      if (auto res = inspection::load(ff, val); !res.ok()) {
        return res;
      }
      map.emplace(pair.key.copyString(), std::move(val));
    }
    return endObject();
  }

  template<class T>
  [[nodiscard]] inspection::Result tuple(T& data) {
    if (auto res = beginArray(); !res.ok()) {
      return res;
    }

    if (auto res = processTuple<0, std::tuple_size_v<T>>(data); !res.ok()) {
      return res;
    }

    return endArray();
  }

  template<std::size_t Idx, std::size_t End, class T>
  [[nodiscard]] inspection::Result processTuple(T& data) {
    if constexpr (Idx < End) {
      LoadInspector ff{_slice[Idx]};
      if (auto res = inspection::load(ff, std::get<Idx>(data)); !res.ok()) {
        return res;
      }
      return processTuple<Idx + 1, End>(data);
    } else {
      return {};
    }
  }

  template<class T, size_t N>
  [[nodiscard]] inspection::Result tuple(T (&data)[N]) {
    if (auto res = beginArray(); !res.ok()) {
      return res;
    }
    assert(_slice.length() == N);
    std::size_t index = 0;
    for (auto&& v : VPackArrayIterator(_slice)) {
      LoadInspector ff(v);
      if (auto res = inspection::load(ff, data[index]); !res.ok()) {
        return res;
      }
      ++index;
    }
    return endArray();
  }

  struct Object {
    template<class... Args>
    [[nodiscard]] inspection::Result fields(Args... args) {
      if (auto res = inspector.beginObject(); !res.ok()) {
        return res;
      }

      if (auto res = applyFields(std::forward<Args>(args)...); !res.ok()) {
        return res;
      }

      return inspector.endObject();
    }

   private:
    friend struct LoadInspector;
    explicit Object(LoadInspector& inspector) : inspector(inspector) {}

    template<class Arg>
    inspection::Result applyFields(Arg arg) {
      return arg(inspector);
    }

    template<class Arg, class... Args>
    inspection::Result applyFields(Arg arg, Args... args) {
      if (auto res = arg(inspector); !res.ok()) {
        return res;
      }
      return applyFields(std::forward<Args>(args)...);
    }

    LoadInspector& inspector;
  };

  template<typename Derived>
  struct Field {
    std::string_view name;

    template<class Predicate>
    [[nodiscard]] auto invariant(Predicate predicate) &&;

    template<class U>
    [[nodiscard]] auto fallback(U val) &&;
  };

  template<typename T>
  struct RawField : Field<RawField<T>> {
    using value_type = T;
    T* value;
    [[nodiscard]] inspection::Result operator()(LoadInspector& f) {
      auto res = inspection::loadField(f, this->name, *value);
      if (!res.ok()) {
        return {std::move(res), this->name};
      }
      return res;
    }
  };

  template<typename T>
  struct VirtualField : Field<VirtualField<T>> {
    using value_type = T;
  };

  template<class Field>
  struct Invariant {};

  [[nodiscard]] Object object() noexcept { return Object{*this}; }

  template<typename T>
  [[nodiscard]] RawField<T> field(std::string_view name,
                                  T& value) const noexcept {
    static_assert(!std::is_const<T>::value);
    return RawField<T>{{name}, std::addressof(value)};
  }

  Slice _slice;
};

}  // namespace arangodb::velocypack
