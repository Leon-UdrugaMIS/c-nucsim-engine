#include "rbwr/engine.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string json_escape(const std::string& value) {
  std::string result;
  for (const char character : value) {
    if (character == '\\' || character == '"') result += '\\';
    result += character;
  }
  return result;
}

void print_snapshot(const rbwr::Engine& engine) {
  const auto snapshot = engine.snapshot();
  std::cout << "{\"type\":\"snapshot\",\"v\":\"" << engine.info().version
            << "\",\"tick\":" << snapshot.tick << ",\"sim_time\":"
            << snapshot.sim_time << ",\"paused\":" << (snapshot.paused ? "true" : "false")
            << ",\"tags\":{";
  bool first = true;
  for (const auto& [id, tag] : snapshot.tags) {
    if (!first) std::cout << ',';
    first = false;
    std::cout << "\"" << json_escape(id) << "\":{\"v\":\""
              << json_escape(tag.value) << "\",\"u\":\"" << json_escape(tag.unit)
              << "\",\"q\":\"" << json_escape(tag.quality) << "\"}";
  }
  std::cout << "}}\n";
}

}  // namespace

int main() {
  rbwr::Engine engine;
  std::cout << "{\"type\":\"hello\",\"v\":\"" << engine.info().version
            << "\",\"transport\":\"line-json\"}\n";
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string type;
    std::string control;
    std::string value;
    input >> type >> control >> value;
    if (type == "step") {
      engine.step(value.empty() ? 0.05 : std::stod(value));
      print_snapshot(engine);
    } else if (type == "toggle" || type == "selector" || type == "button") {
      const auto result = engine.apply_command({type, "stdin", control, value}, rbwr::Role::operator_role);
      std::cout << "{\"type\":\"command_result\",\"ok\":" << (result.ok ? "true" : "false")
                << ",\"code\":\"" << json_escape(result.code) << "\",\"detail\":\""
                << json_escape(result.detail) << "\",\"hint\":\"" << json_escape(result.hint)
                << "\",\"new_value\":\"" << json_escape(result.new_value) << "\"}\n";
    } else if (type == "malfunction") {
      engine.inject_malfunction(control, value != "false");
      std::cout << "{\"type\":\"malfunction_status\",\"id\":\"" << control << "\"}\n";
    } else if (type == "save_slot" || type == "load_slot") {
      try {
        const int slot = std::stoi(control);
        const bool ok = type == "save_slot" ? engine.save_slot(slot, value.empty() ? "." : value)
                                             : engine.load_slot(slot, value.empty() ? "." : value);
        std::cout << "{\"type\":\"save_result\",\"operation\":\"" << type
                  << "\",\"slot\":" << slot << ",\"ok\":" << (ok ? "true" : "false") << "}\n";
      } catch (...) {
        std::cout << "{\"type\":\"save_result\",\"ok\":false,\"code\":\"ERR_INVALID_VALUE\"}\n";
      }
    } else if (type == "slots") {
      const auto slots = engine.save_slots(value.empty() ? (control.empty() ? "." : control) : value);
      std::cout << "{\"type\":\"save_slots\",\"slots\":[";
      for (std::size_t index = 0; index < slots.size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << "{\"slot\":" << slots[index].slot << ",\"exists\":"
                  << (slots[index].exists ? "true" : "false") << ",\"tick\":"
                  << slots[index].tick << ",\"sim_time\":" << slots[index].sim_time << "}";
      }
      std::cout << "]}\n";
    } else if (type == "scenario") {
      const bool ok = engine.load_scenario(control);
      std::cout << "{\"type\":\"scenario_status\",\"scenario\":\""
                << json_escape(engine.scenario_name()) << "\",\"ok\":"
                << (ok ? "true" : "false") << "}\n";
    } else if (type == "quit") {
      break;
    } else {
      std::cout << "{\"type\":\"error\",\"code\":\"ERR_PROTOCOL\"}\n";
    }
    std::cout.flush();
  }
  return 0;
}
