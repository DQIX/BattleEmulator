#ifndef GERUNIKKU_SEARCH_CLI_H
#define GERUNIKKU_SEARCH_CLI_H
#include "GerunikkuSearch.h"
#include <ostream>
namespace gerunikku_search {
int runCli(int argc, char* argv[], const Player players[4]);
void printResult(const Result& result, std::uint64_t seed, std::ostream& stream);
Result runRequest(const Player players[4], std::uint64_t seed,
                  std::span<const std::int32_t> prefix, const Limits& limits,
                  std::ostream& stream);
}
#endif