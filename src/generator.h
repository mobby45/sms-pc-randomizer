// The randomizer's generator: for a seed, which Shine gives which unlock and which
// level is loaded in place of each one. It does what the randomizer module's own
// generator does, step for step and draw for draw (MSL's rand), so a seed gives the
// same game as the original randomizer.
//
// It walks the game from Delfino Plaza: each exit it can take is given a level drawn
// from those it could still enter, the level inheriting what that exit needs; when no
// exit is open, it gives an unlock that opens one, on a Shine already reachable; the
// unlocks left at the end go on reachable Shines too. So every seed can be finished.
//
// Without the shuffle (not something the randomizer has) the walk is the same, from
// Delfino Plaza, but each exit leads to its own level: the unlocks are then placed for
// the game's own map. Nothing in the Plaza can be had without an unlock, so Mario then
// has the spray nozzle from the start, as in the game.
//
// With sprayInWorlds (not something the randomizer has), every way out of Delfino
// Plaza and of the seven worlds, Hotel Delfino, Pinna Park and Casino Delfino needs the
// spray nozzle too: the goop and goop enemies on those entrances are then never asked
// to be cleared without it (the hover trick is not in the logic). The episodes' own
// Shines keep the randomizer's needs.
#ifndef RANDOMIZER_GENERATOR_H
#define RANDOMIZER_GENERATOR_H

#include <stdint.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace gen {

struct LevelExit {
	uint16_t to;     // the level it leads to (stage << 8 | scenario)
	uint8_t shines;  // Shines needed
	uint32_t need;   // restriction bits needed
};

struct LevelShine {
	uint16_t id;     // the Shine << 8 | Shines needed
	uint32_t need;
};

struct LevelData {
	uint16_t id;
	uint8_t flag;    // left out until every Shadow Mario's Shine is reachable
	uint16_t exitFirst, exitCount, shineFirst, shineCount;
};

#include "levels.inc"

const int kLevelCount = (int)(sizeof(kLevels) / sizeof(kLevels[0]));

// The skills, as the randomizer numbers them (its unlocks); talking (bit 10) is had
// from the start.
const uint32_t kTalking    = 1u << 10;
const uint32_t kAllSkills  = 0x7FFFFF;
const uint32_t kSpray      = 1u << 17;
const uint32_t kAllShadows = 0x10000; // restriction: every Shadow Mario's Shine

// What a set of skills meets (the module's restriction bits), and with `shadows` the
// Shadow Mario Shines reachable (7: all of them).
inline uint32_t restrictionsOf(uint32_t s, int shadows)
{
	uint32_t r = (s & 0x80005) ? 1 : 0;
	if ((s & 0x22) || (~s & 0x80001) == 0 || (~s & 0x80004) == 0)
		r |= 2;
	r |= ((s >> 1) & 0x8000) | ((s & 0x200) << 5) | ((s >> 10) & 0x1000) | ((s >> 4) & 0x800) | ((s & 8) << 7);
	r |= ((s & 0x40) << 3) | ((s >> 2) & 0x100) | ((s >> 14) & 0xC0) | ((s >> 2) & 0x20) | ((s >> 4) & 0x10);
	r |= ((s >> 16) & 8) | ((s >> 15) & 4) | ((s >> 1) & ((s & 8) << 10));
	if (shadows == 7)
		r |= 0x10000;
	return r;
}

inline bool shadowShine(int shine)
{
	switch (shine) {
	case 0x06: case 0x10: case 0x1A: case 0x24: case 0x2E: case 0x38: case 0x42: return true;
	}
	return false;
}

// With coinShines (not something the randomizer has), a world's 100-coin Shine (the
// stage's "シャイン（１００枚コイン用）", 100 to 107) is a place an unlock may go, in the
// episodes whose scene places 100 coins or more ("Coin" objects; the coins enemies,
// boxes and goop give are not counted, so the others are left out): Delfino Plaza
// (106), Ricco Harbor 2-8 (101-113), Gelato Beach 2 and 7 (100, 105), Noki Bay 1, 2,
// 4-8 (101-151). It needs what the way in needs, and the spray nozzle.
const uint32_t kCoinShineNeed = 4;

inline int coinShineOf(uint16_t level)
{
	switch (level) {
	case 0x108:
		return 107; // Delfino Plaza
	case 0x301: case 0x302: case 0x303: case 0x304: case 0x305: case 0x306: case 0x307:
		return 101; // Ricco Harbor
	case 0x401: case 0x406:
		return 102; // Gelato Beach
	case 0x900: case 0x901: case 0x903: case 0x904: case 0x905: case 0x906: case 0x907:
		return 105; // Noki Bay
	}
	return -1;
}

// With plazaExtra (the mod's; the randomizer leaves it out), one more of Delfino
// Plaza's Shines is a place an unlock may go: the one behind the wooden door broken
// with the turbo nozzle, by Yoshi's egg at the foot of a bell tower (116; restriction
// 0x80, the turbo).
const uint16_t kPlazaExtraShine = 116;
const uint32_t kPlazaExtraNeed  = 0x80;

// With tradeShines (not something the randomizer has), the Shines the boat's raccoons
// trade for blue coins hold unlocks too. The k-th trade (10 k blue coins) gives the
// first of these not had (the script manievent.sb runs through them in this order).
// Counted, so that the logic never asks too much: the blue coins each reachable
// level's scene places, the most of any one episode for each area, with the spray
// nozzle (many are under goop); hidden ones, and those an action gives, are not.
const uint8_t kTradeShines[24] = { 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
	                               82, 83, 84, 85, 108, 109, 110, 111, 112, 113, 114, 115 };

// The blue coins counted for the levels reached (by kLevels index).
inline int blueCoinsReached(const std::vector<char>& reached, uint32_t skills)
{
	if (!(skills & (1u << 17)))
		return 0; // the spray nozzle
	int best[256] = {};
	for (int i = 0; i < kLevelCount; ++i) {
		const int area = kLevels[i].id >> 8;
		if (reached[i] && kBlueCoins[i] > best[area])
			best[area] = kBlueCoins[i];
	}
	int n = 0;
	for (int a = 0; a < 256; ++a)
		n += best[a];
	return n;
}

