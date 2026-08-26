#include "congestion_model.h"

#include <tess/experimental/path_agent_replan_selection.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "../web_colony/colony_model_internal.h"

namespace wc = tess::examples::web_colony;

namespace tess::examples::web_congestion {

struct CongestionModel::Impl {
  wc::ColonyModel colony;
  int policy = 0;
  int reprice_period = 4;
  std::uint16_t price_cap = 3;
  std::uint64_t next_reprice = 0;
  long long scoped_replan_total = 0;
  std::vector<std::uint16_t> congestion_heat;
  std::vector<tess::Coord3> reprice_positions;
  std::vector<std::uint8_t> price_view;

  explicit Impl(int agent_count)
      : colony(agent_count),
        congestion_heat(static_cast<std::size_t>(wc::kWidth) * wc::kHeight, 0),
        price_view(static_cast<std::size_t>(wc::kWidth) * wc::kHeight, 1) {}

  // Deduced return: the colony's Impl is a private name, but access
  // control restricts naming, not use -- the friend seam hands back a
  // reference we may deduce.
  [[nodiscard]] auto& seam() {
    return wc::ColonyModelNativeAccess::impl(colony);
  }

  [[nodiscard]] int base_policy() const {
    switch (policy) {
      case 23:
      case 24:
      case 27:
        return 9;  // cool
      case 25:
      case 26:
      case 28:
        return 12;  // stallcool
      default:
        return policy;
    }
  }

  void set_policy(int next) {
    if (next == policy) {
      return;
    }
    policy = next;
    reprice_period = 4;
    price_cap = 3;
    switch (next) {
      case 23:
      case 25:
        reprice_period = 8;
        break;
      case 24:
      case 26:
        reprice_period = 16;
        break;
      case 27:
      case 28:
        price_cap = 7;
        break;
      default:
        break;
    }
    congestion_heat.assign(congestion_heat.size(), 0);
    reprice_positions.clear();
    next_reprice = 0;
    if (next == 0) {
      restore_unit_costs();
    }
  }

  void restore_unit_costs() {
    auto& d = seam();
    std::vector<bool> changed(tess::ShapeTraits<wc::Shape>::chunk_count, false);
    for (int y = 0; y < wc::kHeight; ++y) {
      for (int x = 0; x < wc::kWidth; ++x) {
        const tess::Coord3 c{x, y, 0};
        auto& cost = d.world.field<wc::CostTag>(c);
        price_view[static_cast<std::size_t>(y) * wc::kWidth +
                   static_cast<std::size_t>(x)] = 1;
        if (cost != 1) {
          cost = 1;
          changed[static_cast<std::size_t>(
              tess::chunk_key<wc::Shape>(tess::chunk_coord<wc::Shape>(c))
                  .value)] = true;
        }
      }
    }
    publish_price_marks(changed);
    // Leaving pricing mode is a one-time global event: every retained
    // route was planned against prices, so one full replan is correct
    // here (and only here).
    tess::mark_pathing_dirty(seam().tick_state);
  }

  void publish_price_marks(const std::vector<bool>& changed) {
    auto& d = seam();
    for (std::uint64_t k = 0; k < tess::ShapeTraits<wc::Shape>::chunk_count;
         ++k) {
      if (changed[static_cast<std::size_t>(k)]) {
        d.world.mark_content_changed(tess::ChunkKey{k});
      }
    }
  }

  auto tick(double dt_seconds) -> double {
    // Reprice on fixed-tick boundaries: exact under fixed-step driving
    // (the screens and any future matrix); between-frame under a
    // variable browser clock, which is the documented approximation.
    auto& d = seam();
    if (policy != 0 && d.sim_clock.tick >= next_reprice) {
      apply_pricing();
      next_reprice =
          d.sim_clock.tick + static_cast<std::uint64_t>(reprice_period);
    }
    return colony.tick(dt_seconds);
  }

