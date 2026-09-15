#ifndef GERUNIKKU_SEARCH_CLI_H
#define GERUNIKKU_SEARCH_CLI_H
#include "GerunikkuSearch.h"
#include <ostream>
namespace gerunikku_search {
int runCli(int argc, char* argv[], const Player players[4]);
void printResult(const Result& result, std::uint64_t seed, std::ostream& stream);
}
#endif