// With apLogic (not the randomizer's), each Shine also needs what the SMS apworld's
// normal logic asks of it (ap.inc, from Joshark/archipelago-sms): any one of its
// alternatives, each the nozzles of one of its sets (or none), Shines had, another
// Shine or blue coin reachable, Corona Mountain open. The apworld's 240 blue coins are
// places too (1000 + their index in kApCoins), in the levels whose scenes have them
// (placed, or behind a hidden object with their number; those an animal, enemy or
// NPC gives, where that kind is: ap_gen.py): with coinUnlocks, an unlock may go on
// one, and they are what a location may ask for.
struct ApAlt {
	uint8_t ors[6];  // nozzle sets: 1 spray, 2 hover, 4 rocket, 8 turbo, 16 Yoshi
	uint8_t orCount; // 0: no nozzle needed
	uint8_t shines;  // Shines had
	int16_t loc;     // a Shine (0..120) or blue coin (1000 + i) reachable, or -1
	uint8_t corona;  // Corona Mountain open
};
struct ApRef {
	uint16_t first, count;
};
struct ApCoin {
	uint16_t bit;        // its save flag is 0x10000 + bit
	uint16_t levels[12]; // the levels it is in (kLevels ids)
	uint16_t levelCount;
	uint16_t first, count;
	const char* world;
	const char* name;
};

#include "ap.inc"

const int kApCoinCount = (int)(sizeof(kApCoins) / sizeof(kApCoins[0]));
const int kApCoinBase  = 1000;

// The skills a set of apworld nozzles is.
inline uint32_t apSkills(uint8_t m)
{
	uint32_t s = 0;
	if (m & 1)
		s |= 1u << 17; // spray
	if (m & 2)
		s |= 1u << 19; // hover
	if (m & 4)
		s |= 1u << 20; // rocket
	if (m & 8)
		s |= 1u << 21; // turbo
	if (m & 16)
		s |= 1u << 14; // Yoshi
	// the randomizer's moves, which the apworld does not lock (ap_gen.py's MOVES)
	if (m & 32)
		s |= 1u << 3; // grab
	if (m & 64)
		s |= 1u << 8; // ground pound
	if (m & 128)
		s |= 1u << 22; // climbing
	return s;
}

// Does a location's apworld need hold? `got` says whether a location is reachable.
template <class Got>
inline bool apMet(const ApRef& ref, uint32_t skills, int shines, bool corona, Got got)
{
	if (ref.count == 0)
		return true;
	for (int i = 0; i < ref.count; ++i) {
		const ApAlt& a = kApAlts[ref.first + i];
		bool noz       = a.orCount == 0;
		for (int k = 0; k < a.orCount && !noz; ++k)
			noz = (apSkills(a.ors[k]) & ~skills) == 0;
		if (noz && a.shines <= shines && (a.loc < 0 || got(a.loc)) && (!a.corona || corona))
			return true;
	}
	return false;
}

// The apworld has no entrance shuffle: what it asks of a secret course's Shine is how
// to reach that course's way in, and inside FLUDD is taken away. With the shuffle the
// way in is the randomizer's, so those Shines (found only in the secret courses,
// areas 0x15 to 0x33) keep the randomizer's needs alone.
inline bool apSecretShine(int shine)
{
	bool secret = false;
	for (int i = 0; i < kLevelCount; ++i) {
		const LevelData& L = kLevels[i];
		for (int k = 0; k < L.shineCount; ++k) {
			if ((kShines[L.shineFirst + k].id >> 8) != shine)
				continue;
			const int area = L.id >> 8;
			if (area < 0x15 || area > 0x33)
				return false;
			secret = true;
		}
	}
	return secret;
}

// Is a blue coin (kApCoins index) in this level?
inline bool apCoinIn(int c, uint16_t level)
{
	for (int k = 0; k < kApCoins[c].levelCount; ++k) {
		if (kApCoins[c].levels[k] == level)
			return true;
	}
	return false;
}

// With shineSwaps (not the randomizer's), some Shines and blue coins trade places: the
// Shine there is taken quietly and gives its blue coin (SMS_EV_SHINE_TOUCH, API 1.6
// beta 2), and the blue coin gives its Shine. For the logic, the Shine is had where its
// blue coin is, the coin where the Shine is. A Shine of the plaza, of a secret course
// (taking it quietly would leave Mario there), a trade or the turbo door's is never
// one; how many pairs (5 to 30) and which, a seed's own draw, apart from the walk's.
inline bool swappableShine(int shine)
{
	if (shine < 0 || shine >= 120 || shine == kPlazaExtraShine || apSecretShine(shine))
		return false;
	for (int k = 0; k < 24; ++k) {
		if (kTradeShines[k] == shine)
			return false;
	}
	bool found = false;
	for (int i = 0; i < kLevelCount; ++i) {
		const LevelData& L = kLevels[i];
		for (int k = 0; k < L.shineCount; ++k) {
			if ((kShines[L.shineFirst + k].id >> 8) != shine)
				continue;
			if (L.id >> 8 == 1)
				return false; // the plaza's
			found = true;
		}
	}
	return found;
}

// A blue coin that may trade places: in a level, none that only Corona Mountain opens
// (nor one asking for it), so that no Shadow Mario's Shine waits on the mountain.
inline bool swappableCoin(int c)
{
	const ApCoin& k = kApCoins[c];
	if (k.levelCount == 0)
		return false;
	for (int i = 0; i < k.count; ++i) {
		if (kApAlts[k.first + i].corona)
			return false;
	}
	for (int j = 0; j < k.levelCount; ++j) {
		for (int i = 0; i < kLevelCount; ++i) {
			if (kLevels[i].id == k.levels[j] && kLevels[i].flag)
				return false;
		}
		const int area = k.levels[j] >> 8;
		if (area == 0x34 || area == 0x3C || area == 0x14)
			return false; // Corona Mountain's, and the airstrip the mountain's way leads to
	}
	return true;
}

// What the player keeps out of the shuffle (the mod's exclusions.txt): entrances that keep
// their own level, and Shines and blue coins (kApCoins index) that hold no unlock.
struct Exclusions {
	std::vector<uint16_t> keep;   // levels (area << 8 | episode); a low byte of 0xFF stands for every episode of the area
	std::vector<int> shines, coins;
	// plando: what the player places (the mod's plando.txt)
	std::vector<std::pair<int, uint32_t>> unlocks;          // place (a Shine's number, or kApCoinBase + a blue coin's index), skill
	std::vector<std::pair<uint16_t, uint16_t>> entrances;   // level asked for (0: where a new file starts), level loaded
};

struct Plan {
	bool ok        = false;
	uint32_t start = 0;                                     // the skills had from the start
	bool coinShines = false;                                // the 100-coin Shines hold unlocks too
	bool plazaExtra = false;                                // the plaza's turbo door Shine too
	bool tradeShines = false;                               // the blue coin trades too
	bool apLogic = false;                                   // the apworld's needs too
	bool coinUnlocks = false;                               // blue coins hold unlocks too
	std::vector<uint32_t> coinUnlock = std::vector<uint32_t>(kApCoinCount, 0); // by kApCoins index
	std::vector<int> shineCoin = std::vector<int>(121, -1);         // the blue coin a Shine trades with
	std::vector<int> coinShine = std::vector<int>(kApCoinCount, -1); // the Shine a blue coin trades with
	int reachable  = 0;                                     // Shines the walk reached
	bool corona    = false;                                 // with every Shadow Mario's
	uint32_t unlock[121] = {};                              // skill bit given by each Shine, or 0
	std::vector<std::pair<uint16_t, uint16_t>> swaps;       // level asked for, level loaded
};

