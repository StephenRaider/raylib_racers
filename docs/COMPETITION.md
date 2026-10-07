# Running a competition

How to run Raylib Racers as a contest between robots written by different
people: the robots run sandboxed, with a CPU budget, through race weekends.

## 1. Collect source, build it yourself

Ask for source code, not compiled libraries. Put each entry in its own folder,
`bots/<team>/` (one or more `.c`/`.cpp` files plus anything from
`bots/common/`), and build:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Every folder under `bots/` becomes `build/bots/<team>.so` (`.dll` on
Windows) with no CMake edits. Read the code for anything that has no place in a
robot (threads, files, sockets, `system()`); the sandbox blocks those anyway.

## 2. Race with the competition rules

```
rr_race --sandbox --cpu-cap 2 --practice 15 --qualifying --track spa --laps 20 \
        --car team_a --car team_b ...
```

- `--sandbox` runs every car's robot in its own process (`rr_bothost`, built
  next to `rr_race`). A robot that crashes or hangs (no answer within 2 s, or
  200 times the CPU cap) retires its car with "robot crashed" or "robot hung";
  the race goes on. The robot cannot open files or sockets, start programs or
  threads, or write anything:
  - **Linux:** a seccomp filter allows only memory, time and its two pipes;
    everything else fails with EPERM. Strongest.
  - **Windows:** the process runs at low integrity inside a job: no child
    processes, 2 GB of memory, no writing to the user's files, no clipboard or
    desktop. It can still read files the user can read, so read the source.
  - **macOS:** its own process (crashes and hangs are contained), no filter.
- `--cpu-cap MS` limits the CPU one `drive()` call may use (wall time on
  Windows). A later answer is ignored, so the car keeps its previous controls,
  and a car over the cap more than 50 times is retired "over the CPU limit".
  The example robots use about 0.002 ms on average and under 0.6 ms at worst,
  so 2 ms is generous. `create()` is not capped beyond the 10 s hang limit:
  plan there.
- `--practice 15 --qualifying` runs the weekend: each car practises alone (up
  to 15 laps, any tyres), then qualifies alone, then the race starts in
  qualifying order. Robots keep notes from session to session only in their
  weekend memory, which is wiped after the race (see ROBOTS.md, "Race
  weekends").

Results with the robots' CPU use (`cpu_avg_ms`, `cpu_max_ms`, `cpu_overruns`
per car) go to `--json FILE`.

Races are deterministic: the same robots, track and settings give the same
result in or out of the sandbox, as long as every robot stays under the CPU
cap. Only a robot that runs close to the cap can see a different result on a
busier machine.

## 3. A championship

```
rr_race --championship season.json --lineup teams.json --distance 100 \
        --sandbox --cpu-cap 2 --practice 15 --qualifying
rr_race --championship season.json --all-rounds
```

A new season stores the rules (`sandbox`, `cpu_cap_ms`, `practice_laps`,
`qualifying`) in its file, so every later round runs under them. The viewer
does the same for seasons started while it runs with `--sandbox --cpu-cap MS`.
