#include "rbwr/engine.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <utility>

namespace rbwr {

Engine::Engine(std::string version)
    : info_{"RBWR Unit 2 simulation engine", std::move(version), 0} {
  analog_state_["nuc.rtp_pct"] = 0.0;
  analog_state_["reactor.pressure_kpa"] = 101.3;
  analog_state_["reactor.level_nr_m"] = 0.0;
  analog_state_["turb.rpm"] = 3.0;
  analog_state_["cond.pressure_mbar"] = 1013.0;
  analog_state_["crd.drive_water_pressure_kpa"] = 0.0;
  analog_state_["elec.battery_pct"] = 100.0;
  analog_state_["elec.edg1_rpm"] = 0.0;
  analog_state_["elec.edg1_state"] = 0.0;
  analog_state_["eccs.rcic_flow_kgs"] = 0.0;
  analog_state_["eccs.rcic_rpm"] = 0.0;
  analog_state_["eccs.rhr1_flow_kgs"] = 0.0;
  analog_state_["eccs.rhr2_flow_kgs"] = 0.0;
  analog_state_["turb.generator_synced"] = 0.0;
  analog_state_["cst.cst1_level_pct"] = 70.0;
  analog_state_["cst.cst2_level_pct"] = 70.0;
  analog_state_["cond.hotwell_level_pct"] = 55.0;
  analog_state_["dea.level_pct"] = 60.0;
  analog_state_["fw.flow_kgs"] = 0.0;
  for (int rod = 1; rod <= 24; ++rod) {
    analog_state_[std::string("rod.") + (rod < 10 ? "0" : "") + std::to_string(rod) + ".pos_pct"] = 0.0;
  }
  const std::vector<std::pair<std::string, double>> nominal_telemetry = {
      {"turb.oil_temp_c", 48.0}, {"turb.oil_pressure_kpa", 250.0},
      {"turb.hydraulic_pressure_kpa", 9500.0}, {"env.outside_temp_c", 18.0},
      {"env.outside_humidity_pct", 55.0}, {"turb.vibration_um", 35.0},
      {"turb.diff_expansion_mm", 3.0}, {"turb.axial_pressure_kpa", 1800.0},
      {"turb.radial_pressure_kpa", 1500.0}, {"turb.steam_seal_pressure_kpa", 130.0},
      {"turb.generator_temp_c", 85.0}, {"turb.main_steam_pressure_kpa", 7100.0},
      {"turb.main_steam_temp_c", 286.0}, {"turb.hpt1_intake_pct", 85.0},
      {"turb.lpt1_intake_pct", 80.0}, {"turb.lpt2_intake_pct", 80.0},
      {"turb.lpt3_intake_pct", 80.0}, {"fw.temp_c", 215.0},
      {"wt.pol1_level_pct", 90.0}, {"wt.pol2_level_pct", 90.0},
      {"wt.polisher_outflow_kgs", 1500.0}, {"dea.temp_c", 170.0},
      {"dea.pressure_kpa", 800.0}, {"dea.dissolved_o2_ppb", 5.0},
      {"reactor.xe135_pct", 100.0}, {"reactor.i135_pct", 100.0},
      {"fuel.remaining_pct", 100.0}, {"nuc.period_avg_s", 1000.0},
      {"nuc.aprm_pct", 0.0}, {"nuc.irm_pct", 0.0}, {"nuc.srm_period_s", 1000.0},
      {"nuc.srm_depth_pct", 100.0}, {"tower.level_pct", 70.0},
      {"cond.circ_valve_pct", 70.0}, {"cond.circ_flow_kgs", 22000.0}};
  for (const auto& [id, value] : nominal_telemetry) analog_state_[id] = value;
  const auto add = [this](const std::string& id, const std::string& type,
                          const std::string& value,
                          Role role = Role::operator_role,
                          std::vector<std::string> positions = {},
                          double minimum = 0.0, double maximum = 100.0,
                          bool numeric = false) {
    registry_.emplace(id, ControlSpec{type, role, value, std::move(positions),
                                      minimum, maximum, numeric});
    controls_.emplace(id, value);
  };
  for (const auto& id : {"elec.startup_tr_breaker", "elec.bus_a_tr", "elec.bus_b_tr",
                         "elec.lvac_dc_tr", "elec.gen_breaker_52", "crd.pump1",
                         "crd.pump2", "eccs.rcic_turbopump", "turb.stop_valve",
                         "turb.exciter_avr_breaker", "elec.breaker_72dcg",
                         "elec.ac_dc_1_interlock", "elec.edg_remote_startup",
                         "elec.safety_bus_to_dg", "eccs.rhr1_pump", "eccs.rhr2_pump",
                         "rmcp.apr_control", "turb.auto_pressure_control",
                         "turb.gen_breaker_sync", "turb.auto_runup", "cond.auto_control",
                         "cst.cst1_makeup", "cst.cst1_drain", "cst.cst2_makeup",
                         "cst.cst2_drain", "mcc.hotwell_pump", "mcc.cond_pump1",
                         "mcc.cond_pump2", "mcc.fw_pump1", "mcc.fw_pump2"})
    add(id, "toggle", "false");
  for (const auto& id : {"rmcp.scram_button_a", "rmcp.scram_button_b",
                         "elec.ack", "elec.sil", "elec.edg_startup_request"}) {
    add(id, "button", "false");
  }
    add("rmcp.reactor_mode", "selector", "SD", Role::operator_role,
      {"SD", "SRM", "IRM", "RUN"});
    add("rmcp.group_movement", "rate_switch", "/", Role::operator_role,
      {"INSERT", "/", "WITHDRAW"});
    add("rmcp.group_speed", "selector", "SLOW", Role::operator_role,
      {"SLOW", "AVG", "FAST"});
    add("rmcp.apr_setpoint", "percent", "0", Role::operator_role, {}, 0.0,
      120.0, true);
    add("turb.runup_target_rpm", "selector", "STOP", Role::operator_role,
      {"STOP", "900", "1800", "2700", "3600"});
  add("eccs.rhr1_mode", "selector", "LPCI", Role::operator_role, {"LPCI", "RHR"});
  add("eccs.rhr2_mode", "selector", "LPCI", Role::operator_role, {"LPCI", "RHR"});
  add("eccs.rhr1_injection_valve", "selector", "CLOSED", Role::operator_role,
      {"OPEN", "CLOSED"});
  add("eccs.rhr2_injection_valve", "selector", "CLOSED", Role::operator_role,
      {"OPEN", "CLOSED"});
    add("crd.accumulator_valve", "selector", "CLOSED", Role::operator_role,
      {"CLOSED", "REFILL"});
    add("crd.flow_selector", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("elec.unit_interlock_52", "toggle", "false");
    add("elec.main_circuit_breaker", "toggle", "false");
    add("elec.dc_bus_source", "selector", "SAFETY", Role::operator_role,
      {"EMERGENCY", "SAFETY"});
    add("elec.rolldown_protection", "toggle", "false");
    add("elec.edg_emergency_trip", "guarded_button", "false");
    add("eccs.rcic_steam_inlet", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("eccs.cst_selector", "selector", "BOTH", Role::operator_role,
      {"BOTH", "1", "2"});
    add("ads.system_inhibit", "toggle", "false");
    add("wt.pol_info_selector", "selector", "POL1", Role::operator_role,
      {"POL1", "POL2"});
    add("wt.polisher_selector", "selector", "BOTH", Role::operator_role,
      {"1", "2", "BOTH"});
    add("mcc.autocontrol", "toggle", "false");
    add("mcc.reset_setpoints", "button", "false");
    add("mcc.condensate_outflow", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("mcc.fw_inlet1", "toggle", "false");
    add("mcc.fw_outlet1", "toggle", "false");
    add("mcc.fw_inlet2", "toggle", "false");
    add("mcc.fw_outlet2", "toggle", "false");
    add("mcc.fw_pump1_speed", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("mcc.fw_pump2_speed", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("mcc.hp_fw_preheater1", "toggle", "false");
    add("mcc.hp_fw_preheater2", "toggle", "false");
    add("mcc.hp_fw_preheater3", "toggle", "false");
    add("rmcp.rod_control_mode", "selector", "GROUP", Role::operator_role,
      {"GROUP", "ALL RODS"});
    add("rmcp.auto_rod_balancer", "selector", "OFF", Role::operator_role,
      {"OFF", "SLOW", "FAST"});
    add("rmcp.control_mode", "selector", "RODS", Role::operator_role,
      {"RECIRC", "RODS", "GROUP"});
    add("recirc.outlet1", "toggle", "false");
    add("recirc.inlet1", "toggle", "false");
    add("recirc.pump1", "toggle", "false");
    add("recirc.pump1_speed", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("recirc.outlet2", "toggle", "false");
    add("recirc.inlet2", "toggle", "false");
    add("recirc.pump2", "toggle", "false");
    add("recirc.pump2_speed", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("turb.precise_inlet", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("turb.control_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("turb.steam_bypass_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("cond.circ_pump1_breaker", "toggle", "false");
    add("cond.circ_pump2_breaker", "toggle", "false");
    add("cond.circ_pump_valve", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("cond.car_pump1_breaker", "toggle", "false");
    add("cond.car_pump2_breaker", "toggle", "false");
    add("cond.sjae1_breaker", "toggle", "false");
    add("cond.sjae1_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("cond.sjae2_breaker", "toggle", "false");
    add("cond.sjae2_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("tower.makeup_pump_breaker", "toggle", "false");
    add("tower.makeup_speed_valve", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("tower.cond_to_atm_valve", "rate_switch", "/", Role::operator_role,
      {"--", "-", "/", "+", "++"});
    add("dea.auto_control", "toggle", "false");
    add("dea.emergency_steam_inlet_fast_close", "guarded_button", "false");
    add("dea.gas_out_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
    add("dea.steam_in_valve", "rate_switch", "/", Role::operator_role,
      {"-", "/", "+"});
  add("sim.pause", "sim", "false", Role::instructor);
  add("sim.resume", "sim", "false", Role::instructor);
  add("sim.speed", "sim", "1.0", Role::instructor);
  for (const auto& id : {"ann.elec.bus_a_unpowered", "ann.elec.bus_s_unpowered",
                         "ann.elec.offsite_power_connection_loss", "ann.rps.reactor_trip_state",
                         "ann.crd.drive_water_pressure_low"})
    state_.annunciators[id] = {};
  const std::vector<std::string> annunciator_ids = {
      "ann.crd.hcu_accumulators_pressure_low", "ann.crd.water_pressure_low",
      "ann.cst.cst1_high_level", "ann.cst.cst2_high_level", "ann.cst.cst1_low_level",
      "ann.cst.cst2_low_level", "ann.cst.rcic_suction_from_cst", "ann.cst.cst1_drain",
      "ann.cst.cst2_drain", "ann.cst.cst1_makeup", "ann.cst.cst2_makeup",
      "ann.elec.islanding_mode_configuration", "ann.elec.startup_transformer_breaker_closed",
      "ann.elec.startup_transformer_overload", "ann.elec.main_circuit_breaker_open",
      "ann.elec.edg_not_preheated", "ann.elec.dc_bus_main_battery_active",
      "ann.elec.main_battery_critically_low", "ann.elec.bus_b_unpowered",
      "ann.elec.auto_startup_not_available", "ann.elec.dc_bus_unpowered",
      "ann.elec.rolldown_main_bus_breaker_protection_trouble",
      "ann.elec.unit_interlock_receiving_power", "ann.elec.edg2_startup_process",
      "ann.elec.diesel_generator_active", "ann.elec.generator_short_circuit",
      "ann.elec.unit_interlock_sending_power", "ann.elec.edg1_startup_process",
      "ann.eccs.sdc1_active", "ann.eccs.sdc2_active", "ann.eccs.lpci_available",
      "ann.eccs.rcic_turbopump_enabled", "ann.eccs.rcic_turbine_running",
      "ann.eccs.lpci_active", "ann.eccs.low_reactor_water_level",
      "ann.ads.arm_failure_disarmed", "ann.ads.srv1_open", "ann.ads.srv2_open",
      "ann.ads.system_out_of_service", "ann.ads.system_armed", "ann.srv.srv12_open",
      "ann.srv.srv34_open", "ann.srv.srv56_open",
      "ann.wt.polisher1_conductivity_high", "ann.wt.polisher2_conductivity_high",
      "ann.wt.polisher1_dp_high", "ann.wt.polisher2_dp_high",
      "ann.wt.condensate_polisher_bypass", "ann.wt.polisher1_regen_in_progress",
      "ann.wt.polisher2_regen_in_progress", "ann.mcc.low_hotwell_level",
      "ann.mcc.hotwell_water_drain", "ann.mcc.high_feedwater_heater_disabled",
      "ann.mcc.high_reactor_water_level", "ann.mcc.low_deaerator_water_level",
      "ann.mcc.deaerator_water_drain", "ann.mcc.low_feedwater_temperature",
      "ann.mcc.eccs_mcc_conflict", "ann.mcc.reactor_water_drain",
      "ann.mcc.exposed_fuel_rods", "ann.mcc.hotwell_makeup_pumps_active",
      "ann.mcc.feedwater_pump_temperature_low", "ann.mcc.feedwater_pump_overheat",
      "ann.mcc.feedwater_pump_trip", "ann.rps.manual_trip", "ann.rps.auto_trip",
      "ann.rps.reactor_pressure_high_trip", "ann.rps.offsite_power_loss_trip",
      "ann.rps.aprm_upscale_trip", "ann.rps.aprm_downscale_trip",
      "ann.rps.lprm_upscale_trip", "ann.rps.lprm_low_period_trip",
      "ann.rps.low_period_trip", "ann.rps.reactor_level_low_trip",
      "ann.rps.mode_switch_shutdown_trip", "ann.aux.shutdown_cooling_in_service_fault",
      "ann.aux.scram_not_available", "ann.aux.rod_drive_water_pressure_low",
      "ann.aux.insufficient_drive_water_pressure", "ann.aux.high_i135_concentration",
      "ann.aux.high_xe135_concentration", "ann.rmcp.low_reactor_thermal_power",
      "ann.rmcp.low_reactor_period", "ann.rmcp.rps_channel_a_trip",
      "ann.rmcp.high_reactor_water_level", "ann.rmcp.low_local_reactor_period",
      "ann.rmcp.rpv_mechanically_inoperable", "ann.rmcp.high_apr_thermal_power",
      "ann.rmcp.high_lprm_thermal_power", "ann.rmcp.high_rod_power",
      "ann.rmcp.rps_channel_b_trip", "ann.rmcp.rpv_rod_drive_unserviceable",
      "ann.rmcp.rods_temperature_imbalance", "ann.rmcp.reactor_water_drain",
      "ann.rmcp.high_rpv_pressure", "ann.rmcp.core_neutron_flux_low",
      "ann.rmcp.rcic_turbine_operational", "ann.rmcp.lpci_system_active",
      "ann.rmcp.reactor_automatic_control", "ann.rmcp.shutdown_cooling_system",
      "ann.rad.turbine_hall_high", "ann.rad.condenser_area_high",
      "ann.rad.reactor_hall_high", "ann.rad.turbine_secondary_hall_high",
      "ann.rad.condenser_secondary_area_high", "ann.rad.reactor_secondary_hall_high",
      "ann.recirc.pump1_cavitation", "ann.recirc.pump2_cavitation",
      "ann.recirc.no_water_flow", "ann.recirc.low_water_flow",
      "ann.recirc.flow_balance", "ann.turb.no_condenser_vacuum",
      "ann.turb.high_turbine_vibration", "ann.turb.bypass_valve_open",
      "ann.turb.main_circuit_breaker_open", "ann.turb.high_turbine_rpm",
      "ann.turb.high_generator_load", "ann.turb.high_radiation_sensor8",
      "ann.turb.generator_reverse_power", "ann.turb.generator_short_circuit",
      "ann.turb.no_rotor_excitation_field", "ann.turb.turbine_trip",
      "ann.turb.high_main_steam_temperature", "ann.turb.high_main_steam_pressure",
      "ann.turb.smoke_detected", "ann.cond.sjae_disabled", "ann.cond.high_vacuum",
      "ann.cond.low_vacuum", "ann.cond.offgas_open", "ann.cond.steam_condensation_ineffective",
      "ann.cond.hotwell_water_drain", "ann.cond.circ_pump_demand_trouble",
      "ann.cond.hotwell_level_control_valve_open", "ann.cond.hotwell_makeup_valve_open",
      "ann.cond.low_water_level", "ann.tower.water_level_high", "ann.tower.water_level_low",
      "ann.tower.outside_temperature_high", "ann.dea.pressure_low", "ann.dea.pressure_high",
      "ann.dea.temperature_high", "ann.dea.temperature_low", "ann.dea.water_drain",
      "ann.dea.low_low_water_level", "ann.dea.rupture_disk_trouble",
      "ann.dea.low_water_level", "ann.dea.deaeration_ineffective", "ann.dea.autocontrol"};
  for (const auto& id : annunciator_ids) state_.annunciators.emplace(id, AnnunciatorState{});
  publish_state();
  evaluate_annunciators();
}

EngineInfo Engine::info() const { return info_; }

void Engine::step(double dt_seconds) {
  if (!std::isfinite(dt_seconds) || dt_seconds <= 0.0 || state_.paused) return;
  const double dt = std::min(dt_seconds * state_.speed, 1.0);
  state_.sim_time += dt;
  ++info_.tick;
  state_.tick = info_.tick;
  update_rate_switches(dt);
  update_process(dt);
  publish_state();
  evaluate_annunciators();
}

CommandResult Engine::apply_command(const Command& command, Role role) {
  CommandResult result{false, command.req_id, "ERR_UNKNOWN_CONTROL", "", "", info_.tick, "", 0.0};
  if (command.type == "request_snapshot" || command.type == "ping" || command.type == "resume") {
    result.ok = true;
    result.code.clear();
    result.new_value = "accepted";
    return result;
  }
  const auto spec = registry_.find(command.control_id);
  if (spec == registry_.end()) {
    result.detail = "Control does not exist in the live registry";
    result.hint = "Request the control registry and use an exact control_id";
    return result;
  }
  if (role < spec->second.minimum_role) {
    result.code = "ERR_ROLE";
    result.detail = "The authenticated role cannot operate this control";
    result.hint = "Authenticate with the required role";
    return result;
  }
  const bool percent_command = spec->second.type == "percent" && command.type == "set_percent";
  if (command.type != spec->second.type &&
      !(spec->second.type == "sim" && command.type == "sim") && !percent_command) {
    result.code = "ERR_SCHEMA";
    result.detail = "Command type does not match the registered control";
    result.hint = "Use the control type from the registry";
    return result;
  }
  if (command.type == "sim") {
    if (command.control_id == "sim.pause") pause(true);
    else if (command.control_id == "sim.resume") pause(false);
    else if (command.control_id == "sim.speed") {
      const auto value = numeric_value(command.value);
      if (!value || *value < 0.1 || *value > 20.0) {
        result.code = "ERR_OUT_OF_RANGE";
        result.detail = "Simulation speed must be between 0.1 and 20.0";
        result.hint = "Send a value in the supported speed range";
        return result;
      }
      set_speed(*value);
    }
    result.ok = true;
    result.code.clear();
    result.new_value = command.value;
    events_.push_back({info_.tick, "sim_status", command.control_id, command.value});
    return result;
  }
  if (state_.paused) {
    result.code = "ERR_SIM_PAUSED";
    result.detail = "Simulation is paused";
    result.hint = "Resume the simulation before issuing plant commands";
    return result;
  }
  if (command.type == "toggle" && command.value != "true" && command.value != "false") {
    result.code = "ERR_INVALID_VALUE";
    result.detail = "Toggle values must be true or false";
    result.hint = "Send a boolean value";
    return result;
  }
  if (!valid_position(spec->second, command.value)) {
    result.code = "ERR_INVALID_VALUE";
    result.detail = "Value is not valid for the registered control positions or range";
    result.hint = "Use the positions or numeric range advertised by the control registry";
    return result;
  }
  if (command.type == "button" && command.control_id != "elec.ack" && command.control_id != "elec.sil" &&
      command.control_id != "rmcp.scram_button_a" && command.control_id != "rmcp.scram_button_b" &&
      command.control_id != "elec.edg_startup_request") {
    result.code = "ERR_UNSUPPORTED";
    result.detail = "The registered button has no engine action";
    result.hint = "Use a supported control";
    return result;
  }
  if (command.control_id == "elec.bus_a_tr" && !control_value("elec.startup_tr_breaker") &&
      !malfunctions_["mf.elec.loop"]) {
    result.code = "ERR_INTERLOCK";
    result.detail = "Startup transformer is not energized";
    result.hint = "Close elec.startup_tr_breaker first";
    return result;
  }
  if ((command.control_id == "crd.pump1" || command.control_id == "crd.pump2") &&
      !control_value("elec.lvac_dc_tr")) {
    result.code = "ERR_UNPOWERED";
    result.detail = "Safety bus is de-energized";
    result.hint = "Energize Bus A and close the LVAC/DC transformer";
    return result;
  }
  if ((command.control_id == "eccs.rhr1_pump" || command.control_id == "eccs.rhr2_pump") &&
      state_.tags["elec.bus.s.energized"].value != "true") {
    result.code = "ERR_UNPOWERED";
    result.detail = "Safety bus is de-energized";
    result.hint = "Energize Bus S before starting an RHR pump";
    return result;
  }
  if (command.control_id == "turb.gen_breaker_sync" && command.value == "true") {
    const double rpm = analog_state_["turb.rpm"];
    if (rpm < 3500.0 || rpm > 3700.0) {
      result.code = "ERR_SEQUENCE_PERMISSIVE";
      result.detail = "Generator speed is outside the synchronization window";
      result.hint = "Run the turbine between 3500 and 3700 RPM before closing the generator breaker";
      return result;
    }
    if (!control_value("turb.exciter_avr_breaker")) {
      result.code = "ERR_INTERLOCK";
      result.detail = "Generator excitation is not available";
      result.hint = "Close the exciter AVR breaker above 3000 RPM";
      return result;
    }
    analog_state_["turb.generator_synced"] = 1.0;
  }
  if (command.type == "button" && command.control_id == "elec.ack") {
    for (auto& [id, alarm] : state_.annunciators) alarm.acknowledged = true;
  } else if (command.type == "button" && command.control_id == "elec.sil") {
    for (auto& [id, alarm] : state_.annunciators) alarm.silenced = true;
  } else if (command.control_id == "rmcp.scram_button_a" || command.control_id == "rmcp.scram_button_b") {
    controls_[command.control_id] = "true";
    const double pressed_at = state_.sim_time;
    if (command.control_id == "rmcp.scram_button_a") scram_button_a_time_ = pressed_at;
    if (command.control_id == "rmcp.scram_button_b") scram_button_b_time_ = pressed_at;
    if (scram_button_a_time_ >= 0.0 && scram_button_b_time_ >= 0.0 &&
        std::abs(scram_button_a_time_ - scram_button_b_time_) <= 2.0) {
      controls_["rmcp.reactor_mode"] = "SD";
      malfunctions_["rps.scram_active"] = true;
      events_.push_back({info_.tick, "scram", "rps.scram_active", "manual scram"});
    }
  } else if (command.type == "rate_switch") {
    held_rate_switches_[command.control_id] = command.value;
    rate_switch_last_update_[command.control_id] = state_.sim_time;
  } else if (command.control_id == "elec.edg_startup_request") {
    edg_start_commanded_ = true;
    events_.push_back({info_.tick, "event_log", "elec.edg.1", "EVT_EDG_START_REQUEST"});
  } else if (command.control_id == "elec.edg_emergency_trip") {
    analog_state_["elec.edg1_state"] = 0.0;
    analog_state_["elec.edg1_rpm"] = 0.0;
    edg_start_commanded_ = false;
    events_.push_back({info_.tick, "event_log", "elec.edg.1", "EVT_EDG_TRIP"});
  } else if (command.control_id == "sim.pause" || command.control_id == "sim.resume") {
    pause(command.control_id == "sim.pause");
  } else {
    controls_[command.control_id] = command.value.empty() ? "true" : command.value;
  }
  result.ok = true;
  result.code.clear();
  result.new_value = controls_[command.control_id];
  result.settling_estimate_s = command.type == "toggle" ? 0.0 : 0.5;
  events_.push_back({info_.tick, "command_result", command.control_id, result.new_value});
  publish_state();
  evaluate_annunciators();
  return result;
}

Snapshot Engine::snapshot() const { return state_; }

std::vector<std::string> Engine::tag_ids() const {
  std::vector<std::string> result;
  result.reserve(state_.tags.size());
  for (const auto& [id, value] : state_.tags) result.push_back(id);
  return result;
}

std::vector<std::string> Engine::control_ids() const {
  std::vector<std::string> result;
  result.reserve(controls_.size());
  for (const auto& [id, spec] : registry_) result.push_back(id);
  return result;
}

std::vector<Event> Engine::events_since(std::uint64_t tick) const {
  std::vector<Event> result;
  for (const auto& event : events_) {
    if (event.tick > tick) result.push_back(event);
  }
  return result;
}

void Engine::inject_malfunction(const std::string& id, bool active) {
  malfunctions_[id] = active;
  events_.push_back({info_.tick, "malfunction", id, active ? "active" : "cleared"});
  publish_state();
  evaluate_annunciators();
}

bool Engine::malfunction_active(const std::string& id) const {
  const auto found = malfunctions_.find(id);
  return found != malfunctions_.end() && found->second;
}

void Engine::pause(bool value) { state_.paused = value; }

void Engine::set_speed(double value) { state_.speed = std::clamp(value, 0.1, 20.0); }

bool Engine::control_value(const std::string& id) const {
  const auto found = controls_.find(id);
  return found != controls_.end() && found->second == "true";
}

std::optional<double> Engine::numeric_value(const std::string& value) const {
  try {
    std::size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

bool Engine::valid_position(const ControlSpec& spec, const std::string& value) const {
  if (spec.type == "toggle" || spec.type == "button" || spec.type == "guarded_button")
    return value == "true" || value == "false" || value.empty();
  if (!spec.positions.empty())
    return std::find(spec.positions.begin(), spec.positions.end(), value) != spec.positions.end();
  if (spec.numeric) {
    const auto parsed = numeric_value(value);
    return parsed.has_value() && *parsed >= spec.minimum && *parsed <= spec.maximum;
  }
  return !value.empty();
}

void Engine::update_rate_switches(double dt) {
  for (auto& [id, position] : held_rate_switches_) {
    const double last_update = rate_switch_last_update_[id];
    if (state_.sim_time - last_update > 0.6) {
      position = "/";
      events_.push_back({info_.tick, "event_log", id, "EVT_DEADMAN_RELEASE"});
    }
    if (position == "/" || position == "NEUTRAL") continue;
    const double direction = position == "WITHDRAW" || position == "+" || position == "++" ? 1.0 : -1.0;
    const double rate = position == "++" || position == "--" ? 2.0 : 0.5;
    const double current = analog_state_[id + ".actual_pct"];
    analog_state_[id + ".actual_pct"] = std::clamp(current + direction * rate * dt, 0.0, 100.0);
  }
}

void Engine::update_process(double dt) {
  const bool offsite = !malfunction_active("mf.elec.loop");
  const bool bus_a_source = offsite && control_value("elec.startup_tr_breaker");
  const bool bus_a = bus_a_source && control_value("elec.bus_a_tr");
  const bool safety_from_ac = bus_a && control_value("elec.lvac_dc_tr");
  const bool edg_requested = edg_start_commanded_ ||
      (offsite == false && control_value("elec.edg_remote_startup") &&
       control_value("elec.breaker_72dcg"));
  if (edg_requested && analog_state_["elec.edg1_state"] == 0.0 &&
      !malfunction_active("mf.elec.edg1_fail_start")) {
    analog_state_["elec.edg1_state"] = 1.0;
    analog_state_["elec.edg1_rpm"] = 100.0;
    events_.push_back({info_.tick, "event_log", "elec.edg.1", "EVT_EDG_STARTING"});
  }
  if (analog_state_["elec.edg1_state"] == 1.0) {
    analog_state_["elec.edg1_rpm"] = std::min(1800.0, analog_state_["elec.edg1_rpm"] + 180.0 * dt);
    if (analog_state_["elec.edg1_rpm"] >= 1800.0) {
      analog_state_["elec.edg1_state"] = 2.0;
      events_.push_back({info_.tick, "event_log", "elec.edg.1", "EVT_EDG_RUNNING"});
    }
  }
  const bool edg_power = analog_state_["elec.edg1_state"] == 2.0 &&
      control_value("elec.safety_bus_to_dg");
  const bool safety = safety_from_ac || edg_power;
  const bool emergency_bus = safety || analog_state_["elec.battery_pct"] > 0.0;
  if (safety_from_ac && control_value("elec.ac_dc_1_interlock"))
    analog_state_["elec.battery_pct"] = std::min(100.0, analog_state_["elec.battery_pct"] + 0.01 * dt);
  else if (!safety_from_ac && emergency_bus)
    analog_state_["elec.battery_pct"] = std::max(0.0, analog_state_["elec.battery_pct"] - 0.0069 * dt);
  analog_state_["elec.bus_a"] = bus_a ? 1.0 : 0.0;
  analog_state_["elec.bus_b"] = bus_a_source && control_value("elec.bus_b_tr") ? 1.0 : 0.0;
  analog_state_["elec.bus_s"] = safety ? 1.0 : 0.0;
  analog_state_["elec.emergency_bus"] = emergency_bus ? 1.0 : 0.0;
    for (const char* tank_name : {"cst.cst1", "cst.cst2"}) {
      const std::string tank = tank_name;
    const bool makeup = control_value(tank + "_makeup");
    const bool drain = control_value(tank + "_drain");
    const double change = (makeup && !drain ? 0.10 : 0.0) -
      (drain && !makeup ? 0.10 : 0.0);
    analog_state_[tank + "_level_pct"] = std::clamp(
      analog_state_[tank + "_level_pct"] + change * dt, 0.0, 100.0);
    }
    const bool condensate_available = safety &&
      (control_value("mcc.cond_pump1") || control_value("mcc.cond_pump2"));
    const bool feedwater_running = condensate_available &&
      (control_value("mcc.fw_pump1") || control_value("mcc.fw_pump2")) &&
      !malfunction_active("mf.fw.pump_trip");
    analog_state_["fw.flow_kgs"] = feedwater_running ? 1500.0 : 0.0;
    analog_state_["dea.level_pct"] = std::clamp(analog_state_["dea.level_pct"] +
      (condensate_available ? 0.03 : -0.03) * dt, 0.0, 100.0);
    analog_state_["cond.hotwell_level_pct"] = std::clamp(
      analog_state_["cond.hotwell_level_pct"] +
      (feedwater_running ? -0.03 : 0.01) * dt, 0.0, 100.0);
  const bool crd_running = safety && (control_value("crd.pump1") || control_value("crd.pump2"));
  const double drive_pressure = crd_running ? 8500.0 : 0.0;
  const double reactor_power = analog_state_["nuc.rtp_pct"];
  const bool scrammed = malfunction_active("rps.scram_active");
  const double target_power = scrammed ? 0.0 : std::clamp(analog_state_["nuc.rtp_pct"] +
      (control_value("rmcp.apr_control") ? (std::stod(controls_["rmcp.apr_setpoint"]) - reactor_power) * 0.2 : 0.0), 0.0, 120.0);
  analog_state_["nuc.rtp_pct"] += (target_power - reactor_power) * std::clamp(dt * 0.5, 0.0, 1.0);
  analog_state_["reactor.pressure_kpa"] = 101.3 + analog_state_["nuc.rtp_pct"] * 69.987;
    const bool feedwater_fault = malfunction_active("mf.fw.pump_trip") ||
      malfunction_active("mf.fw.valve_stuck");
    const double level_rate = feedwater_fault ? -0.04 : (crd_running ? 0.002 : 0.000);
    analog_state_["reactor.level_nr_m"] = std::clamp(
      analog_state_["reactor.level_nr_m"] + level_rate * dt, -8.0, 4.0);
    const bool low_low_level = analog_state_["reactor.level_nr_m"] <= -1.9;
    const bool rcic_failed = malfunction_active("mf.eccs.rcic_fail_start");
    const bool rcic_enabled = !rcic_failed &&
      (control_value("eccs.rcic_turbopump") || low_low_level) &&
      analog_state_["reactor.pressure_kpa"] > 1000.0 && emergency_bus;
    analog_state_["eccs.rcic_rpm"] = rcic_enabled ? 1800.0 : 0.0;
    analog_state_["eccs.rcic_flow_kgs"] = rcic_enabled ? 80.0 : 0.0;
    if (rcic_enabled) analog_state_["reactor.level_nr_m"] = std::min(
      0.0, analog_state_["reactor.level_nr_m"] + 0.001 * dt);
    const auto rhr_flow = [this, safety](const std::string& pump,
                       const std::string& mode,
                       const std::string& valve) {
    if (!safety || !control_value(pump) || controls_[valve] != "OPEN") return 0.0;
    if (controls_[mode] == "LPCI" && analog_state_["reactor.pressure_kpa"] > 1500.0) return 0.0;
    if (controls_[mode] == "RHR" && analog_state_["reactor.pressure_kpa"] > 1000.0) return 0.0;
    return 120.0;
    };
    analog_state_["eccs.rhr1_flow_kgs"] = rhr_flow("eccs.rhr1_pump", "eccs.rhr1_mode", "eccs.rhr1_injection_valve");
    analog_state_["eccs.rhr2_flow_kgs"] = rhr_flow("eccs.rhr2_pump", "eccs.rhr2_mode", "eccs.rhr2_injection_valve");
    const bool rod_motion_allowed = controls_["rmcp.reactor_mode"] != "SD" &&
        !scrammed && analog_state_["crd.drive_water_pressure_kpa"] >= 5500.0;
    const auto movement = held_rate_switches_.find("rmcp.group_movement");
    if (movement != held_rate_switches_.end() && rod_motion_allowed) {
      const double sign = movement->second == "WITHDRAW" ? 1.0 : movement->second == "INSERT" ? -1.0 : 0.0;
      const double speed = controls_["rmcp.group_speed"] == "FAST" ? 3.0 :
          controls_["rmcp.group_speed"] == "AVG" ? 1.5 : 0.5;
      for (int rod = 1; rod <= 24; ++rod) {
        const std::string id = std::string("rod.") + (rod < 10 ? "0" : "") + std::to_string(rod) + ".pos_pct";
        analog_state_[id] = std::clamp(analog_state_[id] + sign * speed * dt, 0.0, 100.0);
      }
    }
    if (scrammed) {
      for (int rod = 1; rod <= 24; ++rod) {
        const std::string id = std::string("rod.") + (rod < 10 ? "0" : "") + std::to_string(rod) + ".pos_pct";
        analog_state_[id] = std::max(0.0, analog_state_[id] - 25.0 * dt);
      }
    }
    if (analog_state_["reactor.pressure_kpa"] >= 7900.0 && !scrammed) {
      malfunctions_["rps.scram_active"] = true;
      events_.push_back({info_.tick, "scram", "rps.scram_active", "high pressure trip"});
    }
  double runup_target = 3.0;
  if (control_value("turb.auto_runup")) {
    const std::string target = controls_["turb.runup_target_rpm"];
    if (target == "900") runup_target = 900.0;
    else if (target == "1800") runup_target = 1800.0;
    else if (target == "2700") runup_target = 2700.0;
    else if (target == "3600") runup_target = 3600.0;
  }
  analog_state_["turb.rpm"] += (runup_target - analog_state_["turb.rpm"]) * std::clamp(dt * 0.15, 0.0, 1.0);
  const double condenser_target = control_value("cond.auto_control") ? 60.0 : 1013.0;
  analog_state_["cond.pressure_mbar"] += (condenser_target - analog_state_["cond.pressure_mbar"]) * std::clamp(dt * 0.5, 0.0, 1.0);
  analog_state_["crd.drive_water_pressure_kpa"] = drive_pressure;
}

bool Engine::save(const std::string& path) const {
  std::ofstream output(path);
  if (!output) return false;
  output << std::setprecision(17);
  output << info_.tick << '\n' << state_.sim_time << '\n' << state_.speed << '\n'
         << state_.paused << '\n';
  for (const auto& [id, value] : controls_) output << "C " << id << ' ' << value << '\n';
  for (const auto& [id, active] : malfunctions_) output << "M " << id << ' ' << active << '\n';
    for (const auto& [id, value] : analog_state_) output << "A " << id << ' ' << value << '\n';
  return static_cast<bool>(output);
}

bool Engine::load(const std::string& path) {
  std::ifstream input(path);
  if (!input) return false;
  std::uint64_t tick = 0;
  double sim_time = 0.0;
  double speed = 1.0;
  bool paused = false;
  if (!(input >> tick >> sim_time >> speed >> paused)) return false;
  info_.tick = tick;
  state_.tick = tick;
  state_.sim_time = sim_time;
  state_.speed = std::clamp(speed, 0.1, 20.0);
  state_.paused = paused;
  char kind = 0;
  std::string id;
  std::string value;
  while (input >> kind >> id >> value) {
    if (kind == 'C') controls_[id] = value;
    if (kind == 'M') malfunctions_[id] = value == "1";
    if (kind == 'A') {
      try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(parsed)) return false;
        analog_state_[id] = parsed;
      } catch (...) {
        return false;
      }
    }
  }
  publish_state();
  evaluate_annunciators();
  return true;
}

std::string Engine::save_slot_path(int slot, const std::string& directory) {
  if (slot < 1 || slot > 5) return {};
  return (std::filesystem::path(directory) /
          ("rbwr_save_slot_" + std::to_string(slot) + ".save")).string();
}

bool Engine::save_slot(int slot, const std::string& directory) const {
  const std::string path = save_slot_path(slot, directory);
  if (path.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) return false;
  return save(path);
}

bool Engine::load_slot(int slot, const std::string& directory) {
  const std::string path = save_slot_path(slot, directory);
  return !path.empty() && load(path);
}

std::vector<SaveSlotInfo> Engine::save_slots(const std::string& directory) const {
  std::vector<SaveSlotInfo> result;
  result.reserve(5);
  for (int slot = 1; slot <= 5; ++slot) {
    SaveSlotInfo info;
    info.slot = slot;
    const std::string path = save_slot_path(slot, directory);
    std::ifstream input(path);
    if (input) {
      info.exists = static_cast<bool>(input >> info.tick >> info.sim_time);
    }
    result.push_back(info);
  }
  return result;
}

bool Engine::load_scenario(const std::string& name) {
  if (name != "cold_shutdown" && name != "full_power" && name != "scram_recovery") return false;
  scenario_ = name;
  controls_["rmcp.reactor_mode"] = name == "full_power" ? "RUN" : "SD";
  controls_["rmcp.apr_control"] = name == "full_power" ? "true" : "false";
  controls_["rmcp.apr_setpoint"] = name == "full_power" ? "100" : "0";
  controls_["elec.startup_tr_breaker"] = name == "cold_shutdown" ? "false" : "true";
  controls_["elec.bus_a_tr"] = name == "cold_shutdown" ? "false" : "true";
  controls_["elec.lvac_dc_tr"] = name == "cold_shutdown" ? "false" : "true";
  malfunctions_.clear();
  if (name == "scram_recovery") malfunctions_["rps.scram_active"] = true;
  if (name == "full_power") {
    analog_state_["nuc.rtp_pct"] = 100.0;
    analog_state_["reactor.pressure_kpa"] = 7100.0;
    analog_state_["turb.rpm"] = 3600.0;
    analog_state_["turb.generator_synced"] = 1.0;
    controls_["turb.exciter_avr_breaker"] = "true";
    controls_["turb.gen_breaker_sync"] = "true";
  } else {
    analog_state_["nuc.rtp_pct"] = 0.0;
    analog_state_["reactor.pressure_kpa"] = 101.3;
    analog_state_["turb.rpm"] = 3.0;
    analog_state_["turb.generator_synced"] = 0.0;
  }
  info_.tick = 0;
  state_.tick = 0;
  state_.sim_time = 0.0;
  state_.paused = false;
  update_process(0.0);
  publish_state();
  evaluate_annunciators();
  events_.push_back({0, "scenario_status", name, "loaded"});
  return true;
}

std::string Engine::scenario_name() const { return scenario_; }

void Engine::publish_state() {
  const bool grid = !malfunction_active("mf.elec.loop");
  const bool bus_a = analog_state_["elec.bus_a"] > 0.5;
  const bool bus_b = analog_state_["elec.bus_b"] > 0.5;
  const bool safety = analog_state_["elec.bus_s"] > 0.5;
  const bool scram = malfunction_active("rps.scram_active");
  const auto number = [](double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
  };
  state_.tags["sim.tick"] = {std::to_string(info_.tick), "tick", "GOOD"};
  state_.tags["sim.time_s"] = {number(state_.sim_time), "s", "GOOD"};
  state_.tags["elec.bus.a.energized"] = {bus_a ? "true" : "false", "", "GOOD"};
  state_.tags["elec.bus.b.energized"] = {bus_b ? "true" : "false", "", "GOOD"};
  state_.tags["elec.bus.s.energized"] = {safety ? "true" : "false", "", "GOOD"};
  state_.tags["elec.bus.emerg.energized"] = {analog_state_["elec.emergency_bus"] > 0.5 ? "true" : "false", "", "GOOD"};
  state_.tags["elec.battery_pct"] = {number(analog_state_["elec.battery_pct"]), "%", "GOOD"};
  state_.tags["elec.battery_charging"] = {(safety && control_value("elec.ac_dc_1_interlock")) ? "true" : "false", "", "GOOD"};
  state_.tags["elec.edg1_rpm"] = {number(analog_state_["elec.edg1_rpm"]), "rpm", "GOOD"};
  state_.tags["elec.edg1_active"] = {analog_state_["elec.edg1_state"] == 2.0 ? "true" : "false", "", "GOOD"};
  state_.tags["elec.panel_power_ok"] = {analog_state_["elec.emergency_bus"] > 0.0 ? "true" : "false", "", "GOOD"};
  state_.tags["cst.cst1_level_pct"] = {number(analog_state_["cst.cst1_level_pct"]), "%", "GOOD"};
  state_.tags["cst.cst2_level_pct"] = {number(analog_state_["cst.cst2_level_pct"]), "%", "GOOD"};
  state_.tags["cond.hotwell_level_pct"] = {number(analog_state_["cond.hotwell_level_pct"]), "%", "GOOD"};
  state_.tags["dea.level_pct"] = {number(analog_state_["dea.level_pct"]), "%", "GOOD"};
  state_.tags["fw.flow_kgs"] = {number(analog_state_["fw.flow_kgs"]), "kg/s", "GOOD"};
  state_.tags["eccs.rcic_flow_kgs"] = {number(analog_state_["eccs.rcic_flow_kgs"]), "kg/s", "GOOD"};
  state_.tags["eccs.rcic_turbine_rpm"] = {number(analog_state_["eccs.rcic_rpm"]), "rpm", "GOOD"};
  state_.tags["eccs.rhr1_flow_kgs"] = {number(analog_state_["eccs.rhr1_flow_kgs"]), "kg/s", "GOOD"};
  state_.tags["eccs.rhr2_flow_kgs"] = {number(analog_state_["eccs.rhr2_flow_kgs"]), "kg/s", "GOOD"};
  for (int rod = 1; rod <= 24; ++rod) {
    const std::string id = std::string("rod.") + (rod < 10 ? "0" : "") + std::to_string(rod) + ".pos_pct";
    state_.tags[id] = {number(analog_state_[id]), "%", "GOOD"};
  }
  const std::vector<std::pair<std::string, std::string>> telemetry = {
      {"turb.oil_temp_c", "C"}, {"turb.oil_pressure_kpa", "kPa"},
      {"turb.hydraulic_pressure_kpa", "kPa"}, {"env.outside_temp_c", "C"},
      {"env.outside_humidity_pct", "%"}, {"turb.vibration_um", "um"},
      {"turb.diff_expansion_mm", "mm"}, {"turb.axial_pressure_kpa", "kPa"},
      {"turb.radial_pressure_kpa", "kPa"}, {"turb.steam_seal_pressure_kpa", "kPa"},
      {"turb.generator_temp_c", "C"}, {"turb.main_steam_pressure_kpa", "kPa"},
      {"turb.main_steam_temp_c", "C"}, {"turb.bypass_valve_flow_kgs", "kg/s"},
      {"turb.valve_steam_flow_kgs", "kg/s"}, {"turb.hpt1_intake_pct", "%"},
      {"turb.lpt1_intake_pct", "%"}, {"turb.lpt2_intake_pct", "%"},
      {"turb.lpt3_intake_pct", "%"}, {"elec.startup_tr_load_mw", "MW"},
      {"elec.main_tr_load_mw", "MW"}, {"elec.edg2_rpm", "rpm"},
      {"cst.inflow_kgs", "kg/s"}, {"cst.outflow_kgs", "kg/s"},
      {"reactor.temp_change_rate_cph", "C/h"}, {"crd.flow_kgs", "kg/s"},
      {"fw.total_flow_kgs", "kg/s"}, {"fw.temp_c", "C"},
      {"fw.pump1_temp_c", "C"}, {"fw.pump2_temp_c", "C"},
      {"fw.pump1_flow_kgs", "kg/s"}, {"fw.pump2_flow_kgs", "kg/s"},
      {"fw.pump1_valve_pct", "%"}, {"fw.pump2_valve_pct", "%"},
      {"wt.pol1_level_pct", "%"}, {"wt.pol2_level_pct", "%"},
      {"wt.polisher_outflow_kgs", "kg/s"}, {"mcc.hotwell_makeup_pump_pct", "%"},
      {"mcc.hotwell_level_ctrl_valve_pct", "%"}, {"mcc.sp_hotwell_level_pct", "%"},
      {"mcc.sp_reactor_level_m", "m"}, {"mcc.sp_dea_level_pct", "%"},
      {"cond.steam_flow_kgs", "kg/s"}, {"cond.outflow_kgs", "kg/s"},
      {"cond.valve_pct", "%"}, {"cond.circ_valve_pct", "%"},
      {"cond.circ_flow_kgs", "kg/s"}, {"cond.sjae1_pct", "%"},
      {"cond.sjae2_pct", "%"}, {"tower.makeup_pct", "%"},
      {"tower.level_change_pct_per_min", "%/min"}, {"tower.level_pct", "%"},
      {"tower.cond_to_atm_valve_pct", "%"}, {"dea.outlet_valve_pct", "%"},
      {"dea.inlet_valve_pct", "%"}, {"dea.gas_inflow_kgs", "kg/s"},
      {"dea.gas_outflow_kgs", "kg/s"}, {"dea.temp_c", "C"},
      {"dea.pressure_kpa", "kPa"}, {"dea.flow_kgs", "kg/s"},
      {"dea.dissolved_o2_ppb", "ppb"}, {"reactor.xe135_pct", "%"},
      {"reactor.i135_pct", "%"}, {"fuel.remaining_pct", "%"},
      {"nuc.irm_pct", "%"}, {"nuc.period_avg_s", "s"},
      {"nuc.aprm_pct", "%"}, {"nuc.srm_period_s", "s"},
      {"nuc.srm_depth_pct", "%"}, {"nuc.srm_reading", "decades"},
      {"recirc.pump1_valve_pct", "%"}, {"recirc.pump2_valve_pct", "%"},
      {"recirc.pump1_flow_kgs", "kg/s"}, {"recirc.pump2_flow_kgs", "kg/s"},
      {"pool.supp_temp_c", "C"}, {"pool.supp_level_pct", "%"}};
  for (const auto& [id, unit] : telemetry)
    state_.tags[id] = {number(analog_state_[id]), unit, "GOOD"};
  for (auto& [id, tag] : state_.tags) {
    const std::string prefix = "mf.sensor." + id + ".";
    if (malfunction_active(prefix + "dead")) {
      tag.value = "null";
      tag.quality = "BAD";
    } else if (malfunction_active(prefix + "zero")) {
      tag.value = "0";
      tag.quality = "SUBSTITUTED";
    } else if (malfunction_active(prefix + "low")) {
      tag.value = "0";
      tag.quality = "SUBSTITUTED";
    } else if (malfunction_active(prefix + "high")) {
      tag.value = "1";
      tag.quality = "SUBSTITUTED";
    }
  }
  double site_usage = safety ? 1.0 : 0.0;
  site_usage += bus_a && control_value("crd.pump1") ? 0.8 : 0.0;
  site_usage += bus_a && control_value("crd.pump2") ? 0.8 : 0.0;
  site_usage += bus_a && control_value("cst.cst1_makeup") ? 5.0 : 0.0;
  site_usage += bus_a && control_value("cst.cst2_makeup") ? 5.0 : 0.0;
  site_usage += bus_a && control_value("mcc.hotwell_pump") ? 0.8 : 0.0;
  site_usage += bus_b && control_value("mcc.fw_pump2") ? 7.0 : 0.0;
  state_.tags["elec.site_usage_mw"] = {number(site_usage), "MW", "GOOD"};
  const double generator_load = analog_state_["turb.generator_synced"] > 0.5
      ? std::clamp(analog_state_["nuc.rtp_pct"] * 9.11, 0.0, 1500.0) : 0.0;
  state_.tags["elec.generator_load_mw"] = {number(generator_load), "MW", "GOOD"};
  state_.tags["elec.sent_to_grid_mw"] = {number(grid ? generator_load - site_usage : 0.0), "MW", "GOOD"};
  const double pressure = analog_state_["reactor.pressure_kpa"];
  state_.tags["reactor.pressure_kpa"] = {number(pressure), "kPa", "GOOD"};
  state_.tags["reactor.level_nr_m"] = {number(analog_state_["reactor.level_nr_m"]), "m", "GOOD"};
  state_.tags["nuc.rtp_pct"] = {number(analog_state_["nuc.rtp_pct"]), "%", "GOOD"};
  state_.tags["turb.rpm"] = {number(analog_state_["turb.rpm"]), "rpm", "GOOD"};
  const double freq_diff = (analog_state_["turb.rpm"] - 3600.0) / 60.0;
  const double phase = std::fmod(state_.sim_time * freq_diff * 360.0, 360.0);
  state_.tags["turb.sync"] = {"{\"phase_angle_deg\":" + number(phase) +
      ",\"slip_hz\":" + number(freq_diff) + ",\"freq_diff_hz\":" + number(freq_diff) +
      ",\"voltage_diff_pct\":0,\"sync_permissive\":" +
      ((std::abs(phase) < 10.0 && std::abs(freq_diff) < 0.1) ? "true" : "false") +
      ",\"breaker_closed\":" + (analog_state_["turb.generator_synced"] > 0.5 ? "true" : "false") + "}",
      "object", "GOOD"};
  state_.tags["cond.pressure_mbar"] = {number(analog_state_["cond.pressure_mbar"]), "mbar", "GOOD"};
  state_.tags["rps.scram_active"] = {scram ? "true" : "false", "", "GOOD"};
  state_.tags["crd.drive_water_pressure_kpa"] =
      {number(analog_state_["crd.drive_water_pressure_kpa"]), "kPa", "GOOD"};
  state_.tags["alarm.audible_active"] = {"false", "", "GOOD"};
  for (auto& [id, tag] : state_.tags) {
    const std::string prefix = "mf.sensor." + id + ".";
    if (malfunction_active(prefix + "dead")) {
      tag.value = "null";
      tag.quality = "BAD";
    } else if (malfunction_active(prefix + "zero") || malfunction_active(prefix + "low")) {
      tag.value = "0";
      tag.quality = "SUBSTITUTED";
    } else if (malfunction_active(prefix + "high")) {
      tag.value = "1";
      tag.quality = "SUBSTITUTED";
    }
  }
}

void Engine::evaluate_annunciators() {
  const bool bus_a = state_.tags["elec.bus.a.energized"].value == "true";
  const bool bus_s = state_.tags["elec.bus.s.energized"].value == "true";
  const std::map<std::string, bool> conditions = {
      {"ann.elec.bus_a_unpowered", !bus_a},
      {"ann.elec.bus_s_unpowered", !bus_s},
      {"ann.elec.offsite_power_connection_loss", malfunctions_["mf.elec.loop"]},
      {"ann.elec.main_battery_critically_low", analog_state_["elec.battery_pct"] <= 35.0},
      {"ann.elec.diesel_generator_active", analog_state_["elec.edg1_state"] == 2.0},
      {"ann.eccs.rcic_turbine_running", analog_state_["eccs.rcic_rpm"] > 0.0},
      {"ann.eccs.lpci_active", analog_state_["eccs.rhr1_flow_kgs"] > 0.0 || analog_state_["eccs.rhr2_flow_kgs"] > 0.0},
      {"ann.rmcp.low_reactor_water_level", analog_state_["reactor.level_nr_m"] < -0.3},
      {"ann.rps.reactor_trip_state", malfunction_active("rps.scram_active")},
      {"ann.crd.drive_water_pressure_low", state_.tags["crd.drive_water_pressure_kpa"].value == "0.000"}};
  for (const auto& [id, active] : conditions) {
    const std::string next = active ? "TRIGGERED" : "NORMAL";
    if (state_.annunciators[id].state != next) emit_alarm_event(id, next);
    state_.annunciators[id].state = next;
  }
  bool audible = false;
  for (const auto& [id, alarm] : state_.annunciators)
    audible = audible || (alarm.state == "TRIGGERED" && !alarm.silenced);
  state_.tags["alarm.audible_active"].value = audible ? "true" : "false";
}

void Engine::emit_alarm_event(const std::string& id, const std::string& state) {
  events_.push_back({info_.tick, "annunciator", id, state});
  if (state == "TRIGGERED") state_.annunciators[id].acknowledged = false;
}

}  // namespace rbwr
