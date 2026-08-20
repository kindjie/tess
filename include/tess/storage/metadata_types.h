#pragma once

#include <tess/core/fail_fast.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace tess {

/** Application-defined dirty-work categories for one chunk. */
struct DirtyMask {
  std::uint32_t value = 0;

  [[nodiscard]] constexpr bool empty() const noexcept { return value == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !empty();
  }

  friend constexpr bool operator==(DirtyMask lhs,
                                   DirtyMask rhs) noexcept = default;
};

[[nodiscard]] constexpr DirtyMask operator|(DirtyMask lhs,
                                            DirtyMask rhs) noexcept {
  return DirtyMask{lhs.value | rhs.value};
}

[[nodiscard]] constexpr DirtyMask operator&(DirtyMask lhs,
                                            DirtyMask rhs) noexcept {
  return DirtyMask{lhs.value & rhs.value};
}

[[nodiscard]] constexpr DirtyMask operator~(DirtyMask mask) noexcept {
  return DirtyMask{~mask.value};
}

constexpr DirtyMask& operator|=(DirtyMask& lhs, DirtyMask rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

constexpr DirtyMask& operator&=(DirtyMask& lhs, DirtyMask rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

/** Application-defined activity categories for one chunk. */
struct ActiveMask {
  std::uint32_t value = 0;

  [[nodiscard]] constexpr bool empty() const noexcept { return value == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !empty();
  }

  friend constexpr bool operator==(ActiveMask lhs,
                                   ActiveMask rhs) noexcept = default;
};

[[nodiscard]] constexpr ActiveMask operator|(ActiveMask lhs,
                                             ActiveMask rhs) noexcept {
  return ActiveMask{lhs.value | rhs.value};
}

[[nodiscard]] constexpr ActiveMask operator&(ActiveMask lhs,
                                             ActiveMask rhs) noexcept {
  return ActiveMask{lhs.value & rhs.value};
}

[[nodiscard]] constexpr ActiveMask operator~(ActiveMask mask) noexcept {
  return ActiveMask{~mask.value};
}

constexpr ActiveMask& operator|=(ActiveMask& lhs, ActiveMask rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

constexpr ActiveMask& operator&=(ActiveMask& lhs, ActiveMask rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

/** Practically monotonic version of authoritative chunk content. */
struct ContentVersion {
  std::uint64_t value = 0;

  friend constexpr bool operator==(ContentVersion lhs,
                                   ContentVersion rhs) noexcept = default;
};

inline ContentVersion& operator++(ContentVersion& version) noexcept {
  if (version.value == std::numeric_limits<std::uint64_t>::max()) {
    detail::fail_fast("ContentVersion exhausted");
  }
  ++version.value;
  return version;
}

/** Practically monotonic version of topology-relevant chunk state. */
struct TopologyVersion {
  std::uint64_t value = 0;

  friend constexpr bool operator==(TopologyVersion lhs,
                                   TopologyVersion rhs) noexcept = default;
};

inline TopologyVersion& operator++(TopologyVersion& version) noexcept {
  if (version.value == std::numeric_limits<std::uint64_t>::max()) {
    detail::fail_fast("TopologyVersion exhausted");
  }
  ++version.value;
  return version;
}

/** Identifies one sparse chunk residency interval; zero means absent. */
struct ResidencyGeneration {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }

  friend constexpr bool operator==(ResidencyGeneration lhs,
                                   ResidencyGeneration rhs) noexcept = default;
};

inline ResidencyGeneration& operator++(
    ResidencyGeneration& generation) noexcept {
  if (generation.value == std::numeric_limits<std::uint64_t>::max()) {
    detail::fail_fast("ResidencyGeneration exhausted");
  }
  ++generation.value;
  return generation;
}

static_assert(std::is_standard_layout_v<DirtyMask>);
static_assert(std::is_trivially_copyable_v<DirtyMask>);
static_assert(sizeof(DirtyMask) == sizeof(std::uint32_t));
static_assert(alignof(DirtyMask) == alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<ActiveMask>);
static_assert(std::is_trivially_copyable_v<ActiveMask>);
static_assert(sizeof(ActiveMask) == sizeof(std::uint32_t));
static_assert(alignof(ActiveMask) == alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<ContentVersion>);
static_assert(std::is_trivially_copyable_v<ContentVersion>);
static_assert(sizeof(ContentVersion) == sizeof(std::uint64_t));
static_assert(alignof(ContentVersion) == alignof(std::uint64_t));
static_assert(std::is_standard_layout_v<TopologyVersion>);
static_assert(std::is_trivially_copyable_v<TopologyVersion>);
static_assert(sizeof(TopologyVersion) == sizeof(std::uint64_t));
static_assert(alignof(TopologyVersion) == alignof(std::uint64_t));
static_assert(std::is_standard_layout_v<ResidencyGeneration>);
static_assert(std::is_trivially_copyable_v<ResidencyGeneration>);
static_assert(sizeof(ResidencyGeneration) == sizeof(std::uint64_t));
static_assert(alignof(ResidencyGeneration) == alignof(std::uint64_t));

}  // namespace tess
