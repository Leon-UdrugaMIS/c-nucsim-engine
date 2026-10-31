#pragma once

#include <algorithm>
#include <string>

namespace rbwr {

enum class Health { ok, degraded, failed, tripped, isolated, offline };
enum class ValveState { closed, opening, open, closing, stuck, faulted };
enum class PumpState { stopped, starting, running, coasting, tripped, failed };
enum class BreakerState { open, closing, closed, opening, tripped };

struct Component {
  std::string id;
  std::string label;
  Health health = Health::ok;
};

struct Valve : Component {
  ValveState state = ValveState::closed;
  double position_pct = 0.0;
  double target_pct = 0.0;
  double stroke_pct_per_s = 10.0;

  void command(double target) {
    if (health == Health::failed || health == Health::offline) return;
    target_pct = std::clamp(target, 0.0, 100.0);
  }

  void update(double dt) {
    if (health == Health::failed || health == Health::offline) {
      state = ValveState::faulted;
      return;
    }
    const double delta = stroke_pct_per_s * std::max(dt, 0.0);
    if (position_pct < target_pct) {
      position_pct = std::min(position_pct + delta, target_pct);
      state = position_pct == target_pct ? ValveState::open : ValveState::opening;
    } else if (position_pct > target_pct) {
      position_pct = std::max(position_pct - delta, target_pct);
      state = position_pct == target_pct ? ValveState::closed : ValveState::closing;
    } else {
      state = position_pct == 0.0 ? ValveState::closed : ValveState::open;
    }
  }
};

struct Pump : Component {
  PumpState state = PumpState::stopped;
  double speed_pct = 0.0;
  double target_speed_pct = 0.0;
  double ramp_pct_per_s = 12.5;
  double flow_kgs = 0.0;
  double rated_flow_kgs = 100.0;
  double power_mw = 0.0;

  void command(bool running) { target_speed_pct = running ? 100.0 : 0.0; }

  void update(double dt, bool supply_energized) {
    if (!supply_energized) target_speed_pct = 0.0;
    const double delta = ramp_pct_per_s * std::max(dt, 0.0);
    if (speed_pct < target_speed_pct) speed_pct = std::min(speed_pct + delta, target_speed_pct);
    if (speed_pct > target_speed_pct) speed_pct = std::max(speed_pct - delta, target_speed_pct);
    state = speed_pct == 0.0 ? PumpState::stopped
                             : speed_pct < 100.0 ? PumpState::starting : PumpState::running;
    flow_kgs = rated_flow_kgs * speed_pct / 100.0;
    power_mw = 0.01 * speed_pct * speed_pct * speed_pct / 10000.0;
  }
};

struct Breaker : Component {
  BreakerState state = BreakerState::open;
  bool command_closed = false;

  void command(bool closed, bool permissive) {
    if (!permissive) {
      state = BreakerState::tripped;
      command_closed = false;
      return;
    }
    command_closed = closed;
    state = closed ? BreakerState::closed : BreakerState::open;
  }
};

struct Bus : Component {
  bool energized = false;
  double voltage = 0.0;
  double nominal_voltage = 0.0;

  void solve(bool source_available, bool breaker_closed) {
    energized = source_available && breaker_closed && health != Health::failed;
    voltage = energized ? nominal_voltage : 0.0;
  }
};

struct Sensor : Component {
  double value = 0.0;
  double lag_s = 0.0;
  double range_min = 0.0;
  double range_max = 1.0;
  std::string quality = "GOOD";

  void update(double truth, double dt) {
    if (health == Health::failed) {
      quality = "BAD";
      return;
    }
    const double alpha = lag_s <= 0.0 ? 1.0 : std::clamp(dt / lag_s, 0.0, 1.0);
    value += (truth - value) * alpha;
    value = std::clamp(value, range_min, range_max);
    quality = "GOOD";
  }
};

struct Annunciator {
  std::string id;
  bool triggered = false;
  bool acknowledged = false;
  bool silenced = false;

  void evaluate(bool condition) {
    if (condition && !triggered) {
      acknowledged = false;
      silenced = false;
    }
    triggered = condition;
  }
};

}  // namespace rbwr