class Generator {
public:
	Plan run(uint32_t seed, uint32_t startSkills, bool shuffle = true, bool sprayInWorlds = false,
	         bool coinShines = false, bool plazaExtra = false, bool tradeShines = false, bool apLogic = false,
	         bool coinUnlocks = false, bool shineSwaps = false, int entranceMode = 0,
	         const Exclusions* exclusions = nullptr)
	{
		ex_            = exclusions ? *exclusions : Exclusions();
		plandoDone_.assign(ex_.unlocks.size(), 0);
		plandoSkills_  = 0;
		for (size_t i = 0; i < ex_.unlocks.size(); ++i)
			plandoSkills_ |= ex_.unlocks[i].second;
		mode_          = entranceMode;
		apLogic_       = apLogic || coinUnlocks || shineSwaps;
		coinUnlocks_   = coinUnlocks;
		pcoins_.clear();
		rcoins_.clear();
		tradeShines_   = tradeShines;
		tradesOpen_    = 0;
		entered_.assign(kLevelCount, 0);
		if (indexOf(0x108) >= 0)
			entered_[indexOf(0x108)] = 1; // Delfino Plaza
		coinShines_    = coinShines;
		plazaExtra_    = plazaExtra;
		shuffle_       = shuffle;
		sprayInWorlds_ = sprayInWorlds;
		rand_    = seed ^ (uint32_t)modeRetry_ * 0x9E3779B9u;
		skills_  = shuffle ? startSkills : startSkills | kSpray;
		plan_    = Plan();
		plan_.start = skills_;
		plan_.coinShines = coinShines;
		plan_.plazaExtra = plazaExtra;
		plan_.tradeShines = tradeShines;
		plan_.apLogic     = apLogic_;
		plan_.coinUnlocks = coinUnlocks;
		swaps_ = shineSwaps;
		if (shineSwaps)
			drawSwaps(seed);
		pexits_.clear();
		rexits_.clear();
		pshines_.clear();
		rshines_.clear();
		keys_.clear();
		vals_.clear();
		corona_ = false;
		plan_.ok = walk();
		// pairs that leave the walk nowhere to go are drawn again (another salt)
		if (shineSwaps && !plan_.ok && swapSalt_ < 20) {
			++swapSalt_;
			Plan again = run(seed, startSkills, shuffle, sprayInWorlds, coinShines, plazaExtra, tradeShines, apLogic,
			                 coinUnlocks, shineSwaps, entranceMode, exclusions);
			--swapSalt_;
			return again;
		}
		// a mode with pools of its own may leave the walk stuck: drawn again (another salt)
		if (entranceMode != 0 && !plan_.ok && modeRetry_ < 30) {
			++modeRetry_;
			Plan again = run(seed, startSkills, shuffle, sprayInWorlds, coinShines, plazaExtra, tradeShines, apLogic,
			                 coinUnlocks, shineSwaps, entranceMode, exclusions);
			--modeRetry_;
			return again;
		}
		return plan_;
	}

private:
	struct Exit {
		uint16_t to;
		uint8_t shines;
		uint32_t need;
	};
	struct Shine {
		uint16_t id;
		uint32_t need;
	};

	uint32_t rand_ = 1, skills_ = 0;
	bool shuffle_  = true;
	bool sprayInWorlds_ = false;
	bool coinShines_    = false;
	bool plazaExtra_    = false;
	bool tradeShines_   = false;
	int tradesOpen_     = 0; // trades the blue coins reached pay for, given to pshines_
	bool apLogic_       = false;
	bool coinUnlocks_   = false;
	bool swaps_         = false;
	int swapSalt_       = 0; // how many times the pairs were drawn again
	int modeRetry_      = 0; // how many times the walk was drawn again (a mode's own pools can leave it stuck)
	// Which entrances are shuffled, and among which (the Entrance shuffle option): 0 all
	// mixed, 1 worlds among themselves, 2 secret courses among themselves, 3 both, each
	// among its own, 4 secret courses among themselves and the rest mixed. An entrance
	// that is not shuffled keeps its level.
	int mode_     = 0;
	Exclusions ex_;

	bool keeps(uint16_t level) const
	{
		for (size_t i = 0; i < ex_.keep.size(); ++i) {
			if (ex_.keep[i] == level || ((ex_.keep[i] & 0xFF) == 0xFF && (ex_.keep[i] >> 8) == (level >> 8)))
				return true;
		}
		return false;
	}
	// no unlock on this place (a Shine's number, or kApCoinBase + a blue coin's index)?
	bool noUnlock(int place) const
	{
		for (size_t i = 0; i < ex_.unlocks.size(); ++i) {
			if (ex_.unlocks[i].first == place)
				return true; // the plando's: placed when it is reached, not drawn
		}
		const std::vector<int>& v = place >= kApCoinBase ? ex_.coins : ex_.shines;
		return std::find(v.begin(), v.end(), place >= kApCoinBase ? place - kApCoinBase : place) != v.end();
	}
	bool plandoEntrance(uint16_t key, uint16_t* val) const
	{
		for (size_t i = 0; i < ex_.entrances.size(); ++i) {
			if (ex_.entrances[i].first == key) {
				*val = ex_.entrances[i].second;
				return true;
			}
		}
		return false;
	}
	bool plandoLevel(uint16_t level) const
	{
		for (size_t i = 0; i < ex_.entrances.size(); ++i) {
			if (ex_.entrances[i].second == level)
				return true; // an entrance is set to lead there: no other is given it
		}
		return false;
	}
	uint32_t plandoSkills_ = 0;   // the skills the plando places, not yet had
	std::vector<char> plandoDone_;
	int curClass_ = -1; // the class of the entrance being placed (-1: the first level)

