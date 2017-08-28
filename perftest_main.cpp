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

  boost::shared_ptr<SessionSum> sumup(new SessionSum());
  TestArena arena(sumup);
  arena.SetConfig(cfg);
  arena.Run();

  sumup->PrintSummary();
  if (cfg.Detailed()) {
    sumup->WriteToCSV();
  }

  return 0;
}
