#include "rbwr/engine.hpp"

#include <cassert>
#include <cstdio>

int main() {
  rbwr::Engine engine;
  assert(engine.info().tick == 0);
  const auto blocked = engine.apply_command(
    {"toggle", "blocked", "crd.pump1", "true"}, rbwr::Role::observer);
  assert(!blocked.ok && blocked.code == "ERR_ROLE");
  const auto unpowered = engine.apply_command(
    {"toggle", "unpowered", "crd.pump1", "true"}, rbwr::Role::operator_role);
  assert(!unpowered.ok && unpowered.code == "ERR_UNPOWERED");
  assert(engine.apply_command(
    {"toggle", "35a", "elec.startup_tr_breaker", "true"}, rbwr::Role::operator_role).ok);
  assert(engine.apply_command(
    {"toggle", "busa", "elec.bus_a_tr", "true"}, rbwr::Role::operator_role).ok);
  assert(engine.apply_command(
    {"toggle", "lvac", "elec.lvac_dc_tr", "true"}, rbwr::Role::operator_role).ok);
  assert(engine.apply_command(
    {"toggle", "crd", "crd.pump1", "true"}, rbwr::Role::operator_role).ok);
  const auto rate = engine.apply_command(
    {"rate_switch", "rate", "rmcp.group_movement", "WITHDRAW"}, rbwr::Role::operator_role);
  assert(rate.ok);
  engine.step(0.05);
  assert(engine.info().tick == 1);
  assert(engine.snapshot().tags.at("elec.bus.s.energized").value == "true");
  engine.step(0.7);
  const auto events = engine.events_since(0);
  bool deadman_seen = false;
  for (const auto& event : events) deadman_seen = deadman_seen || event.detail == "EVT_DEADMAN_RELEASE";
  assert(deadman_seen);
  assert(engine.apply_command(
    {"button", "scram-a", "rmcp.scram_button_a", "true"}, rbwr::Role::operator_role).ok);
  engine.step(1.0);
  assert(engine.apply_command(
    {"button", "scram-b", "rmcp.scram_button_b", "true"}, rbwr::Role::operator_role).ok);
  assert(engine.malfunction_active("rps.scram_active"));

  rbwr::Engine emergency;
  for (const auto& command : {
           rbwr::Command{"toggle", "e1", "elec.startup_tr_breaker", "true"},
           rbwr::Command{"toggle", "e2", "elec.bus_a_tr", "true"},
           rbwr::Command{"toggle", "e3", "elec.lvac_dc_tr", "true"},
           rbwr::Command{"toggle", "e4", "elec.ac_dc_1_interlock", "true"},
           rbwr::Command{"toggle", "e5", "elec.breaker_72dcg", "true"},
           rbwr::Command{"toggle", "e6", "elec.edg_remote_startup", "true"},
           rbwr::Command{"toggle", "e7", "elec.safety_bus_to_dg", "true"}})
    assert(emergency.apply_command(command, rbwr::Role::operator_role).ok);
  emergency.inject_malfunction("mf.elec.loop");
  for (int tick = 0; tick < 12; ++tick) emergency.step(1.0);
  assert(emergency.snapshot().tags.at("elec.edg1_active").value == "true");
  assert(emergency.snapshot().tags.at("elec.bus.s.energized").value == "true");

  rbwr::Engine eccs;
    assert(eccs.apply_command(
      {"set_percent", "power", "rmcp.apr_setpoint", "100"}, rbwr::Role::operator_role).ok);
    assert(eccs.apply_command(
      {"toggle", "apr", "rmcp.apr_control", "true"}, rbwr::Role::operator_role).ok);
    for (int tick = 0; tick < 30; ++tick) eccs.step(1.0);
  eccs.inject_malfunction("mf.fw.pump_trip");
  for (int tick = 0; tick < 60; ++tick) eccs.step(1.0);
  assert(eccs.snapshot().tags.at("eccs.rcic_flow_kgs").value != "0.000");
  assert(eccs.snapshot().annunciators.at("ann.rmcp.low_reactor_water_level").state == "TRIGGERED");

    rbwr::Engine rods;
    assert(rods.apply_command(
      {"toggle", "crd-power", "elec.startup_tr_breaker", "true"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"toggle", "crd-bus", "elec.bus_a_tr", "true"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"toggle", "crd-safety", "elec.lvac_dc_tr", "true"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"toggle", "crd-pump", "crd.pump1", "true"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"selector", "mode", "rmcp.reactor_mode", "SRM"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"rate_switch", "withdraw", "rmcp.group_movement", "WITHDRAW"}, rbwr::Role::operator_role).ok);
    for (int tick = 0; tick < 10; ++tick) {
      assert(rods.apply_command({"rate_switch", "withdraw", "rmcp.group_movement", "WITHDRAW"},
                                rbwr::Role::operator_role).ok);
      rods.step(0.1);
    }
    assert(rods.snapshot().tags.at("rod.01.pos_pct").value != "0.000");
    assert(rods.apply_command(
      {"button", "scram-a", "rmcp.scram_button_a", "true"}, rbwr::Role::operator_role).ok);
    assert(rods.apply_command(
      {"button", "scram-b", "rmcp.scram_button_b", "true"}, rbwr::Role::operator_role).ok);
    rods.step(1.0);
    assert(rods.snapshot().tags.at("rod.01.pos_pct").value == "0.000");

    rbwr::Engine turbine;
    assert(turbine.apply_command(
      {"toggle", "runup", "turb.auto_runup", "true"}, rbwr::Role::operator_role).ok);
    assert(turbine.apply_command(
      {"selector", "target", "turb.runup_target_rpm", "3600"}, rbwr::Role::operator_role).ok);
    for (int tick = 0; tick < 80; ++tick) turbine.step(1.0);
    const auto sync_rejected = turbine.apply_command(
      {"toggle", "sync", "turb.gen_breaker_sync", "true"}, rbwr::Role::operator_role);
    assert(!sync_rejected.ok && sync_rejected.code == "ERR_INTERLOCK");
    assert(turbine.apply_command(
      {"toggle", "avr", "turb.exciter_avr_breaker", "true"}, rbwr::Role::operator_role).ok);
      assert(turbine.apply_command(
        {"set_percent", "power", "rmcp.apr_setpoint", "100"}, rbwr::Role::operator_role).ok);
      assert(turbine.apply_command(
        {"toggle", "apr", "rmcp.apr_control", "true"}, rbwr::Role::operator_role).ok);
      for (int tick = 0; tick < 30; ++tick) turbine.step(1.0);
    assert(turbine.apply_command(
      {"toggle", "sync", "turb.gen_breaker_sync", "true"}, rbwr::Role::operator_role).ok);
    assert(turbine.snapshot().tags.at("elec.generator_load_mw").value != "0.000");
    const std::string save_path = "rbwr_engine_test.save";
    assert(turbine.save(save_path));
    const auto saved_pressure = turbine.snapshot().tags.at("reactor.pressure_kpa").value;
    turbine.step(1.0);
    assert(turbine.load(save_path));
    assert(turbine.snapshot().tags.at("reactor.pressure_kpa").value == saved_pressure);
    std::remove(save_path.c_str());
    const std::string slot_directory = "rbwr_test_slots";
    for (int slot = 1; slot <= 5; ++slot) assert(turbine.save_slot(slot, slot_directory));
    const auto slots = turbine.save_slots(slot_directory);
    assert(slots.size() == 5);
    for (const auto& slot : slots) assert(slot.exists && slot.tick == turbine.info().tick);
    assert(!turbine.save_slot(0, slot_directory));
    assert(!turbine.load_slot(6, slot_directory));
    turbine.step(1.0);
    assert(turbine.load_slot(3, slot_directory));
    assert(turbine.info().tick == slots.at(2).tick);
    for (int slot = 1; slot <= 5; ++slot)
      std::remove((slot_directory + "/rbwr_save_slot_" + std::to_string(slot) + ".save").c_str());
    std::remove(slot_directory.c_str());

  rbwr::Engine water;
  assert(water.apply_command(
      {"toggle", "water-35a", "elec.startup_tr_breaker", "true"}, rbwr::Role::operator_role).ok);
  assert(water.apply_command(
      {"toggle", "water-busa", "elec.bus_a_tr", "true"}, rbwr::Role::operator_role).ok);
  assert(water.apply_command(
      {"toggle", "water-safety", "elec.lvac_dc_tr", "true"}, rbwr::Role::operator_role).ok);
  assert(water.apply_command(
      {"toggle", "water-cond", "mcc.cond_pump1", "true"}, rbwr::Role::operator_role).ok);
  assert(water.apply_command(
      {"toggle", "water-fw", "mcc.fw_pump1", "true"}, rbwr::Role::operator_role).ok);
  assert(water.apply_command(
      {"toggle", "water-makeup", "cst.cst1_makeup", "true"}, rbwr::Role::operator_role).ok);
  water.step(10.0);
  assert(water.snapshot().tags.at("fw.flow_kgs").value == "1500.000");
  assert(std::stod(water.snapshot().tags.at("cst.cst1_level_pct").value) > 70.0);

  rbwr::Engine scenarios;
  assert(scenarios.load_scenario("full_power"));
  assert(scenarios.scenario_name() == "full_power");
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").value == "7100.000");
  assert(scenarios.snapshot().tags.at("elec.bus.a.energized").value == "true");
  assert(!scenarios.load_scenario("unknown"));
  assert(scenarios.load_scenario("scram_recovery"));
  assert(scenarios.malfunction_active("rps.scram_active"));
  return 0;
}
