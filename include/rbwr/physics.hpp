#pragma once

#include <array>

namespace rbwr::physics {

struct SteamState {
  double pressure_kpa = 101.325;
  double temperature_c = 100.0;
  double enthalpy_kjkg = 2675.5;
  double specific_volume_m3kg = 1.694;
  double quality = 1.0;
};

struct PumpResult {
  double flow_kgs = 0.0;
  double head_kpa = 0.0;
  double power_mw = 0.0;
  bool cavitating = false;
};

class PumpModel {
 public:
  PumpModel(double rated_flow_kgs, double rated_head_kpa, double rated_power_mw,
            double npsh_required_kpa = 30.0);
  [[nodiscard]] PumpResult solve(double speed_pct, double system_head_kpa,
                                 double available_npsh_kpa) const;

 private:
  double rated_flow_kgs_;
  double rated_head_kpa_;
  double rated_power_mw_;
  double npsh_required_kpa_;
};

class ValveModel {
 public:
  ValveModel(double cv_max, double stroke_seconds = 10.0);
  [[nodiscard]] double flow_kgs(double opening_pct, double upstream_kpa,
                                double downstream_kpa, double density_kgm3 = 1000.0) const;
  [[nodiscard]] double stroke_rate_pct_per_s() const;

 private:
  double cv_max_;
  double stroke_seconds_;
};

// Reduced-order property correlations used when a full IF97 table is unavailable.
[[nodiscard]] double saturation_pressure_kpa(double temperature_c);
[[nodiscard]] double saturation_temperature_c(double pressure_kpa);
[[nodiscard]] SteamState saturated_vapor(double pressure_kpa);

class PointKinetics {
 public:
  PointKinetics();
  void reset(double normalized_power = 0.0);
  void step(double dt_seconds, double reactivity);
  [[nodiscard]] double normalized_power() const;
  [[nodiscard]] double period_seconds() const;

 private:
  static constexpr std::array<double, 6> beta_{{0.00025, 0.00125, 0.00120,
                                                0.00250, 0.00075, 0.00025}};
  static constexpr std::array<double, 6> lambda_{{0.0124, 0.0305, 0.111,
                                                   0.301, 1.14, 3.01}};
  double power_ = 0.0;
  std::array<double, 6> precursor_{};
  double previous_power_ = 0.0;
  double period_seconds_ = 1000.0;
};

}  // namespace rbwr::physics