  // queue2 (amendment 4): stall-gated chain detection with a graded
  // response. A chain qualifies only if at least half its members are
  // stalled, so flowing convoys are left alone; open-geometry chains
  // price their own tiles +3 and each member's passable free lateral
  // neighbours +1 (escape lanes differentiate instead of herding);
  // corridor chains price +3 plus Manhattan-2 end regions +2.
  void apply_queue2_pricing(std::vector<std::uint16_t>& signal,
                            const std::vector<std::uint8_t>& stalled_now) {
    auto& d = seam();
    const auto tiles = static_cast<std::size_t>(wc::kWidth) * wc::kHeight;
    const auto index = [](int x, int y) {
      return static_cast<std::size_t>(y) * wc::kWidth +
             static_cast<std::size_t>(x);
    };
    const auto bump = [&](int x, int y, std::uint16_t amount) {
      if (x < 0 || y < 0 || x >= wc::kWidth || y >= wc::kHeight) {
        return;
      }
      signal[index(x, y)] =
          static_cast<std::uint16_t>(signal[index(x, y)] + amount);
    };
    constexpr int kQueueLength = 4;
    std::vector<std::uint8_t> occupied(tiles, 0);
    std::vector<std::uint8_t> tile_stalled(tiles, 0);
    for (std::size_t i = 0; i < d.agents.size(); ++i) {
      if (!d.agents[i].has_goal) continue;
      const auto at = index(static_cast<int>(d.agents[i].position.x),
                            static_cast<int>(d.agents[i].position.y));
      occupied[at] = 1;
      if (stalled_now[i] != 0) {
        tile_stalled[at] = 1;
      }
    }
    const auto occupied_at = [&](int x, int y) {
      return x >= 0 && y >= 0 && x < wc::kWidth && y < wc::kHeight &&
             occupied[index(x, y)] != 0;
    };
    const auto passable_free = [&](int x, int y) {
      if (x < 0 || y < 0 || x >= wc::kWidth || y >= wc::kHeight) {
        return false;
      }
      return d.world.field<wc::PassableTag>(tess::Coord3{x, y, 0}) &&
             occupied[index(x, y)] == 0;
    };
    std::vector<std::uint8_t> visited(tiles, 0);
    std::vector<std::size_t> component;
    for (int sy = 0; sy < wc::kHeight; ++sy) {
      for (int sx = 0; sx < wc::kWidth; ++sx) {
        if (!occupied_at(sx, sy) || visited[index(sx, sy)] != 0) {
          continue;
        }
        component.clear();
        component.push_back(index(sx, sy));
        visited[index(sx, sy)] = 1;
        bool chain = true;
        for (std::size_t head = 0; head < component.size(); ++head) {
          const auto cx = static_cast<int>(component[head] % wc::kWidth);
          const auto cy = static_cast<int>(component[head] / wc::kWidth);
          int neighbours = 0;
          const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (const auto& st : steps) {
            const auto nx = cx + st[0];
            const auto ny = cy + st[1];
            if (!occupied_at(nx, ny)) continue;
            ++neighbours;
            if (visited[index(nx, ny)] == 0) {
              visited[index(nx, ny)] = 1;
              component.push_back(index(nx, ny));
            }
          }
          if (neighbours > 2) {
            chain = false;
          }
        }
        if (!chain ||
            component.size() <= static_cast<std::size_t>(kQueueLength)) {
          continue;
        }
        // Stall gate: at least half the members must not have moved
        // since the previous repricing, or it is a convoy, not a jam.
        std::size_t stalled_members = 0;
        for (const auto tile : component) {
          stalled_members += tile_stalled[tile];
        }
        if (2 * stalled_members < component.size()) {
          continue;
        }
        std::size_t with_lane = 0;
        for (const auto tile : component) {
          const auto cx = static_cast<int>(tile % wc::kWidth);
          const auto cy = static_cast<int>(tile / wc::kWidth);
          if (passable_free(cx + 1, cy) || passable_free(cx - 1, cy) ||
              passable_free(cx, cy + 1) || passable_free(cx, cy - 1)) {
            ++with_lane;
          }
        }
        const bool open_geometry = 2 * with_lane >= component.size();
        if (open_geometry) {
          for (const auto tile : component) {
            signal[tile] = static_cast<std::uint16_t>(signal[tile] + 3);
            const auto cx = static_cast<int>(tile % wc::kWidth);
            const auto cy = static_cast<int>(tile / wc::kWidth);
            const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& st : steps) {
              if (passable_free(cx + st[0], cy + st[1])) {
                bump(cx + st[0], cy + st[1], 1);
              }
            }
          }
        } else {
          for (const auto tile : component) {
            signal[tile] = static_cast<std::uint16_t>(signal[tile] + 3);
          }
          std::size_t end_a = component[0];
          std::size_t end_b = component[0];
          for (const auto tile : component) {
            const auto cx = static_cast<int>(tile % wc::kWidth);
            const auto cy = static_cast<int>(tile / wc::kWidth);
            int neighbours = 0;
            const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& st : steps) {
              if (occupied_at(cx + st[0], cy + st[1])) ++neighbours;
            }
            if (neighbours <= 1) {
              end_b = end_a;
              end_a = tile;
            }
          }
          for (const auto tile : {end_a, end_b}) {
            const auto cx = static_cast<int>(tile % wc::kWidth);
            const auto cy = static_cast<int>(tile / wc::kWidth);
            for (int dy = -2; dy <= 2; ++dy) {
              for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) + std::abs(dy) <= 2) {
                  bump(cx + dx, cy + dy, 2);
                }
              }
            }
          }
        }
      }
    }
  }

  // The pre-registered accounting policies (issue #269, amendments 1-6).
  // Every policy prices 1 + min(3, signal) over its own signal;
  // deterministic fixed-order scans only.
  void apply_pricing() {
    auto& d = seam();
    const auto tiles = static_cast<std::size_t>(wc::kWidth) * wc::kHeight;
    std::vector<std::uint16_t> signal(tiles, 0);
    const auto index = [](int x, int y) {
      return static_cast<std::size_t>(y) * wc::kWidth +
             static_cast<std::size_t>(x);
    };
    const auto bump = [&](int x, int y, std::uint16_t amount) {
      if (x < 0 || y < 0 || x >= wc::kWidth || y >= wc::kHeight) {
        return;
      }
      signal[index(x, y)] =
          static_cast<std::uint16_t>(signal[index(x, y)] + amount);
    };
    const auto halo1 = [&](int x, int y, std::uint16_t amount) {
      bump(x, y, amount);
      bump(x + 1, y, amount);
      bump(x - 1, y, amount);
      bump(x, y + 1, amount);
      bump(x, y - 1, amount);
    };
    // Peaked kernel (amendment 4): every agent is a small peak, not a
    // plateau -- own tile +2, orthogonal ring +1.
    const auto peak1 = [&](int x, int y) {
      bump(x, y, 2);
      bump(x + 1, y, 1);
      bump(x - 1, y, 1);
      bump(x, y + 1, 1);
      bump(x, y - 1, 1);
    };
    // Stall set for the policies that need it: position unchanged since
    // the previous repricing. Policies 5, 10, 11, and 12 consume it and
    // refresh the reference positions afterwards.
    const bool needs_stall = base_policy() == 5 || base_policy() == 10 ||
                             base_policy() == 11 || base_policy() == 12 ||
                             base_policy() >= 14;
    std::vector<std::uint8_t> stalled_now;
    if (needs_stall) {
      stalled_now.assign(d.agents.size(), 0);
      const bool have_previous = reprice_positions.size() == d.agents.size();
      for (std::size_t i = 0; i < d.agents.size(); ++i) {
        if (d.agents[i].has_goal && have_previous &&
            d.agents[i].position == reprice_positions[i]) {
          stalled_now[i] = 1;
        }
      }
      reprice_positions.assign(d.agents.size(), tess::Coord3{});
      for (std::size_t i = 0; i < d.agents.size(); ++i) {
        reprice_positions[i] = d.agents[i].position;
      }
    }

    switch (base_policy()) {
      case 1: {  // prox1: live agents, Manhattan-1 halo.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          halo1(static_cast<int>(agent.position.x),
                static_cast<int>(agent.position.y), 1);
        }
        break;
      }
      case 2: {  // prox2: Manhattan-2 halo (13 tiles).
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          const auto ax = static_cast<int>(agent.position.x);
          const auto ay = static_cast<int>(agent.position.y);
          for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
              if (std::abs(dx) + std::abs(dy) <= 2) {
                bump(ax + dx, ay + dy, 1);
              }
            }
          }
        }
        break;
      }
      case 3: {  // self: own tile only, rescaled to the shared range.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          bump(static_cast<int>(agent.position.x),
               static_cast<int>(agent.position.y), 3);
        }
        break;
      }
      case 4: {  // decay: prox1 pressure with exponential memory.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          halo1(static_cast<int>(agent.position.x),
                static_cast<int>(agent.position.y), 1);
        }
        for (std::size_t i = 0; i < tiles; ++i) {
          congestion_heat[i] = static_cast<std::uint16_t>(
              (congestion_heat[i] + 1) / 2 + signal[i]);
          signal[i] = congestion_heat[i];
        }
        break;
      }
      case 5: {  // stalled: unchanged position since the last repricing.
        for (std::size_t i = 0; i < d.agents.size(); ++i) {
          if (stalled_now[i] != 0) {
            halo1(static_cast<int>(d.agents[i].position.x),
                  static_cast<int>(d.agents[i].position.y), 1);
          }
        }
        break;
      }
      case 6: {  // demand: the next 8 planned route tiles per live agent.
        for (std::size_t i = 0; i < d.agents.size(); ++i) {
          if (!d.agents[i].has_goal || i >= d.tick_state.routes.routes.size()) {
            continue;
          }
          const auto& route = d.tick_state.routes.routes[i];
          const auto begin = d.agents[i].path_index;
          const auto end = std::min(route.size(), begin + 8);
          for (auto step = begin; step < end; ++step) {
            bump(static_cast<int>(route[step].x),
                 static_cast<int>(route[step].y), 1);
          }
        }
        break;
      }
      case 7: {  // queue: single-file chains longer than k, geometry-aware.
        constexpr int kQueueLength = 4;
        std::vector<std::uint8_t> occupied(tiles, 0);
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          occupied[index(static_cast<int>(agent.position.x),
                         static_cast<int>(agent.position.y))] = 1;
        }
        const auto occupied_at = [&](int x, int y) {
          return x >= 0 && y >= 0 && x < wc::kWidth && y < wc::kHeight &&
                 occupied[index(x, y)] != 0;
        };
        const auto passable_free = [&](int x, int y) {
          if (x < 0 || y < 0 || x >= wc::kWidth || y >= wc::kHeight) {
            return false;
          }
          return d.world.field<wc::PassableTag>(tess::Coord3{x, y, 0}) &&
                 occupied[index(x, y)] == 0;
        };
        std::vector<std::uint8_t> visited(tiles, 0);
        std::vector<std::size_t> component;
        for (int sy = 0; sy < wc::kHeight; ++sy) {
          for (int sx = 0; sx < wc::kWidth; ++sx) {
            if (!occupied_at(sx, sy) || visited[index(sx, sy)] != 0) {
              continue;
            }
            component.clear();
            component.push_back(index(sx, sy));
            visited[index(sx, sy)] = 1;
            bool chain = true;
            for (std::size_t head = 0; head < component.size(); ++head) {
              const auto cx = static_cast<int>(component[head] % wc::kWidth);
              const auto cy = static_cast<int>(component[head] / wc::kWidth);
              int neighbours = 0;
              const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
              for (const auto& st : steps) {
                const auto nx = cx + st[0];
                const auto ny = cy + st[1];
                if (!occupied_at(nx, ny)) continue;
                ++neighbours;
                if (visited[index(nx, ny)] == 0) {
                  visited[index(nx, ny)] = 1;
                  component.push_back(index(nx, ny));
                }
              }
              if (neighbours > 2) {
                chain = false;
              }
            }
            if (!chain ||
                component.size() <= static_cast<std::size_t>(kQueueLength)) {
              continue;
            }
            // Geometry: what fraction of the chain has a free lane beside it?
            std::size_t with_lane = 0;
            for (const auto tile : component) {
              const auto cx = static_cast<int>(tile % wc::kWidth);
              const auto cy = static_cast<int>(tile / wc::kWidth);
              if (passable_free(cx + 1, cy) || passable_free(cx - 1, cy) ||
                  passable_free(cx, cy + 1) || passable_free(cx, cy - 1)) {
                ++with_lane;
              }
            }
            const bool open_geometry = 2 * with_lane >= component.size();
            if (open_geometry) {
              // Parallel capacity exists: price the line itself so routes
              // prefer the lanes beside it.
              for (const auto tile : component) {
                signal[tile] = static_cast<std::uint16_t>(signal[tile] + 2);
              }
            } else {
              // Walled corridor: price the line and a Manhattan-2 region
              // around both chain ends so newcomers detour before entering.
              for (const auto tile : component) {
                signal[tile] = static_cast<std::uint16_t>(signal[tile] + 3);
              }
              std::size_t end_a = component[0];
              std::size_t end_b = component[0];
              for (const auto tile : component) {
                const auto cx = static_cast<int>(tile % wc::kWidth);
                const auto cy = static_cast<int>(tile / wc::kWidth);
                int neighbours = 0;
                const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& st : steps) {
                  if (occupied_at(cx + st[0], cy + st[1])) ++neighbours;
                }
                if (neighbours <= 1) {
                  end_b = end_a;
                  end_a = tile;
                }
              }
              for (const auto tile : {end_a, end_b}) {
                const auto cx = static_cast<int>(tile % wc::kWidth);
                const auto cy = static_cast<int>(tile / wc::kWidth);
                for (int dy = -2; dy <= 2; ++dy) {
                  for (int dx = -2; dx <= 2; ++dx) {
                    if (std::abs(dx) + std::abs(dy) <= 2) {
                      bump(cx + dx, cy + dy, 3);
                    }
                  }
                }
              }
            }
          }
        }
        break;
      }
      case 8: {  // peak1: weighted kernel over all live agents.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          peak1(static_cast<int>(agent.position.x),
                static_cast<int>(agent.position.y));
        }
        break;
      }
      case 9: {  // cool: true-cooling memory over the flat prox1 halo.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          halo1(static_cast<int>(agent.position.x),
                static_cast<int>(agent.position.y), 1);
        }
        for (std::size_t i = 0; i < tiles; ++i) {
          congestion_heat[i] =
              static_cast<std::uint16_t>(congestion_heat[i] / 2 + signal[i]);
          signal[i] = congestion_heat[i];
        }
        break;
      }
      case 10: {  // queue2: stall-gated chains, graded response.
        apply_queue2_pricing(signal, stalled_now);
        break;
      }
      case 11: {  // stallpeak: stalled agents, peaked kernel.
        for (std::size_t i = 0; i < d.agents.size(); ++i) {
          if (stalled_now[i] != 0) {
            peak1(static_cast<int>(d.agents[i].position.x),
                  static_cast<int>(d.agents[i].position.y));
          }
        }
        break;
      }
      case 12: {  // stallcool: true-cooling memory of stalled-agent halos.
        for (std::size_t i = 0; i < d.agents.size(); ++i) {
          if (stalled_now[i] != 0) {
            halo1(static_cast<int>(d.agents[i].position.x),
                  static_cast<int>(d.agents[i].position.y), 1);
          }
        }
        for (std::size_t i = 0; i < tiles; ++i) {
          congestion_heat[i] =
              static_cast<std::uint16_t>(congestion_heat[i] / 2 + signal[i]);
          signal[i] = congestion_heat[i];
        }
        break;
      }
      case 13: {  // peakcool: true-cooling memory of peaked-kernel pressure.
        for (const auto& agent : d.agents) {
          if (!agent.has_goal) continue;
          peak1(static_cast<int>(agent.position.x),
                static_cast<int>(agent.position.y));
        }
        for (std::size_t i = 0; i < tiles; ++i) {
          congestion_heat[i] =
              static_cast<std::uint16_t>(congestion_heat[i] / 2 + signal[i]);
          signal[i] = congestion_heat[i];
        }
        break;
      }
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
      case 20:
      case 21:
      case 22: {
        // Amendment-5 factorial: signal source x kernel x memory, with
        // an optional queue overlay added before the shared cap.
        // 14 stallpeakcool; 15 prox1q; 16 peak1q; 17 coolq;
        // 18 peakcoolq; 19 stalledq; 20 stallpeakq; 21 stallcoolq;
        // 22 stallpeakcoolq.
        struct Axes {
          bool stall_source;
          bool peaked;
          bool cooling;
          bool overlay;
        };
        static constexpr Axes kAxes[] = {
            {true, true, true, false},    // 14
            {false, false, false, true},  // 15
            {false, true, false, true},   // 16
            {false, false, true, true},   // 17
            {false, true, true, true},    // 18
            {true, false, false, true},   // 19
            {true, true, false, true},    // 20
            {true, false, true, true},    // 21
            {true, true, true, true},     // 22
        };
        const auto axes = kAxes[static_cast<std::size_t>(policy - 14)];
        for (std::size_t i = 0; i < d.agents.size(); ++i) {
          if (!d.agents[i].has_goal) continue;
          if (axes.stall_source && stalled_now[i] == 0) continue;
          const auto ax = static_cast<int>(d.agents[i].position.x);
          const auto ay = static_cast<int>(d.agents[i].position.y);
          if (axes.peaked) {
            peak1(ax, ay);
          } else {
            halo1(ax, ay, 1);
          }
        }
        if (axes.cooling) {
          for (std::size_t i = 0; i < tiles; ++i) {
            congestion_heat[i] =
                static_cast<std::uint16_t>(congestion_heat[i] / 2 + signal[i]);
            signal[i] = congestion_heat[i];
          }
        }
        if (axes.overlay) {
          apply_queue2_pricing(signal, stalled_now);
        }
        break;
      }
      default:
        return;
    }

    std::vector<bool> changed(tess::ShapeTraits<wc::Shape>::chunk_count, false);
    std::vector<std::uint8_t> increased(tiles, 0);
    bool any_increase = false;
    for (int y = 0; y < wc::kHeight; ++y) {
      for (int x = 0; x < wc::kWidth; ++x) {
        const tess::Coord3 c{x, y, 0};
        const auto capped =
            signal[index(x, y)] > price_cap ? price_cap : signal[index(x, y)];
        const auto price = static_cast<std::uint32_t>(1 + capped);
        auto& cost = d.world.field<wc::CostTag>(c);
        price_view[index(x, y)] = static_cast<std::uint8_t>(price);
        if (cost != price) {
          if (price > cost) {
            increased[index(x, y)] = 1;
            any_increase = true;
          }
          cost = price;
          changed[static_cast<std::size_t>(
              tess::chunk_key<wc::Shape>(tess::chunk_coord<wc::Shape>(c))
                  .value)] = true;
        }
      }
    }
    publish_price_marks(changed);
    // Scoped replanning (issue #269 amendment 3): a price change never
    // invalidates a retained route -- cost affects optimality, not
    // validity -- so only agents whose remaining route crosses a tile
    // whose price INCREASED this repricing are asked to replan. Price
    // decreases elsewhere deliberately trigger nothing: chasing newly
    // cheap tiles is the far-field oscillation this scoping removes.
    if (!any_increase) {
      return;
    }
    scoped_replan_total += static_cast<long long>(
        tess::experimental::request_replans_for_route_crossings(
            d.agents, d.tick_state.routes,
            [&](tess::Coord3 c) {
              return increased[index(static_cast<int>(c.x),
                                     static_cast<int>(c.y))] != 0;
            },
            d.replan_queue));
  }
};