	// 2 a world (areas 2-9, 0xD, 0xE), 1 a secret course (areas 0x15-0x33, as apSecretShine), 0 the rest
	static int classOf(uint16_t id)
	{
		const int a = id >> 8;
		if ((a >= 2 && a <= 9) || a == 0x0D || a == 0x0E)
			return 2;
		return a >= 0x15 && a <= 0x33 ? 1 : 0;
	}
	bool shuffledClass(int c) const
	{
		switch (mode_) {
		case 1: return c == 2;
		case 2: return c == 1;
		case 3: return c != 0;
		}
		return true;
	}
	// Is every level a class's entrances may lead to placed already?
	bool poolSpent(int c) const
	{
		for (int i = 0; i < kLevelCount; ++i) {
			const int k = classOf(kLevels[i].id);
			if (shuffledClass(k) && sameClass(c, k) && !has(vals_, kLevels[i].id) && !keeps(kLevels[i].id) && !plandoLevel(kLevels[i].id))
				return false;
		}
		return true;
	}
	bool sameClass(int a, int b) const
	{
		if (!shuffledClass(b))
			return false; // a level that stays in place is not given to another entrance
		if (a < 0)
			return true;
		if (mode_ == 3)
			return a == b;
		if (mode_ == 4)
			return (a == 1) == (b == 1);
		return true;
	}
	std::vector<int> pcoins_;                          // blue coins reachable (kApCoins index)
	// the swapped pairs (plan_.shineCoin / coinShine), -1 when not
	std::vector<std::pair<int, uint32_t>> rcoins_;     // and not yet, with what the way in needs
	std::vector<char> entered_; // the levels the walk went into (kLevels index)
	Plan plan_;
	std::vector<Exit> pexits_, rexits_;     // exits that can be taken, and not yet
	std::vector<Shine> pshines_, rshines_;  // Shines that can be collected, and not yet
	std::vector<uint16_t> keys_, vals_;     // levels asked for, and the levels placed
	bool corona_ = false;

	// MSL's rand(), as the game's
	uint32_t draw()
	{
		rand_ = rand_ * 1103515245u + 12345u;
		return (rand_ >> 16) & 0x7FFF;
	}

	static int indexOf(uint16_t id)
	{
		for (int i = 0; i < kLevelCount; ++i) {
			if (kLevels[i].id == id)
				return i;
		}
		return -1;
	}

	// What a level's ways out need on top of its own data: the spray nozzle in the
	// worlds, with sprayInWorlds (restriction bit 2).
	uint32_t extra(uint16_t level) const
	{
		if (!sprayInWorlds_)
			return 0;
		switch (level >> 8) {
		case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0D: case 0x0E:
			return 4;
		}
		return 0;
	}

	template <class T>
	static bool has(const std::vector<T>& v, T x) { return std::find(v.begin(), v.end(), x) != v.end(); }

	// What the skills meet (the module's restriction bits).
	uint32_t restrictions() const
	{
		int shadows = 0;
		for (size_t i = 0; i < pshines_.size(); ++i)
			shadows += shadowShine(pshines_[i].id >> 8) ? 1 : 0;
		return restrictionsOf(skills_, shadows);
	}

	// The levels that could still be placed: one with an exit (exitsOnly), or one with
	// an exit or a Shine (unless shinesOnly), that can be reached now.
	std::vector<uint16_t> candidates(bool shinesOnly, bool exitsOnly, bool ap = true) const
	{
		const uint32_t r = restrictions();
		const size_t n   = pshines_.size();
		std::vector<uint16_t> out;
		for (int i = 0; i < kLevelCount; ++i) {
			const LevelData& L = kLevels[i];
			const uint32_t x   = extra(L.id);
			bool ok            = false;
			if (!shinesOnly || exitsOnly) {
				for (int k = 0; k < L.exitCount && !ok; ++k) {
					const LevelExit& e = kExits[L.exitFirst + k];
					ok                 = ((e.need | x) & ~r) == 0 && e.shines <= n;
				}
			}
			if (!ok && !exitsOnly) {
				for (int k = 0; k < L.shineCount && !ok; ++k) {
					const LevelShine& s = kShines[L.shineFirst + k];
					ok                  = (s.need & ~r) == 0 && (size_t)(s.id & 0xFF) <= n && (!ap || shineOk(s.id));
				}
			}
			if (ok && ((r & kAllShadows) || !L.flag) && !has(vals_, L.id) && !keeps(L.id) && !plandoLevel(L.id) && sameClass(curClass_, classOf(L.id)))
				out.push_back(L.id);
		}
		return out;
	}

	void openExits()
	{
		const uint32_t r = restrictions();
		const size_t n   = pshines_.size();
		std::vector<Exit> keep;
		for (size_t i = 0; i < rexits_.size(); ++i) {
			const Exit& e = rexits_[i];
			if ((e.need & ~r) == 0 && e.shines <= n)
				pexits_.push_back(e);
			else
				keep.push_back(e);
		}
		rexits_.swap(keep);
	}

	// The pairs: a Fisher-Yates of the swappable Shines and of the placed blue coins,
	// with its own generator (MSL's rand, seeded apart), so the walk's draws stay.
	void drawSwaps(uint32_t seed)
	{
		uint32_t g = seed ^ 0x5A17C0DEu ^ (uint32_t)swapSalt_ * 0x9E3779B9u;
		auto next  = [&g]() {
			g = g * 1103515245u + 12345u;
			return (g >> 16) & 0x7FFF;
		};
		std::vector<int> sh, co;
		for (int i = 0; i < 120; ++i) {
			if (swappableShine(i))
				sh.push_back(i);
		}
		for (int c = 0; c < kApCoinCount; ++c) {
			if (swappableCoin(c))
				co.push_back(c);
		}
		for (size_t i = sh.size(); i > 1; --i)
			std::swap(sh[i - 1], sh[next() % i]);
		for (size_t i = co.size(); i > 1; --i)
			std::swap(co[i - 1], co[next() % i]);
		const size_t n = std::min<size_t>(5 + next() % 26, std::min(sh.size(), co.size()));
		for (size_t i = 0; i < n; ++i) {
			plan_.shineCoin[sh[i]] = co[i];
			plan_.coinShine[co[i]] = sh[i];
		}
	}

	// Is a place reached: a Shine's (by its number) or a blue coin's (kApCoinBase + i)?
	// A swapped one's is where its partner is had.
	bool gotLoc(int loc) const
	{
		if (loc >= kApCoinBase && plan_.coinShine[loc - kApCoinBase] >= 0)
			loc = plan_.coinShine[loc - kApCoinBase];
		else if (loc >= 0 && loc < 121 && plan_.shineCoin[loc] >= 0)
			loc = kApCoinBase + plan_.shineCoin[loc];
		if (loc >= kApCoinBase)
			return std::find(pcoins_.begin(), pcoins_.end(), loc - kApCoinBase) != pcoins_.end();
		for (size_t i = 0; i < pshines_.size(); ++i) {
			if ((pshines_[i].id >> 8) == loc)
				return true;
		}
		return false;
	}

	// Does the apworld let this Shine (or blue coin) be had now?
	bool apOk(const ApRef& ref) const
	{
		if (!apLogic_)
			return true;
		const bool corona = corona_ || (restrictions() & kAllShadows);
		return apMet(ref, skills_, (int)pshines_.size(), corona, [this](int l) { return gotLoc(l); });
	}
	bool shineOk(uint16_t id) const
	{
		const int sh = id >> 8;
		if (sh <= 120 && plan_.shineCoin[sh] >= 0) {
			const ApCoin& c = kApCoins[plan_.shineCoin[sh]];
			return apOk(ApRef{ c.first, c.count }); // had where its blue coin is
		}
		return sh > 120 || apSecretShine(sh) || apOk(kApShines[sh]);
	}
	// what having a blue coin (its kApCoins index) asks: a swapped one is had where its
	// Shine is
	ApRef coinRef(int c) const
	{
		return plan_.coinShine[c] >= 0 ? kApShines[plan_.coinShine[c]] : ApRef{ kApCoins[c].first, kApCoins[c].count };
	}

