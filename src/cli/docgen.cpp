#include "rbwr/engine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void write_list(std::ofstream& output, const std::string& title,
                const std::vector<std::string>& values) {
  output << "## " << title << "\n\n| ID |\n|---|\n";
  for (const auto& value : values) output << "| `" << value << "` |\n";
  output << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  const std::string directory = argc > 1 ? argv[1] : "docs/GENERATED";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    std::cerr << "cannot create documentation directory: " << error.message() << '\n';
    return 1;
  }
  rbwr::Engine engine;
  std::ofstream output(std::filesystem::path(directory) / "registry_tables.md");
  if (!output) return 1;
  output << "# Generated RBWR Registry Tables\n\n"
         << "Generated from the live Engine registries.\n\n";
  write_list(output, "Tags", engine.tag_ids());
  write_list(output, "Controls", engine.control_ids());
  write_list(output, "Annunciators", engine.annunciator_ids());
  write_list(output, "Malfunctions", engine.malfunction_ids());
  write_list(output, "Components", engine.component_ids());
  std::cout << "generated " << (std::filesystem::path(directory) / "registry_tables.md") << '\n';
  return 0;
}