CongestionModel::CongestionModel(int agent_count)
    : impl_(std::make_unique<Impl>(agent_count)) {}
CongestionModel::~CongestionModel() = default;

auto CongestionModel::queue_wall(int x, int y) -> bool {
  return impl_->colony.queue_wall(x, y);
}
auto CongestionModel::set_wall(int x, int y, bool built) -> bool {
  return impl_->colony.set_wall(x, y, built);
}
auto CongestionModel::aborted_legs() const noexcept -> int {
  return impl_->colony.aborted_legs();
}
auto CongestionModel::stalled_ticks() const noexcept -> int {
  return impl_->colony.stalled_ticks();
}
void CongestionModel::set_spread_congested_routes(bool enabled) noexcept {
  impl_->colony.set_spread_congested_routes(enabled);
}
void CongestionModel::set_replan_each_tick(bool enabled) noexcept {
  impl_->colony.set_replan_each_tick(enabled);
}
void CongestionModel::set_pricing_policy(int policy) {
  impl_->set_policy(policy);
}
auto CongestionModel::pricing_policy() const noexcept -> int {
  return impl_->policy;
}
auto CongestionModel::tick(double dt_seconds) -> double {
  return impl_->tick(dt_seconds);
}
auto CongestionModel::relaunch() -> int { return impl_->colony.relaunch(); }
auto CongestionModel::leg() const noexcept -> int {
  return impl_->colony.leg();
}
auto CongestionModel::completed_legs() const noexcept -> int {
  return impl_->colony.completed_legs();
}
auto CongestionModel::agent_count() const noexcept -> int {
  return impl_->colony.agent_count();
}
auto CongestionModel::arrived() const -> int { return impl_->colony.arrived(); }
auto CongestionModel::unreachable() const -> int {
  return impl_->colony.unreachable();
}
auto CongestionModel::crowd_blocked() const -> int {
  return impl_->colony.crowd_blocked();
}
auto CongestionModel::turnaround_ready() const noexcept -> bool {
  return impl_->colony.turnaround_ready();
}
auto CongestionModel::planning_pending() const noexcept -> int {
  return impl_->colony.planning_pending();
}
auto CongestionModel::advanced_last_tick() const noexcept -> int {
  return impl_->colony.advanced_last_tick();
}
auto CongestionModel::movement_waits_last_tick() const noexcept -> int {
  return impl_->colony.movement_waits_last_tick();
}
auto CongestionModel::scoped_replans() const noexcept -> long long {
  return impl_->scoped_replan_total;
}
auto CongestionModel::tiles() const noexcept -> const std::uint8_t* {
  return impl_->colony.tiles();
}
auto CongestionModel::current_agents() const noexcept -> const std::int16_t* {
  return impl_->colony.current_agents();
}
auto CongestionModel::previous_agents() const noexcept -> const std::int16_t* {
  return impl_->colony.previous_agents();
}
auto CongestionModel::interpolation_alpha() const noexcept -> double {
  return impl_->colony.interpolation_alpha();
}
auto CongestionModel::prices() const noexcept -> const std::uint8_t* {
  return impl_->price_view.data();
}

}  // namespace tess::examples::web_congestion
