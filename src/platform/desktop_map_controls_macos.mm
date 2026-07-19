#include "platform/desktop_map_controls.hpp"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>

#include <deque>
#include <mutex>
#include <utility>

static vulkax::atlas::TravelMode selectedMode(NSPopUpButton* popup) {
  switch (popup.indexOfSelectedItem) {
    case 0: return vulkax::atlas::TravelMode::Walking;
    case 1: return vulkax::atlas::TravelMode::Driving;
    case 2: return vulkax::atlas::TravelMode::Cycling;
    default: return vulkax::atlas::TravelMode::Walking;
  }
}

namespace lve {

struct DesktopMapControls::Impl {
  std::mutex mutex;
  std::deque<DesktopMapAction> actions;
  NSVisualEffectView* panel = nil;
  NSTextField* title = nil;
  NSPopUpButton* cities = nil;
  NSSearchField* search = nil;
  NSPopUpButton* results = nil;
  NSPopUpButton* mode = nil;
  NSTextField* status = nil;
  NSTextField* routeSummary = nil;
  NSObject* target = nil;

  void enqueue(DesktopMapAction action) {
    std::scoped_lock lock{mutex};
    actions.push_back(std::move(action));
  }
};

namespace {

NSString* nsString(const std::string& value) {
  return [[NSString alloc]
      initWithBytes:value.data()
             length:value.size()
           encoding:NSUTF8StringEncoding];
}

NSTextField* label(NSString* text, NSFont* font) {
  NSTextField* field = [NSTextField labelWithString:text];
  field.font = font;
  field.textColor = NSColor.labelColor;
  field.lineBreakMode = NSLineBreakByWordWrapping;
  field.maximumNumberOfLines = 3;
  return field;
}

}  // namespace

}  // namespace lve

@interface VulkaxMapControlTarget : NSObject
@property(nonatomic, assign) void* owner;
- (void)cityPressed:(id)sender;
- (void)searchPressed:(id)sender;
- (void)routePressed:(id)sender;
- (void)followPressed:(id)sender;
- (void)clearPressed:(id)sender;
@end

@implementation VulkaxMapControlTarget
- (lve::DesktopMapControls::Impl*)impl {
  return static_cast<lve::DesktopMapControls::Impl*>(self.owner);
}

- (void)cityPressed:(id)sender {
  auto* state = [self impl];
  NSMenuItem* selected = state->cities.selectedItem;
  if (selected == nil || selected.representedObject == nil) return;
  lve::DesktopMapAction action{};
  action.kind = lve::DesktopMapActionKind::SwitchCity;
  action.cityId =
      [static_cast<NSString*>(selected.representedObject) UTF8String] ?: "";
  state->enqueue(std::move(action));
}

- (void)searchPressed:(id)sender {
  auto* state = [self impl];
  lve::DesktopMapAction action{};
  action.kind = lve::DesktopMapActionKind::Search;
  action.query = state->search.stringValue.UTF8String ?: "";
  state->enqueue(std::move(action));
}

- (void)routePressed:(id)sender {
  auto* state = [self impl];
  NSMenuItem* selected = state->results.selectedItem;
  if (selected == nil || selected.representedObject == nil) return;
  lve::DesktopMapAction action{};
  action.kind = lve::DesktopMapActionKind::Route;
  action.destinationId =
      [static_cast<NSString*>(selected.representedObject) UTF8String] ?: "";
  action.mode = selectedMode(state->mode);
  state->enqueue(std::move(action));
}

- (void)followPressed:(id)sender {
  lve::DesktopMapAction action{};
  action.kind = lve::DesktopMapActionKind::FollowRoute;
  [self impl]->enqueue(std::move(action));
}

- (void)clearPressed:(id)sender {
  lve::DesktopMapAction action{};
  action.kind = lve::DesktopMapActionKind::ClearRoute;
  [self impl]->enqueue(std::move(action));
}
@end

