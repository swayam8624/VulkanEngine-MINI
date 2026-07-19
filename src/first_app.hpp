#pragma once

#include "atlas/core/geodesy.hpp"
#include "beacon/benchmark_config.hpp"
#include "lve_descriptors.hpp"
#include "lve_device.hpp"
#include "lve_game_object.hpp"
#include "lve_renderer.hpp"
#include "lve_window.hpp"

// std
#include <memory>
#include <vector>

namespace lve {
class FirstApp {
 public:
  static constexpr int WIDTH = 800;
  static constexpr int HEIGHT = 600;

  explicit FirstApp(beacon::BenchmarkConfig config = {});
  ~FirstApp();

  FirstApp(const FirstApp &) = delete;
  FirstApp &operator=(const FirstApp &) = delete;

  void run();

 private:
  void loadGameObjects();
  void loadTutorialScene();
  void loadGeneratedScene();
  void loadGeoLights();
  void loadAtlasScene();

  beacon::BenchmarkConfig config{};
  LveWindow lveWindow;
  LveDevice lveDevice{lveWindow, config};
  LveRenderer lveRenderer{lveWindow, lveDevice};

  // note: order of declarations matters
  std::unique_ptr<LveDescriptorPool> globalPool{};
  LveGameObject::Map gameObjects;
  std::vector<vulkax::atlas::GeodeticPosition> atlasRouteShape;
};
}  // namespace lve
