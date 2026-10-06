cmake_minimum_required(VERSION 3.16)
project(raylib_racers VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RR_BUILD_VIEWER "Build the raylib 3D viewer (rr_viewer)" ON)
option(RR_BUILD_BOTS "Build the example robots" ON)

# Everything lands next to the executables: build/rr_race, build/bots/*.so
set(RR_OUT "${CMAKE_BINARY_DIR}")
function(rr_output target dir)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${dir}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${dir}>")
endfunction()

if(MSVC)
  set(RR_WARNINGS /W3)
else()
  set(RR_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- simulation core (no graphics)
add_library(rr_core STATIC
  src/core/track.cpp
  src/core/car.cpp
  src/core/race.cpp
  src/core/robot_loader.cpp
  src/core/config.cpp)
target_include_directories(rr_core PUBLIC include src/core)
target_compile_options(rr_core PRIVATE ${RR_WARNINGS})
target_compile_definitions(rr_core PUBLIC RR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(rr_core PUBLIC ${CMAKE_DL_LIBS})

add_executable(rr_race apps/headless/main.cpp)
target_link_libraries(rr_race PRIVATE rr_core)
target_compile_options(rr_race PRIVATE ${RR_WARNINGS})
rr_output(rr_race "${RR_OUT}")

# ---------------------------------------------------------------- example robots
function(rr_add_robot name)
  add_library(${name} MODULE ${ARGN})
  target_include_directories(${name} PRIVATE include)
  target_compile_options(${name} PRIVATE ${RR_WARNINGS})
  set_target_properties(${name} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  rr_output(${name} "${RR_OUT}/bots")
endfunction()

if(RR_BUILD_BOTS)
  rr_add_robot(simple bots/simple/simple.c)
  rr_add_robot(gapfollow bots/gapfollow/gapfollow.cpp)
  rr_add_robot(racingline bots/racingline/racingline.cpp)
  if(NOT MSVC)
    target_link_libraries(simple PRIVATE m)
  endif()
endif()

# ---------------------------------------------------------------- viewer
if(RR_BUILD_VIEWER)
  find_package(raylib 5.5 QUIET)
  if(NOT raylib_FOUND)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 5.5
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()

  add_executable(rr_viewer
    apps/viewer/main.cpp
    apps/viewer/renderer.cpp
    apps/viewer/hud.cpp
    apps/viewer/car_model.cpp)
  target_link_libraries(rr_viewer PRIVATE rr_core raylib)
  target_compile_options(rr_viewer PRIVATE ${RR_WARNINGS} $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>)
  rr_output(rr_viewer "${RR_OUT}")
  if(APPLE)
    target_link_libraries(rr_viewer PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
  endif()
endif()

# ---------------------------------------------------------------- tests
enable_testing()
if(RR_BUILD_BOTS)
  add_test(NAME race_smoke
    COMMAND rr_race --laps 1 --quiet --car simple --car gapfollow --car racingline
            --json ${CMAKE_BINARY_DIR}/smoke_results.json)
  add_test(NAME race_oval COMMAND rr_race --track oval --laps 1 --quiet --car racingline)
  # Fast tyre wear forces a stop: the racingline robot must pit and finish.
  add_test(NAME race_pit_stop COMMAND rr_race --laps 6 --wear-rate 6 --quiet --car racingline)
  set_tests_properties(race_pit_stop PROPERTIES PASS_REGULAR_EXPRESSION "racingline +[0-9]+:[0-9.]+ .* [1-9] \\(L[0-9]")
endif()

# ---------------------------------------------------------------- install
install(TARGETS rr_race RUNTIME DESTINATION .)
if(RR_BUILD_VIEWER)
  install(TARGETS rr_viewer RUNTIME DESTINATION .)
endif()
if(RR_BUILD_BOTS)
  install(TARGETS simple gapfollow racingline LIBRARY DESTINATION bots RUNTIME DESTINATION bots)
endif()
install(DIRECTORY tracks assets DESTINATION .)
