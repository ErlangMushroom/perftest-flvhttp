#include <iostream>
#include <string>
#include "test_config.hh"
#include "test_arena.hh"

int main(int argc, char* argv[])
{
  TestConfig cfg(argc, argv);
  if (!cfg.IsReady()) {
    cfg.PrintHelp();
    return 1;
  }

  TestArena arena;
  arena.SetConfig(cfg);
  arena.Run();
  arena.PrintResult();

  return 0;
}
