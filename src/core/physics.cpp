#include "rbwr/physics.hpp"

#include <algorithm>
#include <cmath>

namespace rbwr::physics {

namespace {
constexpr double kelvin_offset = 273.15;
constexpr double critical_temperature_k = 647.096;
constexpr double critical_pressure_kpa = 22064.0;
}

PumpModel::PumpModel(double rated_flow_kgs, double rated_head_kpa, double rated_power_mw,
                     double npsh_required_kpa)
    : rated_flow_kgs_(rated_flow_kgs), rated_head_kpa_(rated_head_kpa),
      rated_power_mw_(rated_power_mw), npsh_required_kpa_(npsh_required_kpa) {}

PumpResult PumpModel::solve(double speed_pct, double system_head_kpa,
                            double available_npsh_kpa) const {
  const double speed = std::clamp(speed_pct / 100.0, 0.0, 1.0);
  const double shutoff_head = rated_head_kpa_ * speed * speed;
  const double capacity = rated_flow_kgs_ * speed;
  const double flow = std::clamp(capacity * (1.0 - system_head_kpa /
      std::max(shutoff_head, 1.0)), 0.0, capacity);
  const double head = std::max(0.0, shutoff_head * (1.0 - flow / std::max(capacity, 1.0)));
  const double power = rated_power_mw_ * speed * speed * speed;
  return {flow, head, power, available_npsh_kpa < npsh_required_kpa_};
}

ValveModel::ValveModel(double cv_max, double stroke_seconds)
    : cv_max_(cv_max), stroke_seconds_(std::max(stroke_seconds, 0.001)) {}

double ValveModel::flow_kgs(double opening_pct, double upstream_kpa,
                            double downstream_kpa, double density_kgm3) const {
  const double opening = std::clamp(opening_pct / 100.0, 0.0, 1.0);
  const double pressure_drop = std::max(0.0, upstream_kpa - downstream_kpa);
  return cv_max_ * opening * std::sqrt(pressure_drop * 1000.0 * density_kgm3) * 1.0e-5;
}

double ValveModel::stroke_rate_pct_per_s() const { return 100.0 / stroke_seconds_; }

double saturation_pressure_kpa(double temperature_c) {
    const double temperature_k = std::clamp(temperature_c + kelvin_offset, 273.15, critical_temperature_k);
    const double theta = 1.0 - temperature_k / critical_temperature_k;
    const double log_ratio = (-7.85951783 * theta + 1.84408259 * std::pow(theta, 1.5) -
      11.7866497 * std::pow(theta, 3.0) + 22.6807411 * std::pow(theta, 3.5) -
      15.9618719 * std::pow(theta, 4.0) + 1.80122502 * std::pow(theta, 7.5)) /
      (temperature_k / critical_temperature_k);
    return critical_pressure_kpa * std::exp(log_ratio);
}

double saturation_temperature_c(double pressure_kpa) {
  double low = 0.0;
  double high = 373.946;
  const double target = std::clamp(pressure_kpa, 0.611, 22064.0);
  for (int iteration = 0; iteration < 48; ++iteration) {
    const double middle = (low + high) * 0.5;
    if (saturation_pressure_kpa(middle) < target) low = middle;
    else high = middle;
  }
  return (low + high) * 0.5;
}

SteamState saturated_vapor(double pressure_kpa) {
  const double temperature_c = saturation_temperature_c(pressure_kpa);
  const double temperature_k = temperature_c + kelvin_offset;
  const double specific_volume = 0.461526 * temperature_k / std::max(pressure_kpa, 0.611);
  const double enthalpy = 2500.9 + 1.82 * temperature_c;
  return {pressure_kpa, temperature_c, enthalpy, specific_volume, 1.0};
}

PointKinetics::PointKinetics() { reset(); }

void PointKinetics::reset(double normalized_power) {
  power_ = std::max(0.0, normalized_power);
  previous_power_ = power_;
  period_seconds_ = 1000.0;
  for (std::size_t index = 0; index < precursor_.size(); ++index)
    precursor_[index] = beta_[index] * power_ / lambda_[index];
}

void PointKinetics::step(double dt_seconds, double reactivity) {
  const double dt = std::clamp(dt_seconds, 1.0e-5, 0.25);
  const double delayed_source = [&] {
    double source = 0.0;
    for (double precursor : precursor_) source += precursor;
    return source;
  }();
  const double beta_total = 0.0062;
  const double prompt_lifetime = 0.0001;
  const double derivative = ((reactivity - beta_total) / prompt_lifetime) * power_ + delayed_source;
  power_ = std::max(0.0, power_ + derivative * dt);
  for (std::size_t index = 0; index < precursor_.size(); ++index) {
    const double source = beta_[index] * power_ / prompt_lifetime;
    precursor_[index] = std::max(0.0, precursor_[index] +
        (source - lambda_[index] * precursor_[index]) * dt);
  }
  if (power_ > 1.0e-9 && previous_power_ > 1.0e-9) {
    const double logarithmic_rate = std::log(power_ / previous_power_) / dt;
    period_seconds_ = std::abs(logarithmic_rate) < 1.0e-9 ? 1000.0 : 1.0 / logarithmic_rate;
  }
  previous_power_ = power_;
}

double PointKinetics::normalized_power() const { return power_; }
double PointKinetics::period_seconds() const { return period_seconds_; }

}  // namespace rbwr::physics