namespace lve {

DesktopMapControls::DesktopMapControls(GLFWwindow* window, std::string mapName)
    : impl{std::make_unique<Impl>()} {
  NSWindow* nativeWindow = glfwGetCocoaWindow(window);
  NSView* content = nativeWindow.contentView;
  if (content == nil) return;

  impl->panel = [[NSVisualEffectView alloc]
      initWithFrame:NSMakeRect(16.0, content.bounds.size.height - 376.0, 340.0, 360.0)];
  impl->panel.material = NSVisualEffectMaterialSidebar;
  impl->panel.blendingMode = NSVisualEffectBlendingModeWithinWindow;
  impl->panel.state = NSVisualEffectStateActive;
  impl->panel.wantsLayer = YES;
  impl->panel.layer.cornerRadius = 8.0;
  impl->panel.layer.masksToBounds = YES;
  impl->panel.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;

  NSStackView* stack = [[NSStackView alloc] initWithFrame:NSMakeRect(16, 14, 308, 332)];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 10.0;
  stack.distribution = NSStackViewDistributionGravityAreas;
  stack.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  impl->title =
      label(nsString(mapName), [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold]);
  NSTextField* subtitle =
      label(@"Offline search and road navigation", [NSFont systemFontOfSize:12]);
  subtitle.textColor = NSColor.secondaryLabelColor;

  impl->cities =
      [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 230, 28) pullsDown:NO];
  impl->cities.accessibilityLabel = @"Installed city";
  NSButton* cityButton = [NSButton buttonWithTitle:@"Open"
                                            target:nil
                                            action:nil];
  cityButton.bezelStyle = NSBezelStyleRounded;
  cityButton.accessibilityLabel = @"Open selected city";
  NSStackView* cityRow =
      [NSStackView stackViewWithViews:@[impl->cities, cityButton]];
  cityRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  cityRow.spacing = 8.0;

  impl->search = [[NSSearchField alloc] initWithFrame:NSMakeRect(0, 0, 308, 28)];
  impl->search.placeholderString = @"Search places, roads, metro gates";
  impl->search.accessibilityLabel =
      [NSString stringWithFormat:@"Search %@", nsString(mapName)];

  NSButton* searchButton = [NSButton buttonWithTitle:@"Search"
                                              target:nil
                                              action:nil];
  searchButton.bezelStyle = NSBezelStyleRounded;
  searchButton.accessibilityLabel = @"Search places";

  impl->results =
      [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 308, 28) pullsDown:NO];
  impl->results.accessibilityLabel = @"Search results";
  impl->mode =
      [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 144, 28) pullsDown:NO];
  [impl->mode addItemsWithTitles:@[@"Walking", @"Driving", @"Cycling"]];
  impl->mode.accessibilityLabel = @"Travel mode";

  NSButton* routeButton = [NSButton buttonWithTitle:@"Route"
                                             target:nil
                                             action:nil];
  routeButton.bezelStyle = NSBezelStyleRounded;
  routeButton.accessibilityLabel = @"Create route";
  NSButton* followButton = [NSButton buttonWithTitle:@"Follow"
                                              target:nil
                                              action:nil];
  followButton.bezelStyle = NSBezelStyleRounded;
  followButton.accessibilityLabel = @"Follow current route";
  NSButton* clearButton = [NSButton buttonWithTitle:@"Clear"
                                             target:nil
                                             action:nil];
  clearButton.bezelStyle = NSBezelStyleRounded;
  clearButton.accessibilityLabel = @"Clear current route";

  NSStackView* searchRow =
      [NSStackView stackViewWithViews:@[impl->search, searchButton]];
  searchRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  searchRow.spacing = 8.0;
  NSStackView* routeRow =
      [NSStackView stackViewWithViews:@[impl->mode, routeButton, followButton, clearButton]];
  routeRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  routeRow.spacing = 6.0;

  impl->routeSummary =
      label(@"Choose a destination to create a route.", [NSFont systemFontOfSize:12]);
  impl->routeSummary.textColor = NSColor.secondaryLabelColor;
  impl->status =
      label(@"Loading local map…", [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]);
  impl->status.textColor = NSColor.secondaryLabelColor;

  for (NSView* view in @[
           impl->title,
           subtitle,
           cityRow,
           searchRow,
           impl->results,
           routeRow,
           impl->routeSummary,
           impl->status,
  ]) {
    [stack addArrangedSubview:view];
  }
  [[cityRow.widthAnchor constraintEqualToConstant:308] setActive:YES];
  [[impl->cities.widthAnchor constraintEqualToConstant:230] setActive:YES];
  [[searchRow.widthAnchor constraintEqualToConstant:308] setActive:YES];
  [[impl->search.widthAnchor constraintEqualToConstant:230] setActive:YES];
  [[impl->results.widthAnchor constraintEqualToConstant:308] setActive:YES];
  [[routeRow.widthAnchor constraintEqualToConstant:308] setActive:YES];
  [[impl->routeSummary.widthAnchor constraintEqualToConstant:308] setActive:YES];
  [[impl->status.widthAnchor constraintEqualToConstant:308] setActive:YES];

  VulkaxMapControlTarget* target = [[VulkaxMapControlTarget alloc] init];
  target.owner = impl.get();
  impl->target = target;
  cityButton.target = target;
  cityButton.action = @selector(cityPressed:);
  searchButton.target = target;
  searchButton.action = @selector(searchPressed:);
  impl->search.target = target;
  impl->search.action = @selector(searchPressed:);
  routeButton.target = target;
  routeButton.action = @selector(routePressed:);
  followButton.target = target;
  followButton.action = @selector(followPressed:);
  clearButton.target = target;
  clearButton.action = @selector(clearPressed:);

  [impl->panel addSubview:stack];
  [content addSubview:impl->panel positioned:NSWindowAbove relativeTo:nil];
}

