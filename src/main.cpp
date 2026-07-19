
#include "first_app.hpp"
#include "beacon/benchmark_runner.hpp"

// std
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

bool usesHeadlessResearchRunner(lve::beacon::RenderTechnique technique) {
  using lve::beacon::RenderTechnique;
  return technique == RenderTechnique::CpuClusteredFixed ||
         technique == RenderTechnique::FixedClusterCostModel;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto config = lve::beacon::parseCommandLine(argc, argv);
    const auto executableName = std::filesystem::path{argv[0]}.stem();
    lve::beacon::applyExecutableDefaults(config, executableName.string());
    if (config.benchmark && usesHeadlessResearchRunner(config.technique)) {
      return lve::beacon::runResearchBenchmark(config);
    }
    lve::FirstApp app{config};
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
