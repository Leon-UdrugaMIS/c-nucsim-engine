#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rbwr {

struct EngineInfo {
  std::string name;
  std::string version;
  std::uint64_t tick = 0;
};

enum class Role { observer, operator_role, instructor };

struct Command {
  std::string type;
  std::string req_id;
  std::string control_id;
  std::string value;
};

struct CommandResult {
  bool ok = false;
  std::string req_id;
  std::string code;
  std::string detail;
  std::string hint;
  std::uint64_t applied_tick = 0;
  std::string new_value;
  double settling_estimate_s = 0.0;
};

struct TagValue {
  std::string value;
  std::string unit;
  std::string quality = "GOOD";
};

struct AnnunciatorState {
  std::string state = "NORMAL";
  bool acknowledged = false;
  bool silenced = false;
};

struct Snapshot {
  std::uint64_t tick = 0;
  double sim_time = 0.0;
  bool paused = false;
  double speed = 1.0;
  std::map<std::string, TagValue> tags;
  std::map<std::string, AnnunciatorState> annunciators;
};

struct Event {
  std::uint64_t tick = 0;
  std::string type;
  std::string id;
  std::string detail;
};

struct SaveSlotInfo {
  int slot = 0;
  bool exists = false;
  std::uint64_t tick = 0;
  double sim_time = 0.0;
};

class Engine {
 public:
  explicit Engine(std::string version = "0.1.0");
  [[nodiscard]] EngineInfo info() const;
  void step(double dt_seconds);
  [[nodiscard]] CommandResult apply_command(const Command& command, Role role);
  [[nodiscard]] Snapshot snapshot() const;
  [[nodiscard]] std::vector<std::string> tag_ids() const;
  [[nodiscard]] std::vector<std::string> control_ids() const;
  [[nodiscard]] std::vector<std::string> annunciator_ids() const;
  [[nodiscard]] std::vector<Event> events_since(std::uint64_t tick) const;
  void inject_malfunction(const std::string& id, bool active = true);
  void set_rps_override(const std::string& trigger, bool enabled);
  [[nodiscard]] bool rps_trigger_enabled(const std::string& trigger) const;
  [[nodiscard]] bool rps_enabled() const;
  [[nodiscard]] bool malfunction_active(const std::string& id) const;
  [[nodiscard]] std::vector<std::string> malfunction_ids() const;
  void pause(bool value);
  void set_speed(double value);
  bool save(const std::string& path) const;
  bool load(const std::string& path);
  [[nodiscard]] bool save_slot(int slot, const std::string& directory = ".") const;
  [[nodiscard]] bool load_slot(int slot, const std::string& directory = ".");
  [[nodiscard]] std::vector<SaveSlotInfo> save_slots(const std::string& directory = ".") const;
  [[nodiscard]] bool load_scenario(const std::string& name);
  [[nodiscard]] std::string scenario_name() const;

 private:
  struct ControlSpec {
    std::string type;
    Role minimum_role = Role::operator_role;
    std::string default_value;
    std::vector<std::string> positions;
    double minimum = 0.0;
    double maximum = 100.0;
    bool numeric = false;
  };

  void update_process(double dt);
  void update_rate_switches(double dt);
  void publish_state();
  void evaluate_annunciators();
  void emit_alarm_event(const std::string& id, const std::string& state);
  [[nodiscard]] bool control_value(const std::string& id) const;
  [[nodiscard]] std::optional<double> numeric_value(const std::string& value) const;
  [[nodiscard]] bool valid_position(const ControlSpec& spec, const std::string& value) const;
  [[nodiscard]] static std::string save_slot_path(int slot, const std::string& directory);

  EngineInfo info_;
  Snapshot state_;
  std::map<std::string, std::string> controls_;
  std::map<std::string, ControlSpec> registry_;
  std::map<std::string, bool> malfunctions_;
  std::map<std::string, double> analog_state_;
  std::map<std::string, std::string> held_rate_switches_;
  std::map<std::string, double> rate_switch_last_update_;
  double scram_button_a_time_ = -1.0;
  double scram_button_b_time_ = -1.0;
  bool edg_start_commanded_ = false;
  std::string scenario_ = "cold_shutdown";
  std::vector<int> selected_groups_;
  std::vector<Event> events_;
  double neutron_population_ = 0.0;
  std::array<double, 6> delayed_precursors_{};
  double previous_neutron_population_ = 0.0;
  double reactor_period_s_ = 1000.0;
  bool rps_channel_a_tripped_ = false;
  bool rps_channel_b_tripped_ = false;
  bool rps_enabled_ = true;
  std::map<std::string, bool> rps_trigger_overrides_;
};

}  // namespace rbwr