	// Has the level a place (a Shine, or a blue coin with coinUnlocks) Mario can have
	// from the start, by the randomizer's needs and the apworld's?
	bool startUsable(uint16_t level) const
	{
		const int li = indexOf(level);
		if (li < 0)
			return false;
		const uint32_t r   = restrictions();
		const LevelData& L = kLevels[li];
		for (int k = 0; k < L.shineCount; ++k) {
			const LevelShine& x = kShines[L.shineFirst + k];
			if (plan_.shineCoin[x.id >> 8] < 0 && (x.need & ~r) == 0 && (x.id & 0xFF) == 0 && shineOk(x.id))
				return true;
		}
		for (int c = 0; c < kApCoinCount; ++c) {
			if (!apCoinIn(c, level) || !(coinUnlocks_ || plan_.coinShine[c] >= 0))
				continue;
			const ApCoin& k = kApCoins[c];
			if (apOk(ApRef{ k.first, k.count }))
				return true;
		}
		return false;
	}

	void openCoins()
	{
		if (!apLogic_)
			return;
		const uint32_t r = restrictions();
		std::vector<std::pair<int, uint32_t>> keep;
		for (size_t i = 0; i < rcoins_.size(); ++i) {
			const int c = rcoins_[i].first;
			if ((rcoins_[i].second & ~r) == 0 && apOk(coinRef(c)))
				pcoins_.push_back(c);
			else
				keep.push_back(rcoins_[i]);
		}
		rcoins_.swap(keep);
	}

	void openTrades()
	{
		if (!tradeShines_)
			return;
		const int coins = blueCoinsReached(entered_, skills_);
		while (tradesOpen_ < 24 && coins >= 10 * (tradesOpen_ + 1)) {
			const Shine s = { (uint16_t)(kTradeShines[tradesOpen_] << 8), 0 };
			pshines_.push_back(s);
			++tradesOpen_;
		}
	}

	void openShines()
	{
		openTrades();
		openCoins();
		const uint32_t r = restrictions();
		std::vector<Shine> keep;
		const std::vector<Shine> list = rshines_;
		for (size_t i = 0; i < list.size(); ++i) {
			const Shine& s = list[i];
			if ((s.need & ~r) == 0 && (size_t)(s.id & 0xFF) <= pshines_.size() && shineOk(s.id))
				pshines_.push_back(s);
			else
				keep.push_back(s);
		}
		rshines_.swap(keep);
	}

	// The plando's unlocks whose place is reachable now: given there (with the skills it has, more
	// of the game opens). False when none was.
	bool applyPlando()
	{
		bool any = false;
		for (size_t i = 0; i < ex_.unlocks.size(); ++i) {
			if (plandoDone_[i])
				continue;
			const int place     = ex_.unlocks[i].first;
			const uint32_t skill = ex_.unlocks[i].second;
			bool reached = false;
			if (place >= kApCoinBase) {
				reached = coinUnlocks_ && has(pcoins_, place - kApCoinBase);
			} else {
				for (size_t k = 0; k < pshines_.size() && !reached; ++k)
					reached = (pshines_[k].id >> 8) == place;
			}
			if (!reached)
				continue;
			uint32_t& slot = place >= kApCoinBase ? plan_.coinUnlock[place - kApCoinBase] : plan_.unlock[place];
			if (slot == 0) {
				slot = skill;
				skills_ |= skill;
			}
			plandoDone_[i] = 1;
			any            = true;
		}
		plandoSkills_ = 0;
		for (size_t i = 0; i < ex_.unlocks.size(); ++i) {
			if (!plandoDone_[i])
				plandoSkills_ |= ex_.unlocks[i].second;
		}
		return any;
	}

	std::vector<uint32_t> missing() const
	{
		std::vector<uint32_t> out;
		for (int i = 0; i < 23; ++i) {
			if (!(skills_ & (1u << i)) && !(plandoSkills_ & (1u << i)))
				out.push_back(1u << i);
		}
		return out;
	}

	// The unlocks that would meet a restriction bit, among those still missing.
	std::vector<uint32_t> forRestriction(uint32_t bits) const
	{
		const uint32_t r = restrictions();
		std::vector<uint32_t> l;
		if (bits & 1) {
			l.push_back(1);
			l.push_back(0x80000);
			l.push_back(4);
		}
		if ((r & 1) && (bits & 2)) {
			l.push_back(0x20);
			l.push_back(2);
			if (skills_ & 0x80000) {
				l.push_back(1);
				l.push_back(4);
			}
			if (skills_ & 5)
				l.push_back(0x80000);
		}
		static const uint32_t kMap[][2] = { { 4, 0x20000 }, { 8, 0x80000 }, { 0x40, 0x100000 }, { 0x10, 0x100 },
			                                { 0x20, 0x80 }, { 0x80, 0x200000 }, { 0x100, 0x400 }, { 0x200, 0x40 },
			                                { 0x400, 8 }, { 0x800, 0x8000 }, { 0x1000, 0x400000 }, { 0x2000, 0x4000 },
			                                { 0x4000, 0x200 }, { 0x8000, 0x10000 } };
		for (size_t i = 0; i < sizeof kMap / sizeof kMap[0]; ++i) {
			if (bits & kMap[i][0])
				l.push_back(kMap[i][1]);
		}
		const std::vector<uint32_t> m = missing();
		std::vector<uint32_t> out;
		for (size_t i = 0; i < l.size(); ++i) {
			if (std::find(m.begin(), m.end(), l[i]) != m.end())
				out.push_back(l[i]);
		}
		return out;
	}

	void place(uint16_t key, uint16_t val)
	{
		keys_.push_back(key);
		vals_.push_back(val);
		plan_.swaps.push_back(std::make_pair(key, val));
	}

