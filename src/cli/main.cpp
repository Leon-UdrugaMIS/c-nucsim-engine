#include "rbwr/engine.hpp"

#include <iostream>
#include <string>

int main() {
  rbwr::Engine engine;
  std::cout << "RBWR CLI scenario=cold_start\n";
  const auto close = [&engine](const std::string& id) {
    return engine.apply_command({"toggle", id, id, "true"}, rbwr::Role::operator_role);
  };
  close("elec.startup_tr_breaker");
  close("elec.bus_a_tr");
  close("elec.lvac_dc_tr");
  close("crd.pump1");
  for (int tick = 0; tick < 20; ++tick) engine.step(0.05);
  const auto state = engine.snapshot();
  std::cout << "tick=" << state.tick << " sim_time=" << state.sim_time
            << " bus_a=" << state.tags.at("elec.bus.a.energized").value
            << " drive_pressure_kpa=" << state.tags.at("crd.drive_water_pressure_kpa").value << '\n';
  return 0;
}
