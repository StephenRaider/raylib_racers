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
  src/core/config.cpp
  src/core/spec.cpp
  src/core/testlog.cpp)
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
  # Every folder under bots/ (except common/) with .c/.cpp files is a robot named
  # after the folder. CONFIGURE_DEPENDS re-runs the scan when files are added.
  file(GLOB RR_BOT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/bots/*/*.c ${CMAKE_CURRENT_SOURCE_DIR}/bots/*/*.cpp)
  set(RR_BOTS "")
  foreach(src ${RR_BOT_SOURCES})
    get_filename_component(dir "${src}" DIRECTORY)
    get_filename_component(bot "${dir}" NAME)
    if(NOT bot STREQUAL "common" AND NOT bot IN_LIST RR_BOTS)
      list(APPEND RR_BOTS ${bot})
    endif()
  endforeach()
  foreach(bot ${RR_BOTS})
    file(GLOB src CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/bots/${bot}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/bots/${bot}/*.cpp)
    rr_add_robot(${bot} ${src})
    if(NOT MSVC)
      target_link_libraries(${bot} PRIVATE m)
    endif()
  endforeach()
  message(STATUS "Robots: ${RR_BOTS}")
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
    apps/viewer/car_model.cpp
    apps/viewer/engine_sound.cpp
    apps/viewer/race_audio.cpp
    apps/viewer/menu.cpp
    apps/viewer/hud_test.cpp
    apps/viewer/test_figures.cpp
    apps/viewer/liveries.cpp)
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
  add_test(NAME race_test_log COMMAND rr_race --laps 2 --quiet --car racingline --no-pits --fuel 8 --tires soft
           --test-log ${CMAKE_BINARY_DIR}/ctest_test_runs)
  add_test(NAME race_pit_stop COMMAND rr_race --laps 6 --wear-rate 6 --quiet --car racingline)
  set_tests_properties(race_pit_stop PROPERTIES PASS_REGULAR_EXPRESSION "racingline +[0-9]+:[0-9.]+ .* [1-9] \\(L[0-9]")
  # After the flag every car drives into the pit lane and parks.
  add_test(NAME race_cool_down COMMAND rr_race --laps 1 --quiet --cool-down
           --car racingline --car racingline --car racingline --car gapfollow --car simple)
  set_tests_properties(race_cool_down PROPERTIES PASS_REGULAR_EXPRESSION "cool-down: 5/5 cars parked")
  # Asymmetric cars: a spec file and development tokens load; an over-budget car is refused.
  add_test(NAME race_specs COMMAND rr_race --laps 1 --quiet --car racingline --spec f1_2006
           --car racingline --dev "top_speed=8,downforce=2,pit_stop=5")
  add_test(NAME race_dev_budget COMMAND rr_race --laps 1 --quiet --car racingline --dev "top_speed=10,downforce=10,handling=10")
  set_tests_properties(race_dev_budget PROPERTIES WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------- install
install(TARGETS rr_race RUNTIME DESTINATION .)
if(RR_BUILD_VIEWER)
  install(TARGETS rr_viewer RUNTIME DESTINATION .)
endif()
if(RR_BUILD_BOTS)
  install(TARGETS ${RR_BOTS} LIBRARY DESTINATION bots RUNTIME DESTINATION bots)
endif()
install(DIRECTORY tracks assets specs DESTINATION .)
