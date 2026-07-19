#pragma once

#include "atlas/navigation/navigation.hpp"

#include <filesystem>
#include <memory>

namespace vulkax::atlas {

class LocalNavigationProvider final : public SearchProvider, public RouteProvider {
 public:
  explicit LocalNavigationProvider(const std::filesystem::path& dataset);
  ~LocalNavigationProvider();

  LocalNavigationProvider(LocalNavigationProvider&&) noexcept;
  LocalNavigationProvider& operator=(LocalNavigationProvider&&) noexcept;
  LocalNavigationProvider(const LocalNavigationProvider&) = delete;
  LocalNavigationProvider& operator=(const LocalNavigationProvider&) = delete;

  std::future<std::vector<SearchResult>> search(
      SearchRequest request) override;
  std::future<std::optional<SearchResult>> reverse(
      GeodeticPosition position,
      std::string locale) override;
  std::future<std::vector<RouteResult>> route(
      RouteRequest request) override;
  std::future<std::optional<GeodeticPosition>> mapMatch(
      std::vector<GeodeticPosition> trace,
      TravelMode mode) override;

  size_t placeCount() const;
  size_t nodeCount() const;
  size_t edgeCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace vulkax::atlas
