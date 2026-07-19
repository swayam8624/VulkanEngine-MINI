include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

cmake_minimum_required(VERSION 3.11.0)

set(NAME LveEngine)

message(STATUS "using ${CMAKE_GENERATOR}")
if (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    if (NOT MINGW_PATH) 
      message(FATAL_ERROR "MINGW_PATH not set in .env.cmake")
    endif()
    set(USE_MINGW "True")
    set(CMAKE_C_COMPILER ${MINGW_PATH}/bin/gcc.exe)
    set(CMAKE_CXX_COMPILER  ${MINGW_PATH}/bin/g++.exe)
endif()

project(${NAME} VERSION 0.24.0)

# 1. Set VULKAN_SDK_PATH in .env.cmake to target specific vulkan version
if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include") # 1.1 Make sure this include path is correct
  set(Vulkan_LIBRARIES "${VULKAN_SDK_PATH}/Lib") # 1.2 Make sure lib path is correct
  set(Vulkan_FOUND "True")
else()
  find_package(Vulkan REQUIRED) # throws error if could not find Vulkan
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
endif()
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(glm REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
if (TARGET SQLite3::SQLite3)
  set(ATLAS_SQLITE_TARGET SQLite3::SQLite3)
elseif (TARGET SQLite::SQLite3)
  set(ATLAS_SQLITE_TARGET SQLite::SQLite3)
else()
  add_library(atlas_sqlite_imported INTERFACE)
  target_include_directories(
    atlas_sqlite_imported INTERFACE ${SQLite3_INCLUDE_DIRS}
  )
  target_link_libraries(
    atlas_sqlite_imported INTERFACE ${SQLite3_LIBRARIES}
  )
  set(ATLAS_SQLITE_TARGET atlas_sqlite_imported)
endif()
if (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
else()
	message(STATUS "Using vulkan lib at: ${Vulkan_LIBRARIES}")
endif()


# 2. Set GLFW_PATH in .env.cmake to target specific glfw
if (DEFINED GLFW_PATH)
  message(STATUS "Using GLFW path specified in .env")
  set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
  if (MSVC)
    set(GLFW_LIB "${GLFW_PATH}/lib-vc2019") # 2.1 Update lib-vc2019 to use same version as your visual studio
  elseif (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    message(STATUS "USING MINGW")
    set(GLFW_LIB "${GLFW_PATH}/lib-mingw-w64") # 2.1 make sure matches glfw mingw subdirectory
  endif()
else()
  find_package(glfw3 3.3 REQUIRED)
  set(GLFW_LIB glfw)
  message(STATUS "Found GLFW")
endif()
if (NOT GLFW_LIB)
	message(FATAL_ERROR "Could not find glfw library!")
else()
	message(STATUS "Using glfw lib at: ${GLFW_LIB}")
endif()

include_directories(external)

# If TINYOBJ_PATH not specified in .env.cmake, try fetching from git repo
if (NOT TINYOBJ_PATH)
  message(STATUS "TINYOBJ_PATH not specified in .env.cmake, using external/tinyobjloader")
  set(TINYOBJ_PATH external/tinyobjloader)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "/src/atlas/")

add_library(
  atlas_core STATIC
  src/atlas/core/geodesy.cpp
  src/atlas/core/sha256.cpp
  src/atlas/core/tile_key.cpp
  src/atlas/core/dataset.cpp
)
target_compile_features(atlas_core PUBLIC cxx_std_20)
target_include_directories(atlas_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_core PUBLIC glm::glm nlohmann_json::nlohmann_json
)

add_library(
  atlas_network STATIC
  src/atlas/streaming/tile_source.cpp
)
target_compile_features(atlas_network PUBLIC cxx_std_20)
target_include_directories(atlas_network PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_network PUBLIC atlas_core CURL::libcurl)

add_library(
  atlas_navigation STATIC
  src/atlas/navigation/replay_providers.cpp
  src/atlas/navigation/gateway_provider.cpp
)
target_compile_features(atlas_navigation PUBLIC cxx_std_20)
target_include_directories(atlas_navigation PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_navigation
  PUBLIC atlas_core atlas_network nlohmann_json::nlohmann_json
)

add_library(
  atlas_research STATIC
  src/atlas/research/route_predictive_scheduler.cpp
)
target_compile_features(atlas_research PUBLIC cxx_std_20)
target_include_directories(atlas_research PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_research PUBLIC atlas_core atlas_navigation)

add_library(
  atlas_renderer STATIC
  src/atlas/renderer/globe_mesh.cpp
  src/atlas/renderer/route_mesh.cpp
)
target_compile_features(atlas_renderer PUBLIC cxx_std_20)
target_include_directories(atlas_renderer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_renderer PUBLIC atlas_core atlas_navigation)

add_library(
  atlas_streaming STATIC
  src/atlas/atlas_runtime.cpp
  src/atlas/streaming/tile_cache.cpp
  src/atlas/streaming/regional_pack.cpp
  src/atlas/streaming/globe_selector.cpp
)
target_compile_features(atlas_streaming PUBLIC cxx_std_20)
target_include_directories(atlas_streaming PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_streaming
  PUBLIC atlas_core atlas_network atlas_navigation atlas_research
         ${ATLAS_SQLITE_TARGET}
)

add_library(VulkaxAtlasCore INTERFACE)
target_link_libraries(
  VulkaxAtlasCore
  INTERFACE atlas_core atlas_network atlas_navigation atlas_research
            atlas_renderer atlas_streaming
)

add_executable(VulkaxAtlasBuilder tools/atlas_build.cpp)
set_target_properties(VulkaxAtlasBuilder PROPERTIES OUTPUT_NAME atlas-build)
target_compile_features(VulkaxAtlasBuilder PRIVATE cxx_std_20)
target_link_libraries(VulkaxAtlasBuilder PRIVATE VulkaxAtlasCore)

add_executable(VulkaxAtlasBenchmark tools/atlas_benchmark.cpp)
set_target_properties(VulkaxAtlasBenchmark PROPERTIES OUTPUT_NAME atlas-benchmark)
target_compile_features(VulkaxAtlasBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxAtlasBenchmark PRIVATE VulkaxAtlasCore)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_compile_definitions(${PROJECT_NAME} PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(${PROJECT_NAME} glm::glm VulkaxAtlasCore)

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

if (WIN32)
  message(STATUS "CREATING BUILD FOR WINDOWS")

  if (USE_MINGW)
    target_include_directories(${PROJECT_NAME} PUBLIC
      ${MINGW_PATH}/include
    )
    target_link_directories(${PROJECT_NAME} PUBLIC
      ${MINGW_PATH}/lib
    )
  endif()

  target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${TINYOBJ_PATH}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_PATH}
    )

  target_link_directories(${PROJECT_NAME} PUBLIC
    ${Vulkan_LIBRARIES}
    ${GLFW_LIB}
  )

  target_link_libraries(${PROJECT_NAME} glfw3 vulkan-1 nlohmann_json::nlohmann_json)
elseif (UNIX)
    message(STATUS "CREATING BUILD FOR UNIX")
    target_include_directories(${PROJECT_NAME} PUBLIC
      ${PROJECT_SOURCE_DIR}/src
      ${TINYOBJ_PATH}
    )
    target_link_libraries(${PROJECT_NAME} glfw ${Vulkan_LIBRARIES} nlohmann_json::nlohmann_json)
endif()


############## Build SHADERS #######################

# Find all vertex and fragment sources within shaders directory
# taken from VBlancos vulkan tutorial
# https://github.com/vblanco20-1/vulkan-guide/blob/all-chapters/CMakeLists.txt
find_program(GLSL_VALIDATOR glslangValidator HINTS 
  ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} 
  /usr/bin 
  /usr/local/bin 
  ${VULKAN_SDK_PATH}/Bin
  ${VULKAN_SDK_PATH}/Bin32
  $ENV{VULKAN_SDK}/Bin/ 
  $ENV{VULKAN_SDK}/Bin32/
)

# get all .vert and .frag files in shaders directory
file(GLOB_RECURSE GLSL_SOURCE_FILES
  "${PROJECT_SOURCE_DIR}/shaders/*.comp"
  "${PROJECT_SOURCE_DIR}/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/*.vert"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
)

add_dependencies(${PROJECT_NAME} Shaders)
add_custom_target(atlas_app_desktop DEPENDS ${PROJECT_NAME})
add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:${PROJECT_NAME}>
          $<TARGET_FILE_DIR:${PROJECT_NAME}>/VulkaxAtlas
  COMMENT "Creating the VulkaxAtlas application entrypoint"
)