DesktopMapControls::~DesktopMapControls() {
  [impl->panel removeFromSuperview];
  impl->target = nil;
}

bool DesktopMapControls::available() const {
  return impl->panel != nil;
}

std::optional<DesktopMapAction> DesktopMapControls::pollAction() {
  std::scoped_lock lock{impl->mutex};
  if (impl->actions.empty()) return std::nullopt;
  auto action = std::move(impl->actions.front());
  impl->actions.pop_front();
  return action;
}

void DesktopMapControls::setMapName(const std::string& mapName) {
  impl->title.stringValue = nsString(mapName);
  impl->search.accessibilityLabel =
      [NSString stringWithFormat:@"Search %@", nsString(mapName)];
}

void DesktopMapControls::setCities(
    const std::vector<DesktopCityOption>& cities) {
  [impl->cities removeAllItems];
  NSInteger selectedIndex = -1;
  for (size_t index = 0; index < cities.size(); ++index) {
    const auto& city = cities[index];
    NSString* title = [NSString
        stringWithFormat:@"%@ · %@",
                         nsString(city.displayName),
                         nsString(city.status)];
    [impl->cities addItemWithTitle:title];
    impl->cities.lastItem.representedObject = nsString(city.id);
    if (city.selected) selectedIndex = static_cast<NSInteger>(index);
  }
  if (selectedIndex >= 0) {
    [impl->cities selectItemAtIndex:selectedIndex];
  }
}

void DesktopMapControls::setSearchQuery(const std::string& query) {
  impl->search.stringValue = nsString(query);
}

void DesktopMapControls::setSearchResults(
    const std::vector<vulkax::atlas::SearchResult>& results) {
  [impl->results removeAllItems];
  for (const auto& result : results) {
    NSString* title = nsString(result.name);
    [impl->results addItemWithTitle:title];
    impl->results.lastItem.representedObject = nsString(result.id);
  }
  if (results.empty()) {
    [impl->results addItemWithTitle:@"No matching places"];
    impl->results.lastItem.enabled = NO;
  }
}

void DesktopMapControls::setStatus(const std::string& status) {
  impl->status.stringValue = nsString(status);
}

void DesktopMapControls::setRouteSummary(
    const std::string& destination,
    double distanceMeters,
    double durationSeconds) {
  if (distanceMeters <= 0.0) {
    impl->routeSummary.stringValue = nsString(destination);
    return;
  }
  const int minutes = static_cast<int>(std::round(durationSeconds / 60.0));
  NSString* summary = [NSString
      stringWithFormat:@"%@ · %.0f m · %d min",
                       nsString(destination),
                       distanceMeters,
                       std::max(1, minutes)];
  impl->routeSummary.stringValue = summary;
}

}  // namespace lve
