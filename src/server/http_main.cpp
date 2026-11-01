#include "rbwr/engine.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string json_escape(const std::string& value) {
  std::string result;
  for (const char character : value) {
    if (character == '\\' || character == '"') result += '\\';
    result += character;
  }
  return result;
}

std::string json_string_field(const std::string& body, const std::string& field) {
  const std::string key = "\"" + field + "\"";
  const std::size_t key_position = body.find(key);
  if (key_position == std::string::npos) return {};
  const std::size_t colon = body.find(':', key_position + key.size());
  const std::size_t first_quote = body.find('"', colon == std::string::npos ? key_position : colon + 1);
  if (first_quote == std::string::npos) return {};
  const std::size_t second_quote = body.find('"', first_quote + 1);
  if (second_quote == std::string::npos) return {};
  return body.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::string snapshot_json(const rbwr::Engine& engine) {
  const auto snapshot = engine.snapshot();
  std::ostringstream output;
  output << "{\"type\":\"snapshot\",\"v\":\"" << engine.info().version
         << "\",\"tick\":" << snapshot.tick << ",\"sim_time\":"
         << snapshot.sim_time << ",\"paused\":" << (snapshot.paused ? "true" : "false")
         << ",\"scenario\":\"" << json_escape(engine.scenario_name()) << "\",\"tags\":{";
  bool first = true;
  for (const auto& [id, tag] : snapshot.tags) {
    if (!first) output << ',';
    first = false;
    output << "\"" << json_escape(id) << "\":{\"v\":\""
           << json_escape(tag.value) << "\",\"u\":\"" << json_escape(tag.unit)
           << "\",\"q\":\"" << json_escape(tag.quality) << "\"}";
  }
  output << "}}";
  return output.str();
}

std::string response_for(const std::string& target, const rbwr::Engine& engine) {
  if (target == "/api/v1/health") return "{\"ok\":true,\"status\":\"ready\"}";
  if (target == "/api/v1/version")
    return "{\"version\":\"" + json_escape(engine.info().version) + "\"}";
  if (target == "/api/v1/snapshot") return snapshot_json(engine);
  if (target == "/api/v1/tags") {
    std::ostringstream output;
    output << "{\"tags\":[";
    const auto ids = engine.tag_ids();
    for (std::size_t index = 0; index < ids.size(); ++index) {
      if (index != 0) output << ',';
      output << "\"" << json_escape(ids[index]) << "\"";
    }
    output << "]}";
    return output.str();
  }
  if (target == "/api/v1/controls") {
    std::ostringstream output;
    output << "{\"controls\":[";
    const auto ids = engine.control_ids();
    for (std::size_t index = 0; index < ids.size(); ++index) {
      if (index != 0) output << ',';
      output << "\"" << json_escape(ids[index]) << "\"";
    }
    output << "]}";
    return output.str();
  }
  if (target == "/api/v1/scenarios")
    return "{\"scenarios\":[\"cold_shutdown\",\"full_power\",\"scram_recovery\"]}";
  if (target == "/api/v1/slots") {
    std::ostringstream output;
    output << "{\"slots\":[";
    const auto slots = engine.save_slots(".");
    for (std::size_t index = 0; index < slots.size(); ++index) {
      if (index != 0) output << ',';
      output << "{\"slot\":" << slots[index].slot << ",\"exists\":"
             << (slots[index].exists ? "true" : "false") << ",\"tick\":"
             << slots[index].tick << ",\"sim_time\":" << slots[index].sim_time << "}";
    }
    output << "]}";
    return output.str();
  }
  return {};
}

std::string post_response(const std::string& target, const std::string& body,
                          rbwr::Engine& engine, int& status) {
  try {
    if (target == "/api/v1/command") {
      const rbwr::Command command{json_string_field(body, "type"),
                                  json_string_field(body, "req_id"),
                                  json_string_field(body, "control_id"),
                                  json_string_field(body, "value")};
      if (command.type.empty() || command.control_id.empty())
        throw std::invalid_argument("command requires type and control_id");
      const auto result = engine.apply_command(command, rbwr::Role::operator_role);
      status = result.ok ? 200 : 400;
      return "{\"ok\":" + std::string(result.ok ? "true" : "false") +
          ",\"req_id\":\"" + json_escape(result.req_id) + "\",\"code\":\"" +
          json_escape(result.code) + "\",\"detail\":\"" + json_escape(result.detail) +
          "\",\"hint\":\"" + json_escape(result.hint) + "\",\"new_value\":\"" +
          json_escape(result.new_value) + "\"}";
    }
    if (target == "/api/v1/sim/step") {
      const std::size_t colon = body.find(':');
      const double seconds = std::stod(body.substr(colon == std::string::npos ? 0 : colon + 1));
      if (seconds <= 0.0) throw std::invalid_argument("step must be positive");
      engine.step(seconds);
      return snapshot_json(engine);
    }
    const std::string scenario_prefix = "/api/v1/scenario/";
    if (target.starts_with(scenario_prefix) && target.ends_with("/load")) {
      const std::string name = target.substr(scenario_prefix.size(), target.size() - scenario_prefix.size() - 5);
      const bool ok = engine.load_scenario(name);
      status = ok ? 200 : 400;
      return "{\"ok\":" + std::string(ok ? "true" : "false") +
          ",\"scenario\":\"" + json_escape(engine.scenario_name()) + "\"}";
    }
    const std::string slot_prefix = "/api/v1/slots/";
    if (target.starts_with(slot_prefix)) {
      const std::string suffix = target.substr(slot_prefix.size());
      const std::size_t slash = suffix.find('/');
      if (slash == std::string::npos) throw std::invalid_argument("slot operation missing");
      const int slot = std::stoi(suffix.substr(0, slash));
      const std::string operation = suffix.substr(slash + 1);
      const bool ok = operation == "save" ? engine.save_slot(slot) :
          operation == "load" ? engine.load_slot(slot) : false;
      status = ok ? 200 : 400;
      return "{\"ok\":" + std::string(ok ? "true" : "false") +
          ",\"slot\":" + std::to_string(slot) + ",\"operation\":\"" + operation + "\"}";
    }
  } catch (...) {
    status = 400;
    return "{\"ok\":false,\"code\":\"ERR_INVALID_VALUE\"}";
  }
  status = 404;
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  int port = 8765;
  if (argc == 3 && std::string(argv[1]) == "--port") port = std::stoi(argv[2]);
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) return 1;
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 8) < 0) {
    close(server);
    return 1;
  }
  rbwr::Engine engine;
  std::cout << "rbwr_http listening on 127.0.0.1:" << port << '\n';
  for (;;) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) continue;
    char buffer[4096]{};
    const ssize_t length = read(client, buffer, sizeof(buffer) - 1);
    const std::string raw_request(buffer, length > 0 ? static_cast<std::size_t>(length) : 0);
    std::istringstream request(raw_request);
    std::string method;
    std::string target;
    request >> method >> target;
    const std::size_t separator = raw_request.find("\r\n\r\n");
    const std::string request_body = separator == std::string::npos ? "" : raw_request.substr(separator + 4);
    int status = 200;
    const std::string body = method == "GET" ? response_for(target, engine) :
      method == "POST" ? post_response(target, request_body, engine, status) : std::string();
    if (body.empty() && status == 200) status = 404;
    const std::string status_text = status == 200 ? "OK" : "Not Found";
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
             << "Content-Type: application/json\r\nContent-Length: " << body.size()
             << "\r\nConnection: close\r\n\r\n" << body;
    const std::string serialized = response.str();
    write(client, serialized.data(), serialized.size());
    close(client);
  }
}