	// The level's exits and Shines, each needing also what the way in needs.
	void enter(uint16_t level, uint32_t wayIn)
	{
		const int li = indexOf(level);
		if (li < 0)
			return; // not a level the walk knows: nothing in it
		const uint32_t r   = restrictions();
		const LevelData& L = kLevels[li];
		entered_[li]       = 1;
		for (int k = 0; k < L.exitCount; ++k) {
			const LevelExit& x = kExits[L.exitFirst + k];
			const Exit e       = { x.to, x.shines, x.need | wayIn | extra(level) };
			((e.need & r) == e.need ? pexits_ : rexits_).push_back(e);
		}
		for (int k = 0; k < L.shineCount; ++k) {
			const LevelShine& x = kShines[L.shineFirst + k];
			const int partner   = plan_.shineCoin[x.id >> 8];
			if (partner >= 0) {
				// the Shine's place gives its blue coin
				if (!has(pcoins_, partner))
					rcoins_.push_back(std::make_pair(partner, x.need | wayIn));
				continue;
			}
			const Shine s = { x.id, x.need | wayIn };
			((s.need & r) == s.need && shineOk(s.id) ? pshines_ : rshines_).push_back(s);
		}
		const int coin = coinShines_ ? coinShineOf(level) : -1;
		if (coin >= 0) {
			const Shine s = { (uint16_t)(coin << 8), wayIn | kCoinShineNeed };
			((s.need & r) == s.need && shineOk(s.id) ? pshines_ : rshines_).push_back(s);
		}
		if (apLogic_) {
			for (int c = 0; c < kApCoinCount; ++c) {
				if (!apCoinIn(c, level))
					continue;
				const int partner = plan_.coinShine[c];
				if (partner >= 0) {
					// the blue coin's place gives its Shine
					const Shine s = { (uint16_t)(partner << 8), wayIn };
					bool had      = false; // once, though the coin is in more levels
					for (size_t i = 0; i < pshines_.size() && !had; ++i)
						had = (pshines_[i].id >> 8) == partner;
					for (size_t i = 0; i < rshines_.size() && !had; ++i)
						had = (rshines_[i].id >> 8) == partner;
					if (!had)
						rshines_.push_back(s);
				} else if (!has(pcoins_, c)) {
					rcoins_.push_back(std::make_pair(c, wayIn));
				}
			}
			openCoins();
			if (swaps_)
				openShines();
		}
	}

	// Stuck: an unlock that opens an exit (preferably one that misses only it), on a
	// Shine that can be had now.
	bool giveUnlock()
	{
		const uint32_t r = restrictions();
		// the module's three byte arrays, which overlap by one byte
		uint8_t mem[0x40] = {};
		const int HAVE = 0x00, SINGLE = 0x10, COUNT = 0x20;
		for (int b = 0; b < 17; ++b)
			mem[HAVE + b] = (r >> b) & 1;
		for (size_t i = 0; i < rexits_.size(); ++i) {
			const Exit& e = rexits_[i];
			const int li  = indexOf(e.to);
			if (li < 0 || kLevels[li].shineCount == 0 || e.need == 0)
				continue;
			uint32_t m = e.need;
			int b = 0, miss = 0, last = 0;
			while (m) {
				if ((m & 1) && mem[HAVE + b] == 0) {
					++miss;
					++mem[COUNT + b];
					last = b;
				}
				m >>= 1;
				++b;
			}
			if (miss == 1)
				++mem[SINGLE + last];
		}
		std::vector<uint32_t> cands;
		for (int b = 0; b < 17; ++b) {
			if (mem[SINGLE + b]) {
				const std::vector<uint32_t> f = forRestriction(1u << b);
				cands.insert(cands.end(), f.begin(), f.end());
			}
		}
		if (cands.empty()) {
			for (int b = 0; b < 17; ++b) {
				if (mem[COUNT + b]) {
					const std::vector<uint32_t> f = forRestriction(1u << b);
					cands.insert(cands.end(), f.begin(), f.end());
				}
			}
		}
		if (cands.empty())
			cands = missing();
		if (cands.empty())
			return false; // no unlock left that would help
		std::vector<int> free;
		for (size_t i = 0; i < pshines_.size(); ++i) {
			if (plan_.unlock[pshines_[i].id >> 8] == 0 && !noUnlock(pshines_[i].id >> 8))
				free.push_back(pshines_[i].id >> 8);
		}
		if (coinUnlocks_) {
			for (size_t i = 0; i < pcoins_.size(); ++i) {
				if (plan_.coinUnlock[pcoins_[i]] == 0 && !noUnlock(kApCoinBase + pcoins_[i]))
					free.push_back(kApCoinBase + pcoins_[i]);
			}
		}
		if (free.empty())
			return false; // no Shine to put it on
		const uint32_t skill = cands[draw() % cands.size()];
		const int place      = free[draw() % free.size()];
		uint32_t& slot = place >= kApCoinBase ? plan_.coinUnlock[place - kApCoinBase] : plan_.unlock[place];
		if (slot == 0 && (skill & ~skills_)) {
			slot = skill;
			skills_ |= skill;
		}
		openExits();
		openShines();
		return true;
	}

