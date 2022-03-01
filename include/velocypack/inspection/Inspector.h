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

#include <type_traits>
#include <variant>
#include "velocypack/inspection/InspectorAccess.h"

namespace arangodb::velocypack::inspection {

template<class Derived>
struct InspectorBase {
  Derived& self() { return static_cast<Derived&>(*this); }

  template<class T>
  [[nodiscard]] Result apply(T& x) {
    return process(self(), x);
  }

  struct Object {
    template<class... Args>
    [[nodiscard]] Result fields(Args... args) {
      if (auto res = inspector.beginObject(); !res.ok()) {
        return res;
      }

      if (auto res = inspector.applyFields(std::forward<Args>(args)...);
          !res.ok()) {
        return res;
      }

      return inspector.endObject();
    }

   private:
    friend struct InspectorBase;
    explicit Object(Derived& inspector) : inspector(inspector) {}

    Derived& inspector;
  };

  template<class Field>
  struct InvariantMixin {
    template<class Predicate>
    [[nodiscard]] auto invariant(Predicate predicate) && {
      return InvariantField<Field, Predicate>(
          std::move(static_cast<Field&>(*this)), std::move(predicate));
    }
  };

  template<class Field>
  struct FallbackMixin {
    template<class U>
    [[nodiscard]] auto fallback(U val) && {
      static_assert(std::is_constructible_v<typename Field::value_type, U>);

      return FallbackField<Field, U>(std::move(static_cast<Field&>(*this)),
                                     std::forward<U>(val));
    }
  };

  template<class Field>
  static constexpr bool HasInvariantMethod = requires(Field f) {
    f.invariant([](auto) { return true; });
  };

  template<class Inner>
  using WithInvariant =
      std::conditional_t<HasInvariantMethod<Inner>, std::monostate,
                         InvariantMixin<Inner>>;

  template<class Field>
  static constexpr bool HasFallbackMethod = requires(Field f) {
    f.fallback(std::declval<typename Field::value_type>());
  };

  template<class Inner>
  using WithFallback = std::conditional_t<HasFallbackMethod<Inner>,
                                          std::monostate, FallbackMixin<Inner>>;

  template<class InnerField, class Predicate>
  struct InvariantField : Derived::template PredicateContainer<Predicate>,
                          WithFallback<InvariantField<InnerField, Predicate>> {
    InvariantField(InnerField inner, Predicate&& predicate)
        : Derived::template PredicateContainer<Predicate>(std::move(predicate)),
          inner(std::move(inner)) {}
    using value_type = typename InnerField::value_type;
    InnerField inner;
  };

  template<class InnerField, class U>
  struct FallbackField : Derived::template FallbackContainer<U>,
                         WithInvariant<FallbackField<InnerField, U>> {
    FallbackField(InnerField inner, U&& val)
        : Derived::template FallbackContainer<U>(std::move(val)),
          inner(std::move(inner)) {}
    using value_type = typename InnerField::value_type;
    InnerField inner;
  };

  template<typename DerivedField>
  struct BasicField : InvariantMixin<DerivedField>,
                      FallbackMixin<DerivedField> {
    explicit BasicField(std::string_view name) : name(name) {}
    std::string_view name;
  };

  template<typename T>
  struct RawField : BasicField<RawField<T>> {
    RawField(std::string_view name, T& value)
        : BasicField<RawField>(name), value(value) {}
    using value_type = T;
    T& value;
  };

  template<typename T>
  struct VirtualField : BasicField<VirtualField<T>> {
    using value_type = T;
  };

  template<class Field>
  struct Invariant {};

  [[nodiscard]] Object object() noexcept { return Object{self()}; }

  template<typename T>
  [[nodiscard]] RawField<T> field(std::string_view name,
                                  T& value) const noexcept {
    static_assert(!std::is_const<T>::value);
    return RawField<T>{{name}, value};
  }

  template<class T>
  static std::string_view getFieldName(T& field) noexcept {
    if constexpr (requires() { field.inner; }) {
      return getFieldName(field.inner);
    } else {
      return field.name;
    }
  }

  template<class T>
  static auto& getFieldValue(T& field) noexcept {
    if constexpr (requires() { field.inner; }) {
      return getFieldValue(field.inner);
    } else {
      return field.value;
    }
  }

  template<class T>
  static decltype(auto) getFallbackValue(T& field) noexcept {
    if constexpr (requires() { field.fallbackValue; }) {
      return (field.fallbackValue);  // intentially use braces so the return
                                     // value is an l-value ref
    } else if constexpr (requires() { field.inner; }) {
      return getFallbackValue(field.inner);
    }
  }

 private:
  template<class Arg>
  Result applyFields(Arg arg) {
    return self().applyField(arg);
  }

  template<class Arg, class... Args>
  Result applyFields(Arg arg, Args... args) {
    if (auto res = self().applyField(arg); !res.ok()) {
      return res;
    }
    return applyFields(std::forward<Args>(args)...);
  }
};

}  // namespace arangodb::velocypack::inspection
