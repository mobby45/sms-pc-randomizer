// The generator's checks: every seed can be finished in every entrance mode, the plando and the
// exclusions are kept. Build and run:
//   g++ -std=c++17 -O1 -Isrc tests/generator_test.cpp -o generator_test && ./generator_test
#include <cstdio>
#include <cstring>

#include "generator.h"

using namespace gen;

static int failures = 0;
static void check(bool ok, const char* what, int mode, unsigned seed)
{
	if (!ok) {
		++failures;
		printf("FAIL %s (mode %d, seed %u)\n", what, mode, seed);
	}
}

static int classOf(int area)
{
	return (area >= 2 && area <= 9) || area == 0xD || area == 0xE ? 2 : (area >= 0x15 && area <= 0x33 ? 1 : 0);
}

int main()
{
	const unsigned kSeeds = 150;
	// 1. no plando: every mode gives a game, and keeps its pools
	for (int mode = 0; mode < 5; ++mode) {
		for (unsigned s = 1; s <= kSeeds; ++s) {
			Generator g;
			const Plan p = g.run(s, kTalking, true, false, true, true, true, true, true, true, mode);
			check(p.ok, "a game", mode, s);
			for (size_t i = 0; i < p.swaps.size(); ++i) {
				const int a = classOf(p.swaps[i].first >> 8), b = classOf(p.swaps[i].second >> 8);
				if (p.swaps[i].first == 0 || p.swaps[i].first == p.swaps[i].second)
					continue;
				if (mode == 1)
					check((a == 2) == (b == 2), "worlds with worlds", mode, s);
				if (mode == 2 || mode == 4)
					check((a == 1) == (b == 1), "secrets with secrets", mode, s);
				if (mode == 3)
					check(a == b, "each in its own pool", mode, s);
			}
		}
	}
	// 2. plando: an unlock and two entrances (the start too), in every mode, and the entrances alone with the levels in place
	Exclusions ex;
	ex.unlocks.push_back(std::make_pair(1, 1u << 0)); // double jump on Shine 1
	ex.entrances.push_back(std::make_pair((uint16_t)0x0200, (uint16_t)0x0504));
	for (int mode = 0; mode < 6; ++mode) {
		for (unsigned s = 1; s <= kSeeds; ++s) {
			Generator g;
			const Plan p = g.run(s, kTalking, true, false, false, true, false, true, false, false, mode, &ex);
			check(p.ok, "a game with the plando", mode, s);
			bool entrance = false;
			for (size_t i = 0; i < p.swaps.size(); ++i)
				entrance = entrance || (p.swaps[i].first == 0x0200 && p.swaps[i].second == 0x0504);
			check(entrance, "the plando's entrance", mode, s);
			check(p.unlock[1] == (1u << 0), "the plando's unlock", mode, s);
		}
	}
	// 3. exclusions: an entrance kept, a Shine without an unlock
	Exclusions keep;
	keep.keep.push_back(0x02FF); // every entrance of area 2
	keep.shines.push_back(0);
	for (unsigned s = 1; s <= kSeeds; ++s) {
		Generator g;
		const Plan p = g.run(s, kTalking, true, false, false, true, false, true, false, false, 0, &keep);
		if (!p.ok)
			continue; // too many exclusions may leave no game; the walk's own limit
		for (size_t i = 0; i < p.swaps.size(); ++i) {
			if ((p.swaps[i].first >> 8) == 2 && p.swaps[i].first != 0)
				check(p.swaps[i].first == p.swaps[i].second, "an entrance kept", 0, s);
		}
		check(p.unlock[0] == 0, "no unlock on an excluded Shine", 0, s);
	}
	// 4. two entrances to one level is no game
	Exclusions two;
	two.entrances.push_back(std::make_pair((uint16_t)0x0200, (uint16_t)0x0504));
	two.entrances.push_back(std::make_pair((uint16_t)0x0300, (uint16_t)0x0504));
	for (unsigned s = 1; s <= 20; ++s) {
		Generator g;
		check(!g.run(s, kTalking, true, false, false, true, false, true, false, false, 0, &two).ok, "two entrances to one level", 0, s);
	}
	printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);
	return failures ? 1 : 0;
}