	bool walk()
	{
		static const uint16_t kFixed[] = { 0x100, 0x101, 0x102, 0x105, 0x106, 0x107, 0x108, 0x109, 0x3400, 0x3C00 };
		std::vector<uint16_t> fixed(kFixed, kFixed + sizeof kFixed / sizeof kFixed[0]);
		for (size_t i = 0; i < fixed.size(); ++i)
			place(fixed[i], fixed[i]);
		const LevelData& start = kLevels[indexOf(0x108)];
		for (int k = 0; k < start.exitCount; ++k) {
			const LevelExit& x = kExits[start.exitFirst + k];
			// with sprayInWorlds, the plaza's ways out too (goop and its enemies on them)
			const Exit e = { x.to, x.shines, x.need | (sprayInWorlds_ ? 4u : 0u) };
			rexits_.push_back(e);
		}
		for (int k = 0; k < start.shineCount; ++k) {
			const LevelShine& x = kShines[start.shineFirst + k];
			const Shine s       = { x.id, x.need };
			rshines_.push_back(s);
		}
		if (coinShines_) {
			const Shine s = { (uint16_t)(107 << 8), kCoinShineNeed }; // the plaza's 100 coins
			rshines_.push_back(s);
		}
		if (plazaExtra_) {
			const Shine s = { (uint16_t)(kPlazaExtraShine << 8), kPlazaExtraNeed };
			rshines_.push_back(s);
		}
		plan_.swaps.push_back(std::make_pair((uint16_t)0xF00, (uint16_t)0xF00));
		std::vector<uint16_t> cand;
		curClass_ = -1;
		if (shuffle_) {
			// the level a new file would start in
			// (with apLogic, drawn from the randomizer's own; one with nothing the
			// apworld lets Mario have from the start is left out and another drawn)
			cand = candidates(true, false, false);
			uint16_t first = 0, planFirst = 0;
			const bool planStart = plandoEntrance(0, &planFirst); // the plando's start level
			if (planStart)
				first = planFirst;
			for (; !planStart;) {
				if (cand.empty())
					return false;
				const size_t k = draw() % cand.size();
				first          = cand[k];
				if (!apLogic_ || startUsable(first))
					break;
				cand.erase(cand.begin() + k);
			}
			if (planStart && has(vals_, first))
				return false;
			place(0, first);
			enter(first, 0);
		}
		openExits();
		openShines();
		for (;;) {
			while (applyPlando()) {
				openExits();
				openShines();
			}
			if (pexits_.empty() && rexits_.empty())
				break;
			openExits();
			openShines();
			uint16_t planVal = 0; // an entrance the plando sets leads where it says
			const bool planE = shuffle_ && !pexits_.empty() && plandoEntrance(pexits_.front().to, &planVal);
			curClass_ = pexits_.empty() || has(fixed, pexits_.front().to) ? -1 : classOf(pexits_.front().to);
			// an entrance that is not shuffled keeps its level
			const bool pin = shuffle_ && !pexits_.empty() && !has(fixed, pexits_.front().to)
			                 && !planE && (keeps(pexits_.front().to) || !shuffledClass(curClass_));
			if (shuffle_ && !pin && !planE) {
				cand = candidates(false, rexits_.size() + pexits_.size() < 2);
				if (cand.empty())
					cand = candidates(false, false);
			}
			// With a pool of its own the start level took one of the class's levels, so the
			// last entrance of the class has none left: it leads to one placed already.
			const bool spent = shuffle_ && !pin && !planE && !pexits_.empty() && cand.empty() && mode_ != 0 && poolSpent(curClass_);
			if (!pin && !planE && !spent && (pexits_.empty() || (shuffle_ && cand.empty()))) {
				if (!giveUnlock())
					return false;
				continue;
			}
			const Exit e = pexits_.front();
			pexits_.erase(pexits_.begin());
			if (has(keys_, e.to))
				continue;
			uint16_t val = e.to;
			if (planE) {
				val = planVal;
				if (has(vals_, val))
					return false; // the level is placed already
			} else if (shuffle_ && !pin && !has(fixed, e.to)) {
				const bool few = pshines_.size() < 3;
				cand           = candidates(few, rexits_.size() + pexits_.size() < 11);
				if (cand.empty())
					cand = candidates(few, false);
				bool again = false;
				if (cand.empty() && spent) {
					for (size_t i = 0; i < vals_.size(); ++i) {
						const uint16_t v = vals_[i];
						if (!has(fixed, v) && v != 0 && !keeps(v) && !plandoLevel(v) && shuffledClass(classOf(v)) && sameClass(curClass_, classOf(v)))
							cand.push_back(v);
					}
					again = !cand.empty();
				}
				if (cand.empty())
					return false; // (the module divides by zero here)
				val = cand[draw() % cand.size()];
				if (again) {
					place(e.to, val); // its level is entered already
					continue;
				}
			}
			place(e.to, val);
			enter(val, e.need);
			if ((restrictions() & kAllShadows) && !corona_) {
				const Exit a = { 0x3400, 0, 0x10008 }, b = { 0x1400, 0, 0x10100 };
				rexits_.push_back(a);
				rexits_.push_back(b);
				openExits();
				corona_ = true;
			}
		}
		// the plando's must all have been placed: an unlock whose place the walk never reached, or an
		// entrance it never came to, leaves no game
		for (size_t i = 0; i < plandoDone_.size(); ++i) {
			if (!plandoDone_[i])
				return false;
		}
		for (size_t i = 0; i < ex_.entrances.size(); ++i) {
			if (shuffle_ && ex_.entrances[i].first != 0 && !has(keys_, ex_.entrances[i].first))
				return false;
		}
		std::vector<uint32_t> rem = missing();
		if (apLogic_)
			openShines();
		while (!rem.empty() && !pshines_.empty()) {
			const uint32_t skill = rem[draw() % rem.size()];
			uint32_t* slot       = nullptr;
			int guard            = 0;
			int place            = 0;
			do {
				do {
					const size_t n = pshines_.size() + (coinUnlocks_ ? pcoins_.size() : 0);
					const size_t k = draw() % n;
					slot = k < pshines_.size() ? &plan_.unlock[pshines_[k].id >> 8]
					                           : &plan_.coinUnlock[pcoins_[k - pshines_.size()]];
					place = k < pshines_.size() ? pshines_[k].id >> 8 : kApCoinBase + pcoins_[k - pshines_.size()];
					if (++guard > 1000000)
						return false; // every reachable Shine has an unlock already
				} while (*slot != 0 || noUnlock(place));
			} while ((skill & ~skills_) == 0);
			*slot = skill;
			skills_ |= skill;
			rem = missing();
			if (apLogic_)
				openShines();
		}
		plan_.reachable = (int)pshines_.size();
		plan_.corona    = corona_;
		return true;
	}
};

// The plan's spheres: sphere 1 is every Shine Mario can collect from the start, sphere
// 2 those the unlocks of sphere 1 open, and so on, by the same rules as the walk (what
// an exit and a Shine need, with what the way in needs; Shines needed counted as the
// walk counts them). A new file starts in the level placed for the airstrip with the
// shuffle, in Delfino Plaza without it. Each sphere lists its Shines' unlocks (Shine,
// skill bit); `reachable` its Shines.
struct Sphere {
	std::vector<std::pair<int, uint32_t>> unlocks;
	int reachable = 0;
	bool corona   = false; // every Shadow Mario's Shine reachable by then
};