include(CTest)
if (BUILD_TESTING)
  add_executable(
    BeaconCoreTests
    tests/beacon_core_tests.cpp
    src/beacon/beacon_research.cpp
    src/beacon/cluster_scan_reference.cpp
  )
  target_compile_features(BeaconCoreTests PRIVATE cxx_std_17)
  target_include_directories(BeaconCoreTests PRIVATE ${PROJECT_SOURCE_DIR}/src ${TINYOBJ_PATH})
  target_link_libraries(BeaconCoreTests PRIVATE glm::glm)
  add_test(NAME beacon_core COMMAND BeaconCoreTests)

  add_executable(AtlasCoreTests tests/atlas_core_tests.cpp)
  target_compile_features(AtlasCoreTests PRIVATE cxx_std_20)
  target_compile_definitions(
    AtlasCoreTests PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/"
  )
  target_link_libraries(AtlasCoreTests PRIVATE VulkaxAtlasCore)
  add_test(NAME atlas_core COMMAND AtlasCoreTests)

  set(ATLAS_TEST_OUTPUT ${CMAKE_BINARY_DIR}/atlas-test-output)
  add_test(
    NAME atlas_manifest_generate
    COMMAND
      VulkaxAtlasBuilder generate-manifest
      ${PROJECT_SOURCE_DIR}/config/atlas/regions/delhi-ncr.json
      ${ATLAS_TEST_OUTPUT}/atlas-dataset.json
  )
  add_test(
    NAME atlas_manifest_validate
    COMMAND
      VulkaxAtlasBuilder validate
      ${ATLAS_TEST_OUTPUT}/atlas-dataset.json
  )
  set_tests_properties(
    atlas_manifest_validate PROPERTIES DEPENDS atlas_manifest_generate
  )
  add_test(
    NAME atlas_pack_geobeacon
    COMMAND
      VulkaxAtlasBuilder pack-geobeacon
      ${PROJECT_SOURCE_DIR}/data/connaught_place/generated/geobeacon.json
      ${ATLAS_TEST_OUTPUT}/connaught-place.vxa
  )

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME geobeacon_tools
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_geobeacon_tools.py
  )
  add_test(
    NAME atlas_gateway
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_atlas_gateway.py
  )
  add_test(
    NAME atlas_reference_manifests
    COMMAND
      ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/test_atlas_manifests.py
      $<TARGET_FILE:VulkaxAtlasBuilder>
  )
  add_test(
    NAME atlas_research_benchmark
    COMMAND
      ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/test_atlas_benchmark.py
      $<TARGET_FILE:VulkaxAtlasBenchmark>
  )
endif()
