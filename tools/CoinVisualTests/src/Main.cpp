#include "CompareCommand.h"
#include "RunCommand.h"
#include "SnapshotCommand.h"

#include <Inventor/SoDB.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
  std::cout << "Usage: CoinVisualTests <subcommand> [args]\n"
            << "Subcommands:\n"
            << "  snapshot   Render a scene or spec to PNG.\n"
            << "  compare    Compare two images with tolerances.\n"
            << "  run        Run the full spec-based suite.\n"
            << "  update     Update baselines (alias for: run --update-baselines).\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const std::string cmd = argv[1];
  if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    print_usage();
    return 0;
  }

  const bool uses_coin = cmd == "snapshot" || cmd == "run" || cmd == "update";
  if (uses_coin) {
    SoDB::init();
  }

  int result = 1;
  if (cmd == "snapshot") {
    result = CoinVisualTests::RunSnapshotCommand(argc - 1, argv + 1);
  }
  else if (cmd == "compare") {
    result = CoinVisualTests::RunCompareCommand(argc - 1, argv + 1);
  }
  else if (cmd == "run") {
    result = CoinVisualTests::RunRunCommand(argc - 1, argv + 1);
  }
  else if (cmd == "update") {
    std::vector<const char*> args;
    args.reserve(static_cast<size_t>(argc) + 1);
    args.push_back("run");
    args.push_back("--update-baselines");
    for (int i = 2; i < argc; ++i) {
      args.push_back(argv[i]);
    }
    result = CoinVisualTests::RunRunCommand(static_cast<int>(args.size()), args.data());
  }
  else {
    std::cerr << "Unknown subcommand: " << cmd << '\n';
    print_usage();
  }

  if (uses_coin) {
    SoDB::finish();
  }
  return result;
}