// What can be reached with `skills`: a fixpoint, the Shadow Mario bit depending on it.
// Returns what each reachable place gives (a Shine's number, a blue coin's kApCoinBase
// + i; a swapped place gives its partner), as the walk counts them; `r` is left at the
// restrictions reached. `count` is the Shines an episode or a way asks for are counted
// against. For a file being played (the tracker), `had` gives the Shines it has, which
// the apworld's needs then count, and `shadows` the Shadow Marios it has beaten, which
// open Corona Mountain; -1 for the walk's own counts.
inline std::vector<int> reachable(const Plan& plan, bool shuffle, uint32_t skills, int count, bool coronaOpen,
                                  uint32_t& r, int had = -1, int shadowsHad = -1)
{
	auto levelIndex = [](uint16_t id) {
		for (int i = 0; i < kLevelCount; ++i) {
			if (kLevels[i].id == id)
				return i;
		}
		return -1;
	};
	auto loaded = [&](uint16_t key) {
		if (!shuffle)
			return key;
		for (size_t i = 0; i < plan.swaps.size(); ++i) {
			if (plan.swaps[i].first == key)
				return plan.swaps[i].second;
		}
		return key;
	};
	const int places = kApCoinBase + kApCoinCount;
	std::vector<int> entries;
	r = restrictionsOf(skills, shadowsHad < 0 ? 0 : shadowsHad);
	std::vector<char> have(places, 0); // reachable on the pass before (the apworld's needs)
	size_t lastSize = (size_t)-1;
	for (int pass = 0; pass < 32; ++pass) {
		int haveShines = 0;
		for (size_t i = 0; i < entries.size(); ++i)
			haveShines += entries[i] < kApCoinBase ? 1 : 0;
		if (had >= 0)
			haveShines = had;
		std::fill(have.begin(), have.end(), 0);
		for (size_t i = 0; i < entries.size(); ++i)
			have[entries[i]] = 1;
		const bool coronaNow = coronaOpen || (r & 0x10000);
		// a place reached (a swapped one's where its partner is had)
		auto reached = [&](int l) {
			if (l >= kApCoinBase && plan.coinShine[l - kApCoinBase] >= 0)
				l = plan.coinShine[l - kApCoinBase];
			else if (l >= 0 && l < 121 && plan.shineCoin[l] >= 0)
				l = kApCoinBase + plan.shineCoin[l];
			return have[l] != 0;
		};
		auto apOk = [&](const ApRef& ref) {
			return !plan.apLogic || (&ref >= kApShines && &ref < kApShines + 121 && apSecretShine((int)(&ref - kApShines)))
			       || apMet(ref, skills, haveShines, coronaNow, reached);
		};
		std::vector<char> swapPushed(121, 0); // a swapped Shine once, though its coin is in more levels
		entries.clear();
		std::vector<uint32_t> seen(kLevelCount, 0xFFFFFFFFu); // the least need a level was reached with
		struct Visit {
			int level;
			uint32_t need;
		};
		std::vector<Visit> todo;
		const int plaza = levelIndex(0x108);
		auto addExits = [&](int li, uint32_t wayIn) {
			const LevelData& L = kLevels[li];
			for (int k = 0; k < L.exitCount; ++k) {
				const LevelExit& x = kExits[L.exitFirst + k];
				if (((x.need | wayIn) & ~r) == 0 && (int)x.shines <= count)
					todo.push_back(Visit{ levelIndex(loaded(x.to)), x.need | wayIn });
			}
		};
		if (plaza >= 0)
			todo.push_back(Visit{ plaza, 0 }); // its Shines and its ways out
		if (shuffle) {
			const int first = levelIndex(loaded(0));
			if (first >= 0)
				todo.push_back(Visit{ first, 0 });
		}
		if (coronaOpen || (r & 0x10000)) {
			// Corona Mountain's ways, as the walk adds them
			const uint32_t corona[2][2] = { { 0x3400, 0x10008 }, { 0x1400, 0x10100 } };
			for (int c = 0; c < 2; ++c) {
				if ((corona[c][1] & ~r) == 0)
					todo.push_back(Visit{ levelIndex(loaded(corona[c][0])), corona[c][1] });
			}
		}
		while (!todo.empty()) {
			const Visit v = todo.back();
			todo.pop_back();
			// what the way in needs is met already (it is within r): a level once
			if (v.level < 0 || seen[v.level] != 0xFFFFFFFFu)
				continue;
			seen[v.level]       = v.need;
			const LevelData& L  = kLevels[v.level];
			for (int k = 0; k < L.shineCount; ++k) {
				const LevelShine& x = kShines[L.shineFirst + k];
				const int partner   = plan.shineCoin[x.id >> 8];
				if (((x.need | v.need) & ~r) == 0 && (int)(x.id & 0xFF) <= count && apOk(kApShines[x.id >> 8]))
					entries.push_back(partner >= 0 ? kApCoinBase + partner : x.id >> 8);
			}
			const int coin = plan.coinShines ? coinShineOf(L.id) : -1;
			if (coin >= 0 && ((kCoinShineNeed | v.need) & ~r) == 0 && apOk(kApShines[coin]))
				entries.push_back(coin);
			if (plan.plazaExtra && L.id == 0x108 && (kPlazaExtraNeed & ~r) == 0 && apOk(kApShines[kPlazaExtraShine]))
				entries.push_back(kPlazaExtraShine);
			if (plan.apLogic && (v.need & ~r) == 0) {
				for (int c = 0; c < kApCoinCount; ++c) {
					if (!apCoinIn(c, L.id) || !apOk(ApRef{ kApCoins[c].first, kApCoins[c].count }))
						continue;
					const int partner = plan.coinShine[c];
					if (partner < 0)
						entries.push_back(kApCoinBase + c);
					else if (!swapPushed[partner]) {
						swapPushed[partner] = 1;
						entries.push_back(partner);
					}
				}
			}
			addExits(v.level, v.need);
		}
		if (plan.tradeShines) {
			std::vector<char> reachedLevels(kLevelCount, 0);
			for (int i = 0; i < kLevelCount; ++i)
				reachedLevels[i] = seen[i] != 0xFFFFFFFFu;
			const int coins = blueCoinsReached(reachedLevels, skills);
			for (int k = 0; k < 24 && coins >= 10 * (k + 1); ++k)
				entries.push_back(kTradeShines[k]);
		}
		int shadows = 0;
		for (size_t i = 0; i < entries.size(); ++i)
			shadows += shadowShine(entries[i]) ? 1 : 0;
		if (shadowsHad >= 0)
			shadows = shadowsHad;
		const uint32_t r2 = restrictionsOf(skills, shadows) | (coronaOpen ? 0x10000u : 0u);
		if (r2 == r && (!plan.apLogic || entries.size() == lastSize))
			break;
		lastSize = entries.size();
		r = r2;
	}
	return entries;
}

inline std::vector<Sphere> spheres(const Plan& plan, bool shuffle)
{
	std::vector<Sphere> out;
	uint32_t skills = plan.start;
	bool coronaOpen = false; // once open, Corona Mountain's ways stay (as the walk keeps them)
	const int places = kApCoinBase + kApCoinCount; // Shines, then blue coins (1000 + i)
	std::vector<char> got(places, 0);
	int count = 0; // Shine entries reachable, as the walk counts them
	for (int round = 0; round < 64; ++round) {
		uint32_t r = 0;
		const std::vector<int> entries = reachable(plan, shuffle, skills, count, coronaOpen, r);
		coronaOpen = coronaOpen || (r & 0x10000);
		Sphere sp;
		sp.corona = (r & 0x10000) != 0;
		auto unlockAt = [&](int id) { return id >= kApCoinBase ? plan.coinUnlock[id - kApCoinBase] : plan.unlock[id]; };
		std::vector<char> now(places, 0);
		int newPlaces = 0, shineEntries = 0;
		for (size_t i = 0; i < entries.size(); ++i) {
			const int id = entries[i];
			shineEntries += id < kApCoinBase ? 1 : 0;
			if (!got[id] && !now[id]) {
				now[id] = 1;
				++newPlaces;
				sp.reachable += id < kApCoinBase ? 1 : 0;
				if (unlockAt(id))
					sp.unlocks.push_back(std::make_pair(id, unlockAt(id)));
			}
		}
		if (newPlaces == 0)
			break;
		for (int id = 0; id < places; ++id) {
			if (now[id]) {
				got[id] = 1;
				skills |= unlockAt(id);
			}
		}
		count = shineEntries;
		out.push_back(sp);
	}
	return out;
}

} // namespace gen

#endif
