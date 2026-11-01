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
  const auto annunciators = scenarios.snapshot().annunciators;
  assert(annunciators.size() >= 130);
  assert(annunciators.at("ann.elec.startup_transformer_overload").state == "NORMAL");
  assert(annunciators.at("ann.eccs.lpci_available").state == "NORMAL");
  assert(annunciators.at("ann.rmcp.high_rpv_pressure").state == "NORMAL");
  assert(annunciators.at("ann.recirc.pump1_cavitation").state == "NORMAL");
  assert(annunciators.at("ann.turb.generator_reverse_power").state == "NORMAL");
  assert(annunciators.at("ann.cond.offgas_open").state == "NORMAL");
  assert(annunciators.at("ann.dea.low_low_water_level").state == "NORMAL");
    assert(scenarios.apply_command(
      {"button", "group-3", "rmcp.group_select_3", "true"}, rbwr::Role::operator_role).ok);
    assert(scenarios.snapshot().tags.at("rmcp.selected_groups").value == "[3]");
    assert(scenarios.apply_command(
      {"button", "group-clear", "rmcp.group_clear", "true"}, rbwr::Role::operator_role).ok);
    assert(scenarios.snapshot().tags.at("rmcp.selected_groups").value == "[]");
  const std::string pressure_before_fault = scenarios.snapshot().tags.at("reactor.pressure_kpa").value;
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.dead");
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").value == "null");
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").quality == "BAD");
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.dead", false);
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").value == pressure_before_fault);
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.drift");
  scenarios.step(1.0);
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").quality == "SUBSTITUTED");
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.drift", false);
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.noise");
  scenarios.step(0.1);
  assert(scenarios.snapshot().tags.at("reactor.pressure_kpa").quality == "SUBSTITUTED");
  scenarios.inject_malfunction("mf.sensor.reactor.pressure_kpa.noise", false);
  assert(scenarios.load_scenario("full_power"));
  assert(scenarios.apply_command(
      {"set_percent", "trip-power", "rmcp.apr_setpoint", "120"}, rbwr::Role::operator_role).ok);
  assert(scenarios.apply_command(
      {"toggle", "trip-apr", "rmcp.apr_control", "true"}, rbwr::Role::operator_role).ok);
  bool pressure_alarm_seen = false;
  for (int tick = 0; tick < 100; ++tick) {
    scenarios.step(1.0);
    pressure_alarm_seen = pressure_alarm_seen ||
        scenarios.snapshot().annunciators.at("ann.rmcp.high_rpv_pressure").state == "TRIGGERED";
  }
  assert(pressure_alarm_seen);
  bool pressure_event_seen = false;
  for (const auto& event : scenarios.events_since(0))
    pressure_event_seen = pressure_event_seen || event.id == "ann.rmcp.high_rpv_pressure";
  assert(pressure_event_seen);
  assert(scenarios.load_scenario("cold_shutdown"));
  assert(scenarios.snapshot().annunciators.at("ann.rmcp.high_rpv_pressure").state == "NORMAL");
  rbwr::Engine steam;
  assert(steam.load_scenario("full_power"));
  for (const auto& command : {
           rbwr::Command{"toggle", "ri1", "recirc.inlet1", "true"},
           rbwr::Command{"toggle", "ro1", "recirc.outlet1", "true"},
           rbwr::Command{"toggle", "rp1", "recirc.pump1", "true"}})
    assert(steam.apply_command(command, rbwr::Role::operator_role).ok);
  steam.step(1.0);
  assert(steam.snapshot().tags.at("recirc.pump1_flow_kgs").value == "4500.000");
  assert(steam.apply_command(
      {"set_percent", "steam-power", "rmcp.apr_setpoint", "120"}, rbwr::Role::operator_role).ok);
  assert(steam.apply_command(
      {"toggle", "steam-apr", "rmcp.apr_control", "true"}, rbwr::Role::operator_role).ok);
  bool msrv_seen = false;
  for (int tick = 0; tick < 100; ++tick) {
    steam.step(1.0);
    msrv_seen = msrv_seen || steam.snapshot().tags.at("srv.msrv1_open").value == "true";
  }
  assert(msrv_seen);
  assert(!steam.malfunction_ids().empty());
  steam.inject_malfunction("mf.cond.loss_vacuum");
  steam.step(1.0);
  assert(std::stod(steam.snapshot().tags.at("cond.pressure_mbar").value) > 60.0);
  steam.inject_malfunction("mf.turb.overspeed");
  steam.step(0.1);
  assert(steam.snapshot().tags.at("turb.rpm").value == "4000.000");
  steam.inject_malfunction("mf.turb.high_vibration");
  steam.step(0.1);
  assert(steam.snapshot().tags.at("turb.vibration_um").value == "150.000");
  steam.inject_malfunction("mf.steam.srv_stuck_open");
  steam.step(0.1);
  assert(steam.snapshot().tags.at("srv.msrv1_open").value == "true");
  rbwr::Engine fuel;
  assert(fuel.load_scenario("full_power"));
  for (int tick = 0; tick < 10; ++tick) fuel.step(1.0);
  assert(fuel.snapshot().tags.count("fa.01.l1.temp_c") == 1);
  assert(std::stod(fuel.snapshot().tags.at("fa.01.l1.temp_c").value) > 300.0);
  assert(fuel.snapshot().tags.count("reactor.decay_heat_mw") == 1);
  return 0;
}
