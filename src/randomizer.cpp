// Randomizer: a native mod for the Super Mario Sunshine PC port.
//
// 22 unlocks (jumps, wall kicks, ground pound, nozzles, grab, climbing, Yoshi...) start
// locked, each given by one Shine, and the levels are shuffled (Shuffle levels). Both
// come from the seed, worked out as the randomizer does (generator.h); without the
// shuffle the unlocks are placed for the game's own map, the spray given from the start.
//
// Mario's actions are PLAYER_STATUS, the shuffle and the start of a new file (in
// Delfino Plaza, without the intro) FILE_SELECTED and STAGE_LEAVING, the Y-button
// camera PAD_READ, the "UNLOCKED" message and the list of unlocks SHINE_COLLECTED and
// DRAW, and the options the Mods menu (F1). The nozzles and Mario's outfit need API 1.5
// (PLAYER_NOZZLE, player->set_nozzle, set_fludd, set_outfit); on a game without it they
// are left alone.
#include <sms_mod.hpp>

#include "generator.h"

// the plaza's entrances on its map (the map's own labels, in the map's pixels): a world's area
struct TrackerEntrance {
	uint8_t area;
	uint16_t x, y;
};
static const TrackerEntrance kTrackerEntrances[] = {
	{ 2, 658, 765 },  // Bianco Hills
	{ 3, 385, 575 },  // Ricco Harbor
	{ 4, 1213, 613 }, // Gelato Beach
	{ 5, 600, 350 },  // Pinna Park
	{ 6, 735, 600 },  // Sirena Beach
	{ 7, 735, 660 },  // Hotel Delfino (the dome; the data has it behind Sirena's way)
	{ 8, 930, 372 },  // Pianta Village
	{ 9, 885, 605 },  // Noki Bay
};

// the tracker's maps and markers (tracker.inc, from the Sunshine_Pop PopTracker pack)
struct TrackerMap {
	const char* name;
	const char* image; // in the mod's folder
};
struct TrackerMark {
	uint8_t map;
	uint16_t x, y;         // in the map's pixels
	uint16_t first, count; // its places, in kTrackerPlaces
};
#include "tracker.inc"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

namespace {

struct Unlock {
	const char* name;    // what the rules call it
	const char* display; // as the message and the list show it
	int bit;             // the randomizer's skill bit
};

const Unlock kUnlocks[] = {
	{ "double jump", "DOUBLE JUMP", 0 },
	{ "triple jump", "TRIPLE JUMP", 1 },
	{ "side flip", "SIDEFLIP", 2 },
	{ "grab", "GRAB", 3 },
	{ "ground spin", "GROUND SPIN", 4 },
	{ "spin jump", "SPIN JUMP", 5 },
	{ "rollout", "ROLLOUT", 6 },
	{ "wall kicks", "WALL KICKS", 7 },
	{ "ground pound", "GROUND POUND", 8 },
	{ "y camera", "Y CAM", 9 },
	{ "shine shirt", "SHINE SHIRT", 11 },
	{ "sunglasses", "SUNGLASSES", 12 },
	{ "helmet", "HELMET", 13 },
	{ "yoshi", "YOSHI", 14 },
	{ "blooper", "BLOOPER", 15 },
	{ "rollercoaster cart", "ROLLERCOASTER CART", 16 },
	{ "spray nozzle", "SPRAY NOZZLE", 17 },
	{ "spam spray", "SPAM SPRAY", 18 },
	{ "hover nozzle", "HOVER NOZZLE", 19 },
	{ "rocket nozzle", "ROCKET NOZZLE", 20 },
	{ "turbo nozzle", "TURBO NOZZLE", 21 },
	{ "climbing", "CLIMBING", 22 },
};
const int kUnlockCount = (int)(sizeof(kUnlocks) / sizeof(kUnlocks[0]));

// Indices into kUnlocks for the rules below.
enum {
	DOUBLE_JUMP, TRIPLE_JUMP, SIDE_FLIP, GRAB, GROUND_SPIN, SPIN_JUMP, ROLLOUT, WALL_KICKS, GROUND_POUND,
	Y_CAMERA, SHINE_SHIRT, SUNGLASSES, HELMET, YOSHI, BLOOPER, CART, SPRAY, SPAM_SPRAY, HOVER, ROCKET,
	TURBO, CLIMBING
};

const int kDefaultSeed = 1234;
const int kMaxSeed     = 16777215; // the Mods menu keeps numbers as floats: exact up to 2^24
const int kWins[]      = { 0, 16, 30, 50, 60, 96 };

// MARIO_STATUS_* values the rules name.
const uint32_t kJump = 0x2000880;
// The plaza's ring back to the shuffle's first level, where the player chose it.
const float kWarpRadius  = 120;
const SmsVec3 kWarpPos   = { 3605, 300, 5141 };

// SMS_EV_STAGE_LOADING: the stage about to load, its episode chosen (API 1.5 beta 4)
#if SMS_MOD_API_MINOR > 5 || SMS_MOD_API_PRERELEASE == 0 || SMS_MOD_API_PRERELEASE >= 4
#define RANDOMIZER_STAGE_LOADING 1
#endif
// API 1.5 beta 6: save files' own files, FLUDD's trigger, start kinds, Shine models and
// colours, Yoshi's egg, episodes' Shines
#if SMS_MOD_API_MINOR > 5 || SMS_MOD_API_PRERELEASE == 0 || SMS_MOD_API_PRERELEASE >= 6
#define RANDOMIZER_BETA6 1
#endif
// API 1.6: a save file's menu at file select (SMS_EV_FILE_MENU)
#if SMS_MOD_API_MINOR >= 6
#define RANDOMIZER_FILE_MENU 1
#endif
// API 1.6 beta 2: the episode select's count (SMS_EV_EPISODE_SELECT), every status asked
// (the spin jumps too), the warp menu and save states told apart (SMS_LEAVE_WARP/STATE_LOAD)
#if SMS_MOD_API_MINOR > 6 || (SMS_MOD_API_MINOR == 6 && (SMS_MOD_API_PRERELEASE == 0 || SMS_MOD_API_PRERELEASE >= 2))
#define RANDOMIZER_16_BETA2 1
#endif
// API 1.5 beta 7: the file select waits for a mod (SMS_EV_FILE_STARTING)
#if SMS_MOD_API_MINOR > 5 || SMS_MOD_API_PRERELEASE == 0 || SMS_MOD_API_PRERELEASE >= 7
#define RANDOMIZER_BETA7 1
#endif

// The colour the Shines that give an unlock cycle through: hue `h` (0..1), full
// saturation, lightness 0.5 (the randomizer's is 0.55, which the player found washed
// out), as HSL to RGB.
SmsColor unlockShineColor(double h)
{
	const double q = 1.0, p = 0.0; // lightness 0.5, saturation 1
	double c[3];
	for (int i = 0; i < 3; ++i) {
		double t = h + (1 - i) / 3.0;
		if (t < 0)
			t += 1;
		if (t > 1)
			t -= 1;
		if (t < 1 / 6.0)
			c[i] = p + (q - p) * 6 * t;
		else if (t < 0.5)
			c[i] = q;
		else if (t < 2 / 3.0)
			c[i] = p + (q - p) * 6 * (2 / 3.0 - t);
		else
			c[i] = p;
	}
	return SMS_RGBA((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), 0xF0);
}

// The episode flag (0x40003) the randomizer gives a level it loads: what the pause menu
// names, and what the way back into Hotel Delfino, Pinna Park and Casino Delfino reads.
int episodeFlag(int area, int episode)
{
	static const uint8_t kHotel[] = { 1, 2, 3, 6, 7 }, kPinnaPark[] = { 0, 2, 4, 5, 6, 7, 0, 0 };
	switch (area) {
	case 0x07: return episode < 5 ? kHotel[episode] : episode;
	case 0x0D: return episode < 8 ? kPinnaPark[episode] : episode;
	case 0x0E: return episode == 0 ? 3 : episode == 1 ? 4 : episode;
	case 0x3A: return episode == 0 ? 7 : episode == 1 ? 0 : episode;
	}
	if (episode != 0)
		return episode;
	switch (area) {
	case 0x10: return 7;
	case 0x1E: case 0x32: case 0x33: case 0x37: return 1;
	case 0x2C: case 0x2F: return 2;
	case 0x21: case 0x28: case 0x30: case 0x39: return 3;
	case 0x2A: case 0x38: return 4;
	case 0x1F: case 0x29: case 0x2E: return 5;
	}
	return 0;
}

// for New seed
uint32_t nextRandom(uint32_t& state)
{
	if (state == 0)
		state = 1; // xorshift never leaves 0
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

} // namespace

struct Randomizer : sms::Mod {
	using sms::Mod::Mod;

	// the placement in force
	static const int kFromStart = -2;
	int shine_[22] = {}; // the Shine that gives each unlock, kFromStart, or -1 (none)
	std::vector<std::pair<uint16_t, uint16_t>> swaps_;
	int appliedSeed_ = -1, appliedEnabled_ = -1, appliedAll_ = -1, appliedShuffle_ = -1, appliedCoins_ = -1, appliedTrades_ = -1,
	    appliedCoinUnlocks_ = -1, appliedSwaps_ = -1, appliedMode_ = -1;
	gen::Generator generator_;
	bool planFailed_ = false; // the seed has no game with these options (too many exclusions...)
	gen::Exclusions excl_; // exclusions.txt in the mod's data folder, read by loadExclusions()
	std::string exclText_, plandoText_;
	gen::Plan plan_; // the seed's game, as refresh() worked it out (the tracker's logic)
	// the tracker (Y on the paused page): a world's Shines and blue coins, green those the
	// logic reaches with the unlocks had, red the others, grey those the file has
	bool trackerOpen_      = false;
	int trackerPage_       = 0;
	double trackerAt_      = -1;    // when its logic was last worked out
	std::vector<char> trackerReach_; // by place: Shines, then kApCoinBase + i
	// the plaza's entrances (kTrackerEntrances): open in logic, and how many of the world's
	// levels behind it the file has not been in
	bool entranceOpen_[16]  = {};
	int entranceNew_[16]    = {};
	int entranceTotal_[16]  = {};
	uint16_t lastTrackerButtons_ = 0;
	SmsImage trackerImage_       = 0; // the map shown
	int trackerImagePage_        = -1;
	float trackerCursorX_ = 0, trackerCursorY_ = 0; // in the map's pixels
	float trackerZoom_ = 1;                          // the map's zoom (1 to 6)
	bool trackerHideDone_ = false, trackerHideOut_ = false; // the squares left out: had, or out of logic

	// the message and the list
	int popup_      = -1;
	double popupAt_ = 0;
	bool listShown_ = false;
	int listKey_    = -1;
	int posKey_     = -1; // writes Mario's position to the log
	double spoilerShownAt_ = -1; // when X wrote the spoiler log
	bool lastX_     = false;
	bool xHeld_ = false, xUsed_ = false; // X held last frame; held for the tracker: no spoiler log
	bool lastY_     = false;
	uint32_t hudHidden_ = 0; // the HUD parts the mod hides (set_hud_hidden)
	SmsImage dpadIcons_[3] = {}; // the hover, rocket and turbo icons (icons/*.png)
	// the Shines and blue coins trading places (the plan's), and the swapped Shines this
	// file took quietly (taken.txt with the save file: their blue coins are had)
	std::vector<int> shineCoin_ = std::vector<int>(121, -1), coinShine_ = std::vector<int>(gen::kApCoinCount, -1);
	std::vector<char> taken_    = std::vector<char>(121, 0);
	std::vector<char> seenLevels_ = std::vector<char>(gen::kLevelCount, 0); // the levels the file has been in
	std::vector<char> earned_     = std::vector<char>(kUnlockCount, 0);     // the unlocks the file has had
	std::vector<char> got_        = std::vector<char>(gen::kApCoinBase + gen::kApCoinCount, 0); // the places it has picked up
	uint16_t trackerSwallow_      = 0; // buttons still held from the tracker: not given to the pause menu
	bool ownFlag_               = false; // a flag set by the mod itself (FLAG_CHANGED)
	std::string swapNote_;               // "+1 BLUE COIN" / "+1 SHINE", shown a moment
	double swapNoteAt_ = -1;
	bool lastZ_       = false; // Z held last frame (the warp back to the first level)
	uint16_t lastDpad_ = 0; // the D-pad held last frame (its nozzle choice)
	int targetArea_ = -1, targetEpisode_ = -1; // the level the last shuffle sent Mario to
	uint16_t entered_ = 0;     // the level Mario last went to from the plaza, before the shuffle
	bool shineTaken_  = false; // a Shine was taken since the last stage loaded
	int startPlaced_  = -1;    // the shuffle's first level, where a new file was sent already
	bool loadingSeen_ = false; // the game sends SMS_EV_STAGE_LOADING
	bool fileOpen_    = false; // a save file is played: its game is kept with it
	int livesKept_    = -1;    // Infinite lives: the lives Mario keeps
	// the setup screen of a new file
	bool newFile_     = false; // a new file was started: its setup is still to come
	bool setupAtStart_ = false; // the setup is the file select's (the game leaves after it)
	bool setupDone_    = false; // this file's start had its setup (until a stage loads)
	bool setupAtMenu_  = false; // the setup is over an empty file's menu: A and B go on to it
	bool setupOpen_   = false;
	int seedDigitsHeld_ = 0; // the keyboard's digits down, as bits (the seed row types a number)
	bool seedTyping_    = false; // a number being typed over the seed
	int setupRow_     = 0;
	int setupTab_     = 0; // 0 play, 1 options, 2 excluded locations, 3 plando
	int exclCat_      = 0; // the excluded tab's list: 0 entrances by zone, 1 Shines, 2 blue coins, 3 entrances by episode
	int exclSel_      = 0, exclTop_ = 0; // its row, and the first row shown
	std::string exclFilter_;             // what is typed over the list: the rows that have it in their name
	std::vector<int> exclView_;          // the rows shown (indices of the list)
	uint64_t exclKeysHeld_ = 0;          // the keys of the filter held last frame (letters, digits, space, backspace)
	static const int kExclRows = 12;     // rows of that list shown at once
	uint16_t setupFirst_ = 0; // the first level when the setup opened
	int setupHeld_    = 0;    // directions and buttons held last frame (edges)
	int spoilerSeed_  = -1;   // the seed whose spoiler log the setup wrote
	std::string spoilerPath_; // where it went
	std::vector<SmsObject> unlockShines_; // this stage's Shines that give an unlock
	std::vector<SmsObject> swapShines_;   // this stage's Shines that give a blue coin (tinted blue)
	struct CoinTint {
		SmsObject object;
		SmsColor color;
		int variant; // 0 the tint; 1.. a Shine model on a coin: what makes it show (an experiment)
	};
	std::vector<CoinTint> coinTints_; // blue coins to colour once their model is made
	std::vector<SmsObject> coinSparkles_; // blue coins drawn as a Shine: its sparkles are asked for each tick

	// ------------------------------------------------------------------ state

	int seed() const
	{
		const int v = setting("seed", kDefaultSeed);
		return v < 1 ? 1 : v > kMaxSeed ? kMaxSeed : v;
	}
	bool enabled() const { return setting("enabled", 1) != 0; }

	// A new seed, kept as the Seed setting (the Mods menu shows it there).
	void drawSeed()
	{
		static uint32_t state = 0;
		state += (uint32_t)time(nullptr) ^ (uint32_t)(core().time() * 1000.0);
		const int s = (int)(nextRandom(state) % (uint32_t)kMaxSeed) + 1;
		config().set_int(handle(), "seed", s);
		config().save(handle());
		info("new seed %d", s);
	}
	bool allUnlocks() const { return setting("all_unlocks", 0) != 0; }

	// ------------------------------------------------------------------ setup

	// The setup screen of a new file: the seed, a new one, the options that shape the
	// game, and Start. The file select waits for it (SMS_EV_FILE_STARTING) and the game
	// starts in the first level of the game chosen. On a game without that event it
	// opens as the new file's first stage starts, the controller taken, and Start sends
	// Mario to the first level of the game chosen when it is another.
	enum { ROW_SEED, ROW_NEW_SEED, ROW_MODE, ROW_ALL, ROW_WIN, ROW_COINS, ROW_TRADES, ROW_BLUE, ROW_SWAPS, ROW_LIVES, ROW_GATES, ROW_SPOILER, ROW_START, ROW_COUNT };

	void openSetup()
	{
		loadExclusions();
		if (!SMS_API_HAS(api().input, capture) || !input().capture(handle(), 1))
			return; // another mod has the controller: the Mods menu still has the options
		if (setupAtMenu_ && setting("random_seed", 0))
			drawSeed(); // Use random seed: drawn now, so the screen shows the game's own
		refresh();
		setupOpen_  = true;
		setupRow_   = ROW_START;
		setupTab_   = 0;
		setupFirst_ = shuffle() ? swapOf(0) : (uint16_t)0x100;
		setupHeld_  = ~0; // what is held as it opens does not count
	}

	void closeSetup()
	{
		setupOpen_ = false;
		input().capture(handle(), 0);
		refresh();
		if (setupAtStart_) {
			setupDone_ = true; // the file select goes on: FILE_SELECTED sends it to the game's first level
			newFile_   = false;
			return;
		}
		const uint16_t first = shuffle() ? swapOf(0) : (uint16_t)0x100;
		if (first != setupFirst_ && SMS_API_HAS(api().game, warp)) {
			startPlaced_ = first; // already the shuffle's: not swapped again as it loads
			game().warp(first >> 8, first & 0xFF);
		}
	}

	// ---- the excluded locations tab: entrances (by area), Shines and blue coins
	std::vector<int> exclAreas() const
	{
		std::vector<int> v;
		for (int i = 0; i < gen::kLevelCount; ++i) {
			const int a = gen::kLevels[i].id >> 8;
			if (a >= 2 && std::find(v.begin(), v.end(), a) == v.end())
				v.push_back(a);
		}
		return v;
	}
	// every level an entrance can lead to (a world's episodes, the secret courses...): by episode
	std::vector<uint16_t> exclLevels() const
	{
		std::vector<uint16_t> v;
		for (int i = 0; i < gen::kLevelCount; ++i) {
			if ((gen::kLevels[i].id >> 8) >= 2)
				v.push_back(gen::kLevels[i].id);
		}
		return v;
	}
	int exclCount(int cat) const
	{
		return cat == 0 ? (int)exclAreas().size() : cat == 1 ? 120 : cat == 2 ? gen::kApCoinCount : (int)exclLevels().size();
	}
	std::string exclLabel(int cat, int i) const
	{
		if (cat == 0) {
			const int a = exclAreas()[i];
			if (areaTitle(a))
				return areaTitle(a);
			const char* an = SMS_API_HAS(api().game, area_name) ? game().area_name(a) : nullptr;
			return an && *an ? titleCase(an) : "Area " + std::to_string(a);
		}
		if (cat == 3)
			return levelName(exclLevels()[i]);
		if (cat == 1)
		{
			const std::string n = shineName(i); // the episode's own name, the plaza's task...
			return n == "?" ? "Shine " + std::to_string(i) : n;
		}
		return std::string(gen::kApCoins[i].world) + ": " + gen::kApCoins[i].name;
	}
	void exclRebuild()
	{
		auto lower = [](std::string v) {
			for (size_t i = 0; i < v.size(); ++i)
				v[i] = (char)tolower((unsigned char)v[i]);
			return v;
		};
		const std::string f = lower(exclFilter_);
		exclView_.clear();
		for (int i = 0; i < exclCount(exclCat_); ++i) {
			if (f.empty() || lower(exclLabel(exclCat_, i)).find(f) != std::string::npos)
				exclView_.push_back(i);
		}
		exclSel_ = exclTop_ = 0;
	}
	// the keys typed this frame into the filter (a key counts once as it goes down)
	bool exclTyped()
	{
		uint64_t now = 0;
		char name[2] = { 0, 0 };
		for (int k = 0; k < 38; ++k) {
			const char* key;
			if (k < 26) {
				name[0] = (char)('A' + k);
				key     = name;
			} else if (k < 36) {
				name[0] = (char)('0' + k - 26);
				key     = name;
			} else {
				key = k == 36 ? "Space" : "Backspace";
			}
			const int code = input().key_from_name(key);
			if (code >= 0 && input().key_down(code))
				now |= (uint64_t)1 << k;
		}
		const uint64_t fresh = now & ~exclKeysHeld_;
		exclKeysHeld_        = now;
		bool changed         = false;
		for (int k = 0; k < 38; ++k) {
			if (!(fresh & ((uint64_t)1 << k)))
				continue;
			if (k == 37) {
				if (!exclFilter_.empty())
					exclFilter_.pop_back();
			} else if (exclFilter_.size() < 24) {
				exclFilter_ += k < 26 ? (char)('a' + k) : k < 36 ? (char)('0' + k - 26) : ' ';
			}
			changed = true;
		}
		return changed;
	}
	bool exclHas(int cat, int i) const
	{
		if (cat == 0)
			return std::find(excl_.keep.begin(), excl_.keep.end(), (uint16_t)(exclAreas()[i] << 8 | 0xFF)) != excl_.keep.end();
		if (cat == 3)
			return std::find(excl_.keep.begin(), excl_.keep.end(), exclLevels()[i]) != excl_.keep.end();
		const std::vector<int>& v = cat == 1 ? excl_.shines : excl_.coins;
		return std::find(v.begin(), v.end(), i) != v.end();
	}
	// Adds or removes one, and keeps the list in exclusions.txt (the game is worked out again)
	void exclToggle(int cat, int i)
	{
		const bool had = exclHas(cat, i);
		if (cat == 0 || cat == 3) {
			const uint16_t k = cat == 3 ? exclLevels()[i] : (uint16_t)(exclAreas()[i] << 8 | 0xFF);
			if (had)
				excl_.keep.erase(std::find(excl_.keep.begin(), excl_.keep.end(), k));
			else
				excl_.keep.push_back(k);
		} else {
			std::vector<int>& v = cat == 1 ? excl_.shines : excl_.coins;
			if (had)
				v.erase(std::find(v.begin(), v.end(), i));
			else
				v.push_back(i);
		}
		std::string text;
		char buf[96];
		for (size_t k = 0; k < excl_.keep.size(); ++k) {
			snprintf(buf, sizeof buf, (excl_.keep[k] & 0xFF) == 0xFF ? "entrance %02X\n" : "entrance %04X\n",
			         (excl_.keep[k] & 0xFF) == 0xFF ? excl_.keep[k] >> 8 : excl_.keep[k]);
			text += buf;
		}
		for (size_t k = 0; k < excl_.shines.size(); ++k)
			text += "shine " + std::to_string(excl_.shines[k]) + "\n";
		for (size_t k = 0; k < excl_.coins.size(); ++k)
			text += std::string("coin ") + gen::kApCoins[excl_.coins[k]].world + "/" + gen::kApCoins[excl_.coins[k]].name + "\n";
		FILE* f = fopen((std::string(core().data_dir(handle())) + "/exclusions.txt").c_str(), "wb");
		if (f) {
			fwrite(text.data(), 1, text.size(), f);
			fclose(f);
		}
		exclText_    = text;
		appliedSeed_ = -1;
	}
	std::vector<int> setupTabRows() const
	{
		static const int k0[] = { ROW_SEED, ROW_NEW_SEED, ROW_SPOILER, ROW_START };
		static const int k1[] = { ROW_MODE, ROW_ALL, ROW_WIN, ROW_COINS, ROW_TRADES, ROW_BLUE, ROW_SWAPS, ROW_LIVES, ROW_GATES };
		return setupTab_ == 0 ? std::vector<int>(k0, k0 + 4) : std::vector<int>(k1, k1 + 9);
	}

	// ---- the plando tab: an unlock on a Shine or blue coin, an entrance to a level (kept in plando.txt)
	int plCat_ = 0;         // 0 unlocks, 1 entrances
	int plStage_ = 0;       // 0 the rows, 1 choosing what the row gets
	int plRow_ = 0;         // the row being set (stage 1)
	bool plDirty_ = true;   // the list must be made again
	std::vector<int> plView_;
	int plSel_ = 0, plTop_ = 0;

	int plCount(int stage, int cat) const
	{
		if (stage == 0)
			return cat == 0 ? kUnlockCount : 1 + (int)exclLevels().size();
		return cat == 0 ? 120 + gen::kApCoinCount : (int)exclLevels().size();
	}
	// the plando entry of a row (stage 0), or -1
	int plAssigned(int cat, int row) const
	{
		if (cat == 0) {
			const uint32_t skill = 1u << kUnlocks[row].bit;
			for (size_t i = 0; i < excl_.unlocks.size(); ++i) {
				if (excl_.unlocks[i].second == skill)
					return (int)i;
			}
			return -1;
		}
		const uint16_t key = row == 0 ? (uint16_t)0 : exclLevels()[row - 1];
		for (size_t i = 0; i < excl_.entrances.size(); ++i) {
			if (excl_.entrances[i].first == key)
				return (int)i;
		}
		return -1;
	}
	std::string plPlaceName(int place) const
	{
		return place >= gen::kApCoinBase ? "Blue coin: " + exclLabel(2, place - gen::kApCoinBase) : exclLabel(1, place);
	}
	std::string plLabel(int stage, int cat, int i) const
	{
		if (stage == 1)
			return cat == 0 ? (i < 120 ? exclLabel(1, i) : "Blue coin: " + exclLabel(2, i - 120)) : levelName(exclLevels()[i]);
		const int at = plAssigned(cat, i);
		if (cat == 0) {
			std::string out = kUnlocks[i].display;
			return at < 0 ? out : out + "  ->  " + plPlaceName(excl_.unlocks[at].first);
		}
		std::string out = i == 0 ? std::string("WHERE A NEW FILE STARTS") : levelName(exclLevels()[i - 1]);
		return at < 0 ? out : out + "  ->  " + levelName(excl_.entrances[at].second);
	}
	void plRebuild()
	{
		auto lower = [](std::string v) {
			for (size_t i = 0; i < v.size(); ++i)
				v[i] = (char)tolower((unsigned char)v[i]);
			return v;
		};
		const std::string f = lower(exclFilter_);
		plView_.clear();
		for (int i = 0; i < plCount(plStage_, plCat_); ++i) {
			if (f.empty() || lower(plLabel(plStage_, plCat_, i)).find(f) != std::string::npos)
				plView_.push_back(i);
		}
		plSel_ = plTop_ = 0;
		plDirty_       = false;
	}
	void savePlando()
	{
		std::string text;
		char buf[64];
		for (size_t i = 0; i < excl_.entrances.size(); ++i) {
			if (excl_.entrances[i].first == 0)
				snprintf(buf, sizeof buf, "entrance start = %04X", excl_.entrances[i].second);
			else
				snprintf(buf, sizeof buf, "entrance %04X = %04X", excl_.entrances[i].first, excl_.entrances[i].second);
			text += std::string(buf) + "\n";
		}
		for (size_t i = 0; i < excl_.unlocks.size(); ++i) {
			std::string name;
			for (int k = 0; k < kUnlockCount; ++k) {
				if (1u << kUnlocks[k].bit == excl_.unlocks[i].second)
					name = kUnlocks[k].name;
			}
			const int place = excl_.unlocks[i].first;
			text += "unlock " + name + " = "
			        + (place >= gen::kApCoinBase ? std::string("coin ") + gen::kApCoins[place - gen::kApCoinBase].world + "/"
			                                           + gen::kApCoins[place - gen::kApCoinBase].name
			                                     : "shine " + std::to_string(place))
			        + "\n";
		}
		FILE* f = fopen((std::string(core().data_dir(handle())) + "/plando.txt").c_str(), "wb");
		if (f) {
			fwrite(text.data(), 1, text.size(), f);
			fclose(f);
		}
		plandoText_  = text;
		appliedSeed_ = -1;
	}
	// one frame of the plando tab's controls (the filter is the excluded tab's)
	void plandoInput(int down)
	{
		if (plDirty_)
			plRebuild();
		if (exclTyped())
			plRebuild();
		const int n = (int)plView_.size();
		if (n > 0 && (down & SMS_PAD_UP))
			plSel_ = (plSel_ + n - 1) % n;
		if (n > 0 && (down & SMS_PAD_DOWN))
			plSel_ = (plSel_ + 1) % n;
		if (down & (SMS_PAD_LEFT | SMS_PAD_RIGHT)) {
			if (plStage_ == 0) {
				plCat_ ^= 1;
			} else {
				plStage_ = 0; // back to the rows
			}
			exclFilter_.clear();
			plRebuild();
		}
		if (plSel_ < plTop_)
			plTop_ = plSel_;
		if (plSel_ >= plTop_ + kExclRows)
			plTop_ = plSel_ - kExclRows + 1;
		if (n > 0 && plSel_ < n && (down & SMS_PAD_A)) {
			const int row = plView_[plSel_];
			if (plStage_ == 0) {
				const int at = plAssigned(plCat_, row);
				if (at >= 0) {
					// set already: taken out
					if (plCat_ == 0)
						excl_.unlocks.erase(excl_.unlocks.begin() + at);
					else
						excl_.entrances.erase(excl_.entrances.begin() + at);
					savePlando();
					plRebuild();
				} else {
					plRow_   = row;
					plStage_ = 1;
					exclFilter_.clear();
					plRebuild();
				}
			} else {
				if (plCat_ == 0) {
					const uint32_t skill = 1u << kUnlocks[plRow_].bit;
					const int place      = row < 120 ? row : gen::kApCoinBase + (row - 120);
					for (size_t i = excl_.unlocks.size(); i-- > 0;) {
						if (excl_.unlocks[i].first == place || excl_.unlocks[i].second == skill)
							excl_.unlocks.erase(excl_.unlocks.begin() + i);
					}
					excl_.unlocks.push_back(std::make_pair(place, skill));
				} else {
					const uint16_t key = plRow_ == 0 ? (uint16_t)0 : exclLevels()[plRow_ - 1];
					for (size_t i = excl_.entrances.size(); i-- > 0;) {
						if (excl_.entrances[i].first == key)
							excl_.entrances.erase(excl_.entrances.begin() + i);
					}
					excl_.entrances.push_back(std::make_pair(key, exclLevels()[row]));
				}
				savePlando();
				plStage_ = 0;
				exclFilter_.clear();
				plRebuild();
			}
		}
		refresh();
	}
	void drawPlando(const SmsEvDraw& d, float x, float y, float pw, float ph)
	{
		const SmsColor gold = SMS_RGB(255, 220, 90), white = SMS_RGB(255, 255, 255), pale = SMS_RGB(255, 235, 140);
		const int n = (int)plView_.size();
		std::string head = plStage_ == 0 ? std::string("< ") + (plCat_ == 0 ? "UNLOCKS" : "ENTRANCES") + " >"
		                                 : "WHAT FOR " + std::string(plCat_ == 0 ? kUnlocks[plRow_].display : plLabel(0, 1, plRow_).c_str());
		draw().text(x + pw / 2, y + 48, 14, gold, SMS_TEXT_CENTER | SMS_TEXT_SHADOW,
		            (head + "   FILTER: " + exclFilter_ + "_").c_str());
		if (n == 0)
			draw().text(x + pw / 2, y + 78, 12, SMS_RGBA(255, 255, 255, 150), SMS_TEXT_CENTER, "NO ROW HAS THAT");
		for (int r = 0; r < kExclRows && plTop_ + r < n; ++r) {
			const int vi   = plTop_ + r;
			const int i    = plView_[vi];
			const float ry = y + 78 + r * 26;
			const bool sel = vi == plSel_;
			if (sel)
				draw().rect(x + 14, ry - 3, pw - 28, 24, SMS_RGBA(255, 220, 90, 60));
			const bool set = plStage_ == 0 && plAssigned(plCat_, i) >= 0;
			std::string label = std::string(set ? "[X] " : plStage_ == 0 ? "[  ] " : "") + plLabel(plStage_, plCat_, i);
			while (label.size() > 8 && draw().text_width(13, label.c_str()) > pw - 44)
				label = label.substr(0, label.size() - 4) + "...";
			draw().text(x + 24, ry, 13, sel ? pale : white, SMS_TEXT_SHADOW, label.c_str());
		}
		draw().text(x + pw / 2, y + ph - 40, 10, SMS_RGBA(255, 255, 255, 140), SMS_TEXT_CENTER,
		            plStage_ == 0 ? "A: SET A ROW, OR TAKE IT OUT   LEFT/RIGHT: UNLOCKS / ENTRANCES"
		                          : "A: PUT IT THERE   LEFT/RIGHT: BACK   TYPE: FILTER");
		draw().text(x + pw / 2, y + ph - 22, 11, SMS_RGBA(255, 255, 255, 170), SMS_TEXT_CENTER,
		            "UP/DOWN: ROW   BACKSPACE: ERASE   L/R: TAB   B: CLOSE");
		(void)d;
	}

	void setupSet(const char* key, int value)
	{
		config().set_int(handle(), key, value);
		config().save(handle());
	}

	// One frame of the setup's controls: up and down choose a row, left and right
	// change it, A does it, B closes it (the game starts as set).
	void setupInput()
	{
		SmsPad pad = SmsPad();
		pad.size   = sizeof pad;
		if (!input().pad(0, &pad))
			return;
		int held = pad.held;
		if (pad.stick_y > 0.6f)
			held |= SMS_PAD_UP;
		if (pad.stick_y < -0.6f)
			held |= SMS_PAD_DOWN;
		if (pad.stick_x < -0.6f)
			held |= SMS_PAD_LEFT;
		if (pad.stick_x > 0.6f)
			held |= SMS_PAD_RIGHT;
		// the keyboard too: arrows, Return (A), Backspace (B; Escape is the port's quit),
		// Page Up / Page Down (R / L)
		static const struct {
			const char* key;
			int button;
		} kKeys[] = { { "Up", SMS_PAD_UP }, { "Down", SMS_PAD_DOWN }, { "Left", SMS_PAD_LEFT },
			          { "Right", SMS_PAD_RIGHT }, { "Return", SMS_PAD_A }, { "Backspace", SMS_PAD_B },
			          { "PageUp", SMS_PAD_R }, { "PageDown", SMS_PAD_L } };
		for (const auto& k : kKeys) {
			if ((setupRow_ == ROW_SEED || setupTab_ == 2) && k.button == SMS_PAD_B)
				continue; // Backspace erases a digit (or a letter of the filter) there (the pad's B still closes)
			const int code = input().key_from_name(k.key);
			if (code >= 0 && input().key_down(code))
				held |= k.button;
		}
		const int down = held & ~setupHeld_;
		setupHeld_     = held;
		if (down & (SMS_PAD_UP | SMS_PAD_DOWN))
			seedTyping_ = false;
		if (down & SMS_PAD_B) {
			closeSetup();
			return;
		}
		// L and R go from tab to tab
		if (down & (SMS_PAD_L | SMS_PAD_R)) {
			setupTab_   = (setupTab_ + ((down & SMS_PAD_R) ? 1 : 3)) % 4;
			plDirty_    = true;
			plStage_    = 0;
			exclFilter_.clear();
			seedTyping_ = false;
			setupRow_   = setupTab_ == 0 ? (int)ROW_START : (int)ROW_MODE;
			return;
		}
		if (setupTab_ == 3) {
			plandoInput(down);
			return;
		}
		if (setupTab_ == 2) {
			// the excluded locations: typing filters the list; up and down a row, left and right the
			// list (entrances, Shines, blue coins), A puts the row in or takes it out
			if (exclView_.empty() && exclFilter_.empty())
				exclRebuild();
			if (exclTyped())
				exclRebuild();
			const int n = (int)exclView_.size();
			if (n > 0 && (down & SMS_PAD_UP))
				exclSel_ = (exclSel_ + n - 1) % n;
			if (n > 0 && (down & SMS_PAD_DOWN))
				exclSel_ = (exclSel_ + 1) % n;
			if (down & (SMS_PAD_LEFT | SMS_PAD_RIGHT)) {
				exclCat_ = (exclCat_ + ((down & SMS_PAD_RIGHT) ? 1 : 3)) % 4;
				exclRebuild();
			}
			if (exclSel_ < exclTop_)
				exclTop_ = exclSel_;
			if (exclSel_ >= exclTop_ + kExclRows)
				exclTop_ = exclSel_ - kExclRows + 1;
			if (n > 0 && exclSel_ < n && (down & SMS_PAD_A))
				exclToggle(exclCat_, exclView_[exclSel_]);
			refresh();
			return;
		}
		{
			const std::vector<int> rows = setupTabRows();
			size_t at = 0;
			while (at < rows.size() && rows[at] != setupRow_)
				++at;
			if (at == rows.size())
				at = 0;
			if (down & SMS_PAD_UP)
				at = (at + rows.size() - 1) % rows.size();
			if (down & SMS_PAD_DOWN)
				at = (at + 1) % rows.size();
			setupRow_ = rows[at];
		}
		const int step = (down & SMS_PAD_RIGHT) ? 1 : (down & SMS_PAD_LEFT) ? -1 : 0;
		const bool a   = (down & SMS_PAD_A) != 0; // not Start: spamming it must not change a row or close the menu
		switch (setupRow_) {
		case ROW_SEED: {
			// left and right a step of 1
			int d = step;
			// digits typed replace the number (the first one starts it over), Backspace erases one
			int digits = 0;
			for (int n = 0; n < 10; ++n) {
				const std::string a = std::to_string(n), b = "Keypad " + a;
				const int c1 = input().key_from_name(a.c_str()), c2 = input().key_from_name(b.c_str());
				if ((c1 >= 0 && input().key_down(c1)) || (c2 >= 0 && input().key_down(c2)))
					digits |= 1 << n;
			}
			const int bs = input().key_from_name("Backspace");
			if (bs >= 0 && input().key_down(bs))
				digits |= 1 << 10;
			const int fresh = digits & ~seedDigitsHeld_;
			seedDigitsHeld_ = digits;
			for (int n = 0; n <= 10; ++n) {
				if (!(fresh & (1 << n)))
					continue;
				long v = seedTyping_ ? seed() : 0;
				v      = n == 10 ? v / 10 : v * 10 + n;
				if (v > kMaxSeed || (n != 10 && v < 1))
					continue;
				seedTyping_ = true;
				setupSet("seed", (int)(v < 1 ? 1 : v));
			}
			if (d) {
				seedTyping_ = false;
				int v = seed() + d;
				v     = v < 1 ? kMaxSeed : v > kMaxSeed ? 1 : v;
				setupSet("seed", v);
			}
			break; // A leaves the number as set: "New seed" is the row below
		}
		case ROW_NEW_SEED:
			if (a || step)
				drawSeed();
			break;
		case ROW_MODE:
			if (a || step)
				setupSet("entrance_mode", (entranceSetting() + (step < 0 ? 5 : 1)) % 6);
			break;
		case ROW_ALL:
			if (a || step)
				setupSet("all_unlocks", allUnlocks() ? 0 : 1);
			break;
		case ROW_WIN:
			if (a || step) {
				const int w = setting("win", 0) < 0 || setting("win", 0) > 5 ? 0 : setting("win", 0);
				setupSet("win", (w + (step < 0 ? 5 : 1)) % 6);
			}
			break;
		case ROW_LIVES:
			if (a || step)
				setupSet("infinite_lives", setting("infinite_lives", 0) ? 0 : 1);
			break;
		case ROW_COINS:
			if (a || step)
				setupSet("coin_shines", coinShines() ? 0 : 1);
			break;
		case ROW_TRADES:
			if (a || step)
				setupSet("trade_shines", tradeShines() ? 0 : 1);
			break;
		case ROW_SWAPS:
			if (a || step)
				setupSet("shine_swaps", shineSwaps() ? 0 : 1);
			break;
		case ROW_BLUE:
			if (a || step)
				setupSet("coin_unlocks", coinUnlocks() ? 0 : 1);
			break;
		case ROW_GATES:
			if (a || step)
				setupSet("open_gates", setting("open_gates", 0) ? 0 : 1);
			break;
		case ROW_SPOILER:
			if (a) {
				writeSpoiler();
				spoilerSeed_ = seed();
			}
			break;
		case ROW_START:
			if (a)
				closeSetup();
			break;
		}
		refresh();
	}

	void drawSetup(const SmsEvDraw& d)
	{
		static const char* const kWinNames[] = { "ALL SHADOW MARIOS", "16 SHINES", "30 SHINES",
			                                     "50 SHINES", "60 SHINES", "96 SHINES" };
		static const char* const kTabs[4] = { "PLAY", "OPTIONS", "EXCLUDED LOCATIONS", "PLANDO" };
		static const char* const kCats[4] = { "ENTRANCES (ZONES)", "SHINES", "BLUE COINS", "ENTRANCES (EPISODES)" };
		const int w      = setting("win", 0) < 0 || setting("win", 0) > 5 ? 0 : setting("win", 0);
		const float pw = 640, ph = 440, x = (d.canvas_w - pw) / 2, y = (d.canvas_h - ph) / 2;
		const SmsColor gold = SMS_RGB(255, 220, 90), white = SMS_RGB(255, 255, 255), pale = SMS_RGB(255, 235, 140);
		draw().rect(x, y, pw, ph, SMS_RGBA(10, 14, 40, 225));
		draw().rect_outline(x, y, pw, ph, 2, SMS_RGBA(255, 220, 90, 255));
		// the tabs
		float tx = x + 24;
		for (int t = 0; t < 4; ++t) {
			const float tw = draw().text_width(15, kTabs[t]);
			draw().text(tx, y + 14, 15, t == setupTab_ ? gold : SMS_RGBA(255, 255, 255, 150), SMS_TEXT_SHADOW, kTabs[t]);
			if (t == setupTab_)
				draw().rect(tx - 6, y + 38, tw + 12, 3, SMS_RGBA(255, 220, 90, 255));
			tx += tw + 40;
		}
		draw().rect(x, y + 42, pw, 1, SMS_RGBA(255, 220, 90, 120));
		if (planFailed_) {
			// no game could be worked out: the exclusions leave nowhere for the unlocks (or the entrances no way)
			draw().rect(x + 14, y + ph - 64, pw - 28, 22, SMS_RGBA(150, 20, 20, 235));
			draw().text(x + pw / 2, y + ph - 61, 12, SMS_RGB(255, 255, 255), SMS_TEXT_CENTER | SMS_TEXT_SHADOW,
			            "NO GAME: CHECK THE EXCLUSIONS AND THE PLANDO, OR TRY ANOTHER SEED");
		}
		if (setupTab_ == 3) {
			drawPlando(d, x, y, pw, ph);
			return;
		}
		if (setupTab_ == 2) {
			// left the list being chosen from, right what is excluded
			const float lw = 360, ly = y + 78;
			const int n = (int)exclView_.size();
			draw().text(x + lw / 2, y + 48, 14, gold, SMS_TEXT_CENTER | SMS_TEXT_SHADOW,
			            (std::string("< ") + kCats[exclCat_] + " >   FILTER: " + exclFilter_ + "_").c_str());
			if (n == 0)
				draw().text(x + lw / 2, ly, 12, SMS_RGBA(255, 255, 255, 150), SMS_TEXT_CENTER, "NO ROW HAS THAT");
			for (int r = 0; r < kExclRows && exclTop_ + r < n; ++r) {
				const int vi    = exclTop_ + r;
				const int i     = exclView_[vi];
				const float ry  = ly + r * 26;
				const bool sel  = vi == exclSel_;
				if (sel)
					draw().rect(x + 14, ry - 3, lw - 28, 24, SMS_RGBA(255, 220, 90, 60));
				std::string label = std::string(exclHas(exclCat_, i) ? "[X] " : "[  ] ") + exclLabel(exclCat_, i);
				while (label.size() > 8 && draw().text_width(13, label.c_str()) > lw - 44)
					label = label.substr(0, label.size() - 4) + "...";
				draw().text(x + 24, ry, 13, sel ? pale : white, SMS_TEXT_SHADOW, label.c_str());
			}
			draw().rect(x + lw, y + 44, 1, ph - 88, SMS_RGBA(255, 220, 90, 90));
			draw().text(x + lw + (pw - lw) / 2, y + 52, 15, gold, SMS_TEXT_CENTER | SMS_TEXT_SHADOW, "EXCLUDED");
			std::vector<std::string> shown;
			for (int c = 0; c < 4; ++c) {
				for (int i = 0; i < exclCount(c); ++i) {
					if (exclHas(c, i))
						shown.push_back(exclLabel(c, i));
				}
			}
			const int most = kExclRows;
			for (int r = 0; r < most && r < (int)shown.size(); ++r) {
				std::string label = shown[r];
				while (label.size() > 8 && draw().text_width(12, label.c_str()) > pw - lw - 36)
					label = label.substr(0, label.size() - 4) + "...";
				draw().text(x + lw + 18, ly + r * 26, 12, white, SMS_TEXT_SHADOW, label.c_str());
			}
			if ((int)shown.size() > most)
				draw().text(x + lw + 18, ly + most * 26, 12, SMS_RGBA(255, 255, 255, 170), SMS_TEXT_SHADOW,
				            ("+ " + std::to_string((int)shown.size() - most) + " MORE").c_str());
			if (shown.empty())
				draw().text(x + lw + (pw - lw) / 2, ly, 12, SMS_RGBA(255, 255, 255, 150), SMS_TEXT_CENTER,
				            "NOTHING: A PUTS THE ROW HERE");
			draw().text(x + pw / 2, y + ph - 22, 11, SMS_RGBA(255, 255, 255, 170), SMS_TEXT_CENTER,
			            "TYPE: FILTER   BACKSPACE: ERASE   UP/DOWN: ROW   LEFT/RIGHT: LIST   A: EXCLUDE   L/R: TAB");
			draw().text(x + pw / 2, y + ph - 40, 10, SMS_RGBA(255, 255, 255, 140), SMS_TEXT_CENTER,
			            "AN ENTRANCE KEEPS ITS LEVEL; A SHINE OR BLUE COIN HOLDS NO UNLOCK");
			return;
		}
		char rows[ROW_COUNT][64];
		snprintf(rows[ROW_SEED], 64, "SEED        < %d >", seed());
		snprintf(rows[ROW_NEW_SEED], 64, "NEW SEED");
		snprintf(rows[ROW_MODE], 64, "ENTRANCES  < %s >", modeNames()[entranceSetting()]);
		snprintf(rows[ROW_ALL], 64, "ALL UNLOCKS START  %s", allUnlocks() ? "ON" : "OFF");
		snprintf(rows[ROW_WIN], 64, "WIN   < %s >", kWinNames[w]);
		snprintf(rows[ROW_LIVES], 64, "INFINITE LIVES     %s", setting("infinite_lives", 0) ? "ON" : "OFF");
		snprintf(rows[ROW_COINS], 64, "100-COIN SHINES    %s", coinShines() ? "ON" : "OFF");
		snprintf(rows[ROW_TRADES], 64, "BLUE COIN TRADES   %s", tradeShines() ? "ON" : "OFF");
		snprintf(rows[ROW_BLUE], 64, "BLUE COIN UNLOCKS  %s", coinUnlocks() ? "ON" : "OFF");
		snprintf(rows[ROW_SWAPS], 64, "SHINES <> COINS    %s", shineSwaps() ? "ON" : "OFF");
		snprintf(rows[ROW_GATES], 64, "OPEN PLAZA GATES   %s", setting("open_gates", 0) ? "ON" : "OFF");
		snprintf(rows[ROW_SPOILER], 64, "WRITE SPOILER LOG%s", spoilerSeed_ == seed() ? "  (WRITTEN)" : "");
		snprintf(rows[ROW_START], 64, "START");
		const std::vector<int> tabRows = setupTabRows();
		for (size_t k = 0; k < tabRows.size(); ++k) {
			const int i     = tabRows[k];
			const float ry  = y + 62 + (float)k * 30 + (i == ROW_START ? 10 : 0);
			const bool sel  = i == setupRow_;
			if (sel)
				draw().rect(x + 24, ry - 4, pw - 48, 26, SMS_RGBA(255, 220, 90, 60));
			draw().text(x + (i == ROW_START ? pw / 2 : 40), ry, 17, sel ? pale : white,
			            SMS_TEXT_SHADOW | (i == ROW_START ? SMS_TEXT_CENTER : 0), rows[i]);
		}
		// where the spoiler log went, its end when the whole path is too long
		if (setupTab_ == 0 && spoilerSeed_ == seed() && !spoilerPath_.empty()) {
			std::string shown = spoilerPath_;
			for (size_t i = 0; i < shown.size(); ++i) {
				if (shown[i] == '\\')
					shown[i] = '/';
			}
			while (shown.size() > 4 && draw().text_width(10, ("SAVED IN " + shown).c_str()) > pw - 24)
				shown = "..." + shown.substr(4);
			draw().text(x + pw / 2, y + ph - 40, 10, pale, SMS_TEXT_CENTER | SMS_TEXT_SHADOW, ("SAVED IN " + shown).c_str());
		}
		draw().text(x + pw / 2, y + ph - 22, 11, SMS_RGBA(255, 255, 255, 170), SMS_TEXT_CENTER,
		            "UP/DOWN: CHOOSE   LEFT/RIGHT: CHANGE   A: OK   L/R: TAB   B: CLOSE");
	}

	// ------------------------------------------------------------------ spoiler

	// The secret courses, bosses and other single areas, by the names the port's
	// achievements give them (ext/achievements/game/achievements.h).
	static const char* areaTitle(int area)
	{
		switch (area) {
		case 0x00: return "Delfino Airstrip";
		case 0x14: return "Delfino Airstrip (from the plaza)";
		case 0x15: return "Super Slide";
		case 0x16: return "Pachinko";
		case 0x17: return "Red Coin Field";
		case 0x18: return "Lily Pad Ride";
		case 0x1D: return "Turbo Track";
		case 0x1E: return "Blooper Surfing";
		case 0x1F: return "Noki Bay: the Shell's Secret";
		case 0x20: return "Gelato Beach: the Sand Castle's Secret";
		case 0x21: return "Gelato Beach: the Sand Bird";
		case 0x28: return "Casino Delfino's secret";
		case 0x29: return "Pinna Park: the Yoshi-Go-Round's Secret";
		case 0x2A: return "Pianta Village: Secret of the Village Underside";
		case 0x2C: return "Noki Bay: Red Coins in a Bottle";
		case 0x2E: return "Bianco Hills: the Secret of the Dirty Lake";
		case 0x2F: return "Bianco Hills: the Hillside Cave Secret";
		case 0x30: return "Ricco Harbor: the Secret of Ricco Tower";
		case 0x32: return "Pinna Park: the Beach Cannon's Secret";
		case 0x33: return "Hotel Delfino: the Hotel Lobby's Secret";
		case 0x34: return "Corona Mountain";
		case 0x37: return "Bianco Hills: the Petey Piranha fight";
		case 0x38: return "Hotel Delfino: the King Boo fight";
		case 0x39: return "Noki Bay: the Eely-Mouth fight";
		case 0x3A: return "Pinna Park: the roller coaster";
		case 0x3B: return "Ricco Harbor: the Gooper Blooper fight";
		case 0x3C: return "Corona Mountain: the Bowser fight";
		}
		return nullptr;
	}

	// "BIANCO HILLS" as "Bianco Hills": the game's area names are in capitals.
	static std::string titleCase(const char* text)
	{
		std::string out = text;
		bool start      = true;
		for (size_t i = 0; i < out.size(); ++i) {
			const char ch = out[i];
			if (ch >= 'A' && ch <= 'Z' && !start)
				out[i] = (char)(ch - 'A' + 'a');
			start = !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '\'');
		}
		return out;
	}

	// A level as the player knows it: a world's episode by its number and name ("BIANCO
	// HILLS 3: Down with Petey Piranha!"), a secret course or a boss by its name, another
	// part of a world by its area and scenario.
	std::string levelName(uint16_t level) const
	{
		const int area = level >> 8, ep = level & 0xFF;
		if (areaTitle(area))
			return areaTitle(area);
		const char* an  = SMS_API_HAS(api().game, area_name) ? game().area_name(area) : nullptr;
		std::string out = an && *an ? titleCase(an) : "Area " + std::to_string(area);
		const int kind  = SMS_API_HAS(&api(), world) ? api().world->area_kind(area) : SMS_AREA_NONE;
		char buf[96]    = "";
		switch (kind) {
		case SMS_AREA_WORLD:
			out += " " + std::to_string(ep + 1);
			if (api().world->episode_name(area, ep, buf, sizeof buf) > 0)
				out += std::string(": ") + buf;
			break;
		case SMS_AREA_PLAZA:
			break;
		case SMS_AREA_SECRET:
			out += " (secret course)";
			break;
		default:
			out += " (scenario " + std::to_string(ep + 1) + ")";
		}
		return out;
	}

	// A 100-coin Shine's world (generator.h's coinShineOf), or null.
	static const char* coinShineWorld(int shine)
	{
		// where the logic counts them (generator.h's coinShineOf)
		static const char* const kWorlds[] = { "Bianco Hills", "Ricco Harbor (episodes 2 to 8)",
			                                   "Gelato Beach (episodes 2 and 7)", "Pinna Park", "Sirena Beach",
			                                   "Noki Bay (episodes 1, 2 and 4 to 8)", "Pianta Village", "Delfino Plaza" };
		return shine >= 100 && shine <= 107 ? kWorlds[shine - 100] : nullptr;
	}

	// What a Shine is, as the game names it: an episode's Shine by its world, number and
	// name; a 100-coin Shine by its world; another (a secret course's, the plaza's) by
	// the level it is in.
	// A place an unlock is on, by where it is: a swapped Shine is had at its blue coin,
	// a swapped blue coin at its Shine.
	std::string placeName(int place) const
	{
		if (place >= 0 && place < 121 && shineCoin_[place] >= 0)
			return shineName(gen::kApCoinBase + shineCoin_[place]) + " (gives a Shine)";
		if (place >= gen::kApCoinBase && coinShine_[place - gen::kApCoinBase] >= 0)
			return shineName(coinShine_[place - gen::kApCoinBase]) + " (gives a blue coin)";
		return shineName(place);
	}

	std::vector<uint16_t> placeLevels(int place) const
	{
		if (place >= 0 && place < 121 && shineCoin_[place] >= 0)
			place = gen::kApCoinBase + shineCoin_[place];
		else if (place >= gen::kApCoinBase && coinShine_[place - gen::kApCoinBase] >= 0)
			place = coinShine_[place - gen::kApCoinBase];
		if (place >= gen::kApCoinBase) {
			const gen::ApCoin& c = gen::kApCoins[place - gen::kApCoinBase];
			return std::vector<uint16_t>(c.levels, c.levels + c.levelCount);
		}
		return levelsOfShine(place);
	}

	std::string shineName(int shine) const
	{
		if (shine >= gen::kApCoinBase) {
			const gen::ApCoin& c = gen::kApCoins[shine - gen::kApCoinBase];
			return std::string("Blue coin in ") + c.world + ": " + c.name;
		}
		if (coinShineWorld(shine))
			return std::string("100 coins in ") + coinShineWorld(shine);
		for (int k = 0; k < 24; ++k) {
			if (gen::kTradeShines[k] == shine)
				return "Blue coin trade " + std::to_string(k + 1) + " at the boathouse (" + std::to_string(10 * (k + 1))
				       + " blue coins)";
		}
		// the plaza's and the airstrip's, from their names in the stages' scenes
		switch (shine) {
		// the 9th and 10th episodes (the game's data gives these the name of an episode they are
		// found in; the episodes' names are from mariowiki.com)
		case 18: return "Ricco Harbor 9: Red Coins in Ricco Tower";
		case 19: return "Ricco Harbor 10: Blooper-Surfing Sequel";
		case 28: return "Gelato Beach 9: Red Coins in the Sand Castle";
		case 29: return "Gelato Beach 10: Sandy Shine Sprite";
		case 38: return "Pinna Park 9: Red Coins in the Cannon";
		case 39: return "Pinna Park 10: Red Coins in the Yoshi-Go-Round";
		case 48: return "Sirena Beach 9: Red Coins in Boo's Big Mouth";
		case 49: return "Sirena Beach 10: Red Coin Winnings in the Casino";
		case 58: return "Noki Bay 10: Red Coins on the Half Shell";
		case 59: return "Noki Bay 9: A Golden Bird";
		case 68: return "Pianta Village 9: Red Coin Chucksters";
		case 69: return "Pianta Village 10: Soak the Sun";
		case 86: return "Delfino Airstrip: Red Coin Waterworks (red coins)";
		// a secret course's Shine: its level holds it alone; the plaza's list (mariowiki.com) says clearing the course
		// the airstrip's two Shines: the tracker pack puts 88 on the runway's Shine (the first, found by
		// defeating the Gatekeeper) and 86 on the red coins' (mariowiki.com names the two episodes);
		// 119 is in no level of the data: Corona Mountain's, the win condition
		case 88: return "Delfino Airstrip: Delfino Airstrip Dilemma";
		case 119: return "Corona Mountain 1: Father and Son Shine!";
		case 87: return "Turbo Track: clear the course";
		case 89: return "Pachinko Game: clear the course";
		case 90: return "Super Slide: clear the course";
		case 91: return "Lily Pad Ride: clear the course";
		case 92: return "Red Coin Field: clear the course";
		case 93: return "Delfino Plaza: the super ground-pound block";
		case 94: return "Delfino Plaza: the wooden crate game (first)";
		case 95: return "Delfino Plaza: the wooden crate game (second)";
		case 96: return "Delfino Plaza: the bell tower on Ricco Harbor's side";
		case 97: return "Delfino Plaza: the bell tower on Gelato Beach's side";
		case 98: return "Delfino Plaza: above the Pachinko's entrance";
		case 99: return "Delfino Plaza: the goop-covered Shine on the Shine Gate";
		case 115: return "Delfino Plaza: the raccoon parent and child, high up on Ricco Harbor's side";
		case 116: return "Delfino Plaza: behind the turbo nozzle door at the foot of the bell tower, by Yoshi's egg";
		case 117: return "Delfino Plaza: the hidden Shine drawing";
		case 118: return "Delfino Plaza: the bird";
		}
		if (SMS_API_HAS(&api(), world)) {
			int area     = -1;
			const int ep = api().world->shine_episode(shine, &area);
			if (ep >= 0 && area >= 0)
				return levelName((uint16_t)(area << 8 | ep));
		}
		// a secret course's second Shine is its red coins'
		static const int kRedCoins[] = { 8, 9, 18, 23, 28, 38, 39, 48, 49, 52, 57, 58, 68, 88, 89, 91, 92 };
		bool red = false;
		for (size_t i = 0; i < sizeof kRedCoins / sizeof kRedCoins[0]; ++i)
			red = red || kRedCoins[i] == shine;
		const std::vector<uint16_t> levels = levelsOfShine(shine);
		if (levels.empty())
			return "?";
		return levelName(levels[0]) + (red ? " (red coins)" : "");
	}

	// The levels whose Shines include `shine`, in the randomizer's data.
	std::vector<uint16_t> levelsOfShine(int shine) const
	{
		std::vector<uint16_t> out;
		for (int i = 0; i < gen::kLevelCount; ++i) {
			const gen::LevelData& L = gen::kLevels[i];
			for (int k = 0; k < L.shineCount; ++k) {
				if ((gen::kShines[L.shineFirst + k].id >> 8) == shine) {
					out.push_back(L.id);
					break;
				}
			}
		}
		return out;
	}

	// The unlock a skill bit is (kUnlocks), or null (talking).
	static const Unlock* unlockOfBit(uint32_t bit)
	{
		for (int i = 0; i < kUnlockCount; ++i) {
			if ((1u << kUnlocks[i].bit) == bit)
				return &kUnlocks[i];
		}
		return nullptr;
	}

	// The seed's game as a text file in the mod's data folder: its spheres (what can be
	// had from the start, then what each sphere's unlocks open), which Shine gives which
	// unlock (where it is, and the entrance that leads there), and the level each
	// entrance loads.
	void writeSpoiler()
	{
		refresh();
		const std::string path = std::string(core().data_dir(handle())) + "/spoiler-" + std::to_string(seed()) + ".txt";
		FILE* f                = fopen(path.c_str(), "w");
		if (!f) {
			warn("cannot write %s", path.c_str());
			return;
		}
		const int win = kWins[setting("win", 0) < 0 || setting("win", 0) > 5 ? 0 : setting("win", 0)];
		fprintf(f, "Randomizer spoiler\nSeed: %d (%08X)\nShuffle levels: %s\nEntrances: %s\nStart with all unlocks: %s\n", seed(),
		        (unsigned)seed(), shuffle() ? "yes" : "no", modeNames()[entranceSetting()], allUnlocks() ? "yes" : "no");
		if (win > 0)
			fprintf(f, "Win condition: %d Shines\n", win);
		else
			fprintf(f, "Win condition: all Shadow Marios\n");
		// the spheres, worked out again from the seed's game
		{
			const gen::Plan plan = makePlan(allUnlocks());
			const std::vector<gen::Sphere> spheres = gen::spheres(plan, shuffle());
			fprintf(f, "\nSPHERES (sphere 1: what can be had from the start; each next: what the unlocks before open)\n");
			bool corona = false;
			for (size_t k = 0; k < spheres.size(); ++k) {
				fprintf(f, "Sphere %d%s\n", (int)k + 1,
				        spheres[k].corona && !corona ? " (every Shadow Mario reachable: Corona Mountain)" : "");
				corona = corona || spheres[k].corona;
				for (size_t u = 0; u < spheres[k].unlocks.size(); ++u) {
					const Unlock* un = unlockOfBit(spheres[k].unlocks[u].second);
					if (!un)
						continue;
					const int sh = spheres[k].unlocks[u].first;
					fprintf(f, "    %s: %s\n", placeName(sh).c_str(), un->display);
				}
			}
		}
		fprintf(f, "\nUNLOCKS\n");
		for (int i = 0; i < kUnlockCount; ++i) {
			if (shine_[i] == kFromStart) {
				fprintf(f, "%-20s from the start\n", kUnlocks[i].display);
				continue;
			}
			if (shine_[i] < 0) {
				fprintf(f, "%-20s nowhere\n", kUnlocks[i].display);
				continue;
			}
			fprintf(f, "%s: %s\n", placeName(shine_[i]).c_str(), kUnlocks[i].display);
			if (coinShineWorld(shine_[i])) {
				fprintf(f, "%-20s   100 coins in %s\n", "", coinShineWorld(shine_[i]));
				// the episodes it may be done in, and the entrance each is behind
				for (int li = 0; li < gen::kLevelCount; ++li) {
					const uint16_t lv = gen::kLevels[li].id;
					if (gen::coinShineOf(lv) != shine_[i] || lv == 0x108)
						continue;
					fprintf(f, "%-20s   in %s", "", levelName(lv).c_str());
					bool shown = false;
					for (size_t j = 0; shuffle() && j < swaps_.size(); ++j) {
						if (swaps_[j].second == lv) {
							fprintf(f, ", behind %s", swaps_[j].first == 0 ? "where a new file starts"
							                                                 : ("the entrance to " + levelName(swaps_[j].first)).c_str());
							shown = true;
						}
					}
					fprintf(f, "%s\n", shuffle() && !shown ? " (not placed by this seed)" : "");
				}
				continue;
			}
			const std::vector<uint16_t> levels = placeLevels(shine_[i]);
			for (size_t k = 0; k < levels.size(); ++k) {
				fprintf(f, "%-20s   in %s\n", "", levelName(levels[k]).c_str());
				if (!shuffle())
					continue;
				for (size_t j = 0; j < swaps_.size(); ++j) {
					if (swaps_[j].second == levels[k] && swaps_[j].first == levels[k] && (levels[k] >> 8) != 1)
						fprintf(f, "%-20s   behind its own entrance (not moved)\n", "");
					if (swaps_[j].second == levels[k] && swaps_[j].first != levels[k])
						fprintf(f, "%-20s   %s\n", "",
						        swaps_[j].first == 0 ? "where a new file starts"
						                             : ("behind the entrance to " + levelName(swaps_[j].first)).c_str());
				}
			}
		}
		if (shineSwaps()) {
			fprintf(f, "\nSHINES <> BLUE COINS (the Shine there gives a blue coin, the blue coin the Shine)\n");
			for (int i = 0; i < 121; ++i) {
				if (shineCoin_[i] >= 0)
					fprintf(f, "%s\n    <> %s\n", shineName(i).c_str(), shineName(gen::kApCoinBase + shineCoin_[i]).c_str());
			}
		}
		if (shuffle()) {
			fprintf(f, "\nLEVELS (the entrance to -> the level it loads)\n");
			for (size_t j = 0; j < swaps_.size(); ++j) {
				if (swaps_[j].first == swaps_[j].second)
					continue;
				const std::string from = swaps_[j].first == 0 ? std::string("a new file's start") : levelName(swaps_[j].first);
				fprintf(f, "%s\n    -> %s\n", from.c_str(), levelName(swaps_[j].second).c_str());
			}
		}
		// every Shine with the name the lists show it by (the excluded locations' too)
		fprintf(f, "\nSHINE NAMES\n");
		for (int i = 0; i < 120; ++i)
			fprintf(f, "%3d  %s\n", i, exclLabel(1, i).c_str());
		fclose(f);
		spoilerPath_ = path;
		info("spoiler written: %s", path.c_str());
	}

	// the entrance shuffle, in the order of the option: 0 off, 1 worlds, 2 secrets, 3 both apart, 4 secrets and the rest mixed, 5 all mixed
	static const char* const* modeNames() { static const char* const k[6] = { "OFF", "WORLDS", "SECRETS", "WORLDS+SECRETS", "SECRETS, REST MIXED", "ALL MIXED" }; return k; }
	int entranceSetting() const { const int m = setting("entrance_mode", 0); return m < 0 || m > 5 ? 0 : m; }
	// (Off with plando entrances still moves those: the generator's mode 5, the levels otherwise in place)
	bool shuffle() const { return entranceSetting() != 0 || !excl_.entrances.empty(); }
	// which entrances the shuffle moves, and among which (0 all mixed ... 4, the generator's)
	int entranceMode() const { const int m = entranceSetting(); return m == 5 ? 0 : m == 0 ? 5 : m; } // the generator's mode (0 all mixed, 1 worlds, 2 secrets, 3 both, 4 secrets and the rest mixed)
	// the 100-coin Shines hold unlocks too (generator.h; not the randomizer's)
	bool coinShines() const { return setting("coin_shines", 0) != 0; }
	// the blue coin trades hold unlocks too (generator.h; not the randomizer's)
	bool tradeShines() const { return setting("trade_shines", 0) != 0; }
	// the SMS apworld's blue coins holding unlocks (generator.h)
	// some Shines and blue coins trading places (generator.h; API 1.6 beta 2's quiet take)
	// the swaps work with all unlocks from the start too (nothing is then an unlock)
	bool swapsOn() { return enabled() && shineSwaps(); }
	bool shineSwaps() const
	{
#ifdef RANDOMIZER_16_BETA2
		return setting("shine_swaps", 0) != 0;
#else
		return false;
#endif
	}
	bool coinUnlocks() const { return setting("coin_unlocks", 0) != 0; }
	// the Shines that give a blue coin tinted blue (an option, on)
	bool tintSwaps() const { return setting("tint_swaps", 1) != 0; }

	// The seed's game (with every unlock from the start the walk can get stuck, as the
	// randomizer itself would: the levels are then shuffled as with the usual start).
	gen::Plan makePlan(bool all)
	{
		gen::Plan plan = generator_.run((uint32_t)seed(), all ? gen::kAllSkills : gen::kTalking, shuffle(), false,
		                                coinShines(), true, tradeShines(), true, coinUnlocks(),
		                                shineSwaps(), entranceMode(), &excl_);
		if (!plan.ok && all)
			plan = generator_.run((uint32_t)seed(), gen::kTalking, shuffle(), false, coinShines(), true, tradeShines(),
			                      true, coinUnlocks(), shineSwaps(), entranceMode(), &excl_);
		return plan;
	}

	// plando.txt (the same folder) is read with it. exclusions.txt (the mod's data folder), one per line, # for a comment:
	//   entrance 02      the entrance to every level of area 02 keeps its level (4 digits: one level, e.g. 0201)
	//   shine 12         no unlock on Shine 12
	//   coin Tourist     no unlock on that blue coin (its name, or World/Name)
	// Read when the mod loads and when the setup screen opens; a change works out the game again.
	void loadExclusions()
	{
		std::string text;
		FILE* f = fopen((std::string(core().data_dir(handle())) + "/exclusions.txt").c_str(), "rb");
		if (f) {
			char buf[512];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
				text.append(buf, n);
			fclose(f);
		}
		std::string ptext;
		FILE* pf = fopen((std::string(core().data_dir(handle())) + "/plando.txt").c_str(), "rb");
		if (pf) {
			char buf[512];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, pf)) > 0)
				ptext.append(buf, n);
			fclose(pf);
		}
		if (text == exclText_ && ptext == plandoText_)
			return;
		exclText_   = text;
		plandoText_ = ptext;
		excl_       = gen::Exclusions();
		auto lower = [](std::string v) {
			for (size_t i = 0; i < v.size(); ++i)
				v[i] = (char)tolower((unsigned char)v[i]);
			return v;
		};
		size_t pos = 0;
		while (pos < text.size()) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			std::string line = text.substr(pos, end - pos);
			pos              = end + 1;
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			const size_t sp = line.find(' ');
			if (line.empty() || line[0] == '#' || sp == std::string::npos)
				continue;
			const std::string key = lower(line.substr(0, sp)), arg = line.substr(sp + 1);
			if (key == "entrance") {
				const unsigned long v = strtoul(arg.c_str(), nullptr, 16);
				if (arg.size() <= 2)
					excl_.keep.push_back((uint16_t)(v << 8 | 0xFF));
				else
					excl_.keep.push_back((uint16_t)v);
			} else if (key == "shine") {
				excl_.shines.push_back(atoi(arg.c_str()));
			} else if (key == "coin") {
				bool found = false;
				for (int c = 0; c < gen::kApCoinCount; ++c) {
					const std::string name = lower(gen::kApCoins[c].name), full = lower(std::string(gen::kApCoins[c].world) + "/" + gen::kApCoins[c].name);
					if (lower(arg) == name || lower(arg) == full) {
						excl_.coins.push_back(c);
						found = true;
					}
				}
				if (!found)
					warn("exclusions.txt: no blue coin named '%s'", arg.c_str());
			}
		}

		// plando.txt: what the player places
		//   entrance 0200 = 0501      the entrance to level 0200 loads level 0501 (the key `start`: where a new file starts)
		//   unlock double jump = shine 12      that Shine (its number, or its name) gives the unlock
		//   unlock hover nozzle = coin Tourist  that blue coin does (its name, or World/Name)
		auto trim = [](std::string v) {
			while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
				v.pop_back();
			size_t b = 0;
			while (b < v.size() && (v[b] == ' ' || v[b] == '\t'))
				++b;
			return v.substr(b);
		};
		pos = 0;
		while (pos < ptext.size()) {
			size_t end = ptext.find('\n', pos);
			if (end == std::string::npos)
				end = ptext.size();
			const std::string line = trim(ptext.substr(pos, end - pos));
			pos                    = end + 1;
			const size_t eq        = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos)
				continue;
			const std::string left = trim(line.substr(0, eq)), right = trim(line.substr(eq + 1));
			const size_t sp = left.find(' ');
			if (sp == std::string::npos)
				continue;
			const std::string kind = lower(left.substr(0, sp)), what = lower(trim(left.substr(sp + 1)));
			if (kind == "entrance") {
				const unsigned long to = strtoul(right.c_str(), nullptr, 16);
				const unsigned long from = what == "start" ? 0 : strtoul(what.c_str(), nullptr, 16);
				excl_.entrances.push_back(std::make_pair((uint16_t)from, (uint16_t)to));
			} else if (kind == "unlock") {
				uint32_t skill = 0;
				for (int k = 0; k < kUnlockCount; ++k) {
					if (what == lower(kUnlocks[k].name) || what == lower(kUnlocks[k].display))
						skill = 1u << kUnlocks[k].bit;
				}
				const size_t rs = right.find(' ');
				if (!skill || rs == std::string::npos) {
					warn("plando.txt: unknown unlock or place in '%s'", line.c_str());
					continue;
				}
				const std::string rk = lower(right.substr(0, rs)), rw = lower(trim(right.substr(rs + 1)));
				int place = -1;
				if (rk == "shine") {
					for (int i = 0; i < 120 && place < 0; ++i) {
						if (rw == std::to_string(i) || rw == lower(exclLabel(1, i)))
							place = i;
					}
				} else if (rk == "coin") {
					for (int c = 0; c < gen::kApCoinCount && place < 0; ++c) {
						if (rw == lower(gen::kApCoins[c].name) || rw == lower(std::string(gen::kApCoins[c].world) + "/" + gen::kApCoins[c].name))
							place = gen::kApCoinBase + c;
					}
				}
				if (place < 0)
					warn("plando.txt: no place '%s'", right.c_str());
				else
					excl_.unlocks.push_back(std::make_pair(place, skill));
			}
		}
		appliedSeed_ = -1; // the game is worked out again
	}

	// Has the file the place an unlock is on (a Shine, or a blue coin: kApCoinBase + i)?
	bool placeHad(int place)
	{
		if (place >= gen::kApCoinBase) {
			const int c = place - gen::kApCoinBase;
			if (coinShine_[c] >= 0)
				return taken_[coinShine_[c]] != 0; // its Shine's place, taken quietly
			return game().flag_get(0x10000u + gen::kApCoins[c].bit) != 0;
		}
		return game().has_shine(place); // a swapped Shine's flag comes with its blue coin
	}

	// What the mod keeps for a save file beyond the game's own save (the unlocks had, the
	// Shines taken quietly, the levels been in) is written at once, by the file's slot,
	// not with the game's save: a blue coin does not save the game.
	int slot_ = -1;
	static std::string slotName(int slot, const char* name) { return "slot" + std::to_string(slot) + "-" + name; }
	bool putFile(const char* name, const std::string& data)
	{
		const std::string n = slotName(slot_, name);
		return api().files->write(handle(), SMS_FILES_MOD, n.c_str(), data.data(), (uint32_t)data.size()) != 0;
	}
	int getFile(const char* name, char* buf, uint32_t cap)
	{
		if (slot_ < 0)
			return -1;
		const std::string n = slotName(slot_, name);
		return api().files->read(handle(), SMS_FILES_MOD, n.c_str(), buf, cap);
	}

	static const char* slotFile(int i)
	{
		static const char* const kNames[] = { "earned.txt", "taken.txt", "levels.txt", "got.txt" };
		return kNames[i];
	}
	static const int kSlotFileCount = 4;

	// A save file erased at file select: all the mod kept for it goes, its spoiler log too.
	void eraseSlot(int slot)
	{
		if (!SMS_API_HAS(&api(), files) || slot < 0)
			return;
		char buf[64];
		const std::string e = slotName(slot, "earned.txt");
		const int n         = api().files->read(handle(), SMS_FILES_MOD, e.c_str(), buf, sizeof buf - 1);
		if (n > 0) {
			buf[n] = 0;
			const int seedOf = atoi(buf); // the first line: the file's seed
			if (seedOf > 0)
				remove((std::string(core().data_dir(handle())) + "/spoiler-" + std::to_string(seedOf) + ".txt").c_str());
		}
		for (int k = 0; k < kSlotFileCount; ++k)
			api().files->remove(handle(), SMS_FILES_MOD, slotName(slot, slotFile(k)).c_str());
	}

	// A save file copied over another: the mod's files with it.
	void copySlot(int from, int to)
	{
		if (!SMS_API_HAS(&api(), files) || from < 0 || to < 0)
			return;
		for (int k = 0; k < kSlotFileCount; ++k) {
			const char* name = slotFile(k);
			char buf[4096];
			const int n = api().files->read(handle(), SMS_FILES_MOD, slotName(from, name).c_str(), buf, sizeof buf);
			if (n >= 0)
				api().files->write(handle(), SMS_FILES_MOD, slotName(to, name).c_str(), buf, (uint32_t)n);
			else
				api().files->remove(handle(), SMS_FILES_MOD, slotName(to, name).c_str());
		}
	}

	// Every Shine and blue coin the file has picked up (got.txt), whatever the game's own
	// save kept: the tracker's grey.
	void writeGot()
	{
		if (slot_ < 0 || !SMS_API_HAS(&api(), files))
			return;
		std::string out;
		for (size_t i = 0; i < got_.size(); ++i) {
			if (got_[i])
				out += std::to_string(i) + "\n";
		}
		putFile("got.txt", out);
	}

	// What the file picked up that the game's save lost (a blue coin does not save the
	// game): the flags set again, so the coin or the Shine stays gone, and the counters
	// raised to what the flags say. Never lowered: the game's may count more.
	void restoreGot()
	{
		if (!enabled() || slot_ < 0)
			return;
		ownFlag_ = true;
		for (size_t p = 0; p < got_.size(); ++p) {
			if (!got_[p])
				continue;
			if (p < 120) {
				if (!game().flag_get(0x10000u + (uint32_t)p))
					game().flag_set(0x10000u + (uint32_t)p, 1);
				continue;
			}
			const int c = (int)p - gen::kApCoinBase;
			if (!game().flag_get(0x10000u + gen::kApCoins[c].bit))
				game().flag_set(0x10000u + gen::kApCoins[c].bit, 1);
			// a swapped coin gave its Shine
			const int sh = coinShine_[c];
			if (sh >= 0 && !game().flag_get(0x10000u + (uint32_t)sh))
				game().flag_set(0x10000u + (uint32_t)sh, 1);
		}
		int shines = 0, coins = 0, swappedCoins = 0, quiet = 0;
		for (int n = 0; n < 120; ++n)
			shines += game().flag_get(0x10000u + (uint32_t)n) ? 1 : 0;
		for (int c = 0; c < gen::kApCoinCount; ++c) {
			if (game().flag_get(0x10000u + gen::kApCoins[c].bit)) {
				++coins;
				swappedCoins += coinShine_[c] >= 0 ? 1 : 0;
			}
		}
		for (int i = 0; i < 121; ++i)
			quiet += taken_[i] ? 1 : 0;
		// each trade at the boathouse's building spends 10 blue coins
		int trades = 0;
		for (int k = 0; k < 24; ++k)
			trades += game().flag_get(0x10000u + (uint32_t)gen::kTradeShines[k]) ? 1 : 0;
		const int blue = coins - swappedCoins + quiet - 10 * trades;
		if (game().flag_get(0x40000) < shines)
			game().flag_set(0x40000, shines);
		if (game().flag_get(0x40001) < blue)
			game().flag_set(0x40001, blue);
		ownFlag_ = false;
	}

	void readGot()
	{
		std::fill(got_.begin(), got_.end(), 0);
		static char buf[4096];
		const int n = getFile("got.txt", buf, sizeof buf - 1);
		if (n <= 0)
			return;
		buf[n] = 0;
		for (char* t = strtok(buf, " \n"); t; t = strtok(nullptr, " \n")) {
			const int v = atoi(t);
			if (v >= 0 && v < (int)got_.size())
				got_[v] = 1;
		}
	}

	// The levels the file has been in (levels.txt with the save file): the tracker's
	// green is for places whose way in is known.
	void markSeen(int area, int episode)
	{
		const int li = indexOfLevel((uint16_t)(area << 8 | episode));
		if (li < 0 || seenLevels_[li])
			return;
		seenLevels_[li] = 1;
		trackerAt_      = -1;
		writeSeen();
	}

	void writeSeen()
	{
		if (slot_ < 0 || !SMS_API_HAS(&api(), files))
			return;
		std::string out;
		for (int i = 0; i < gen::kLevelCount; ++i) {
			if (seenLevels_[i])
				out += std::to_string(gen::kLevels[i].id) + "\n";
		}
		putFile("levels.txt", out);
	}

	void readSeen()
	{
		std::fill(seenLevels_.begin(), seenLevels_.end(), 0);
		char buf[1024];
		const int n = getFile("levels.txt", buf, sizeof buf - 1);
		if (n <= 0)
			return;
		buf[n] = 0;
		for (char* t = strtok(buf, " \n"); t; t = strtok(nullptr, " \n")) {
			const int li = indexOfLevel((uint16_t)atoi(t));
			if (li >= 0)
				seenLevels_[li] = 1;
		}
	}

	void writeEarned()
	{
		if (slot_ < 0 || !SMS_API_HAS(&api(), files))
			return;
		std::string out = std::to_string(seed()) + "\n";
		for (int i = 0; i < kUnlockCount; ++i) {
			if (earned_[i])
				out += std::to_string(i) + "\n";
		}
		putFile("earned.txt", out);
	}

	void readEarned()
	{
		std::fill(earned_.begin(), earned_.end(), 0);
		char buf[512];
		const int n = getFile("earned.txt", buf, sizeof buf - 1);
		if (n <= 0)
			return;
		buf[n] = 0;
		char* t = strtok(buf, " \n");
		if (!t || atoi(t) != seed())
			return; // another game's
		while ((t = strtok(nullptr, " \n"))) {
			const int v = atoi(t);
			if (v >= 0 && v < kUnlockCount)
				earned_[v] = 1;
		}
	}

	void readTaken()
	{
		readGot();
		readEarned();
		readSeen();
		std::fill(taken_.begin(), taken_.end(), 0);
		char buf[512];
		const int n = getFile("taken.txt", buf, sizeof buf - 1);
		if (n <= 0)
			return;
		buf[n] = 0;
		for (char* t = strtok(buf, " \n"); t; t = strtok(nullptr, " \n")) {
			const int v = atoi(t);
			if (v >= 0 && v < 121)
				taken_[v] = 1;
		}
	}

	void writeTaken()
	{
		if (slot_ < 0 || !SMS_API_HAS(&api(), files))
			return;
		std::string out;
		for (int i = 0; i < 121; ++i) {
			if (taken_[i])
				out += std::to_string(i) + "\n";
		}
		putFile("taken.txt", out);
	}

	void showSwapNote(const char* text)
	{
		swapNote_   = text;
		swapNoteAt_ = core().time();
	}

	// The unlock a place gives, shown as it is had.
	void popupFor(int place)
	{
		for (int i = 0; i < kUnlockCount; ++i) {
			if (shine_[i] == place) {
				popup_   = i;
				popupAt_ = core().time();
				applyOutfit();
			}
		}
	}

	// Works out the seed's game again when the seed, the switch, the start or the
	// shuffle changed.
	void refresh()
	{
		const int s = seed(), e = enabled() ? 1 : 0, all = allUnlocks() ? 1 : 0, sh = shuffle() ? 1 : 0;
		const int co = coinShines() ? 1 : 0, tr = tradeShines() ? 1 : 0;
		const int cu = coinUnlocks() ? 1 : 0, sw = shineSwaps() ? 1 : 0, em = entranceMode();
		if (s == appliedSeed_ && e == appliedEnabled_ && all == appliedAll_ && sh == appliedShuffle_
		    && co == appliedCoins_ && tr == appliedTrades_ && cu == appliedCoinUnlocks_ && sw == appliedSwaps_ && em == appliedMode_)
			return;
		appliedMode_        = em;
		appliedCoinUnlocks_ = cu;
		appliedSwaps_       = sw;
		appliedCoins_   = co;
		appliedTrades_  = tr;
		appliedSeed_    = s;
		appliedEnabled_ = e;
		appliedAll_     = all;
		appliedShuffle_ = sh;
		swaps_.clear();
		plan_ = gen::Plan();
		std::fill(shineCoin_.begin(), shineCoin_.end(), -1);
		std::fill(coinShine_.begin(), coinShine_.end(), -1);
		for (int i = 0; i < kUnlockCount; ++i)
			shine_[i] = -1;
		planFailed_ = false;
		if (!e)
			return;
		const gen::Plan plan = makePlan(all != 0);
		if (!plan.ok) {
			planFailed_ = true;
			warn("seed %d: no game could be worked out", s);
			return;
		}
		for (int k = 0; k < kUnlockCount; ++k) {
			if (plan.start & (1u << kUnlocks[k].bit))
				shine_[k] = kFromStart;
			for (int i = 0; i < 121; ++i) {
				if (plan.unlock[i] == 1u << kUnlocks[k].bit)
					shine_[k] = i;
			}
			for (int i = 0; i < gen::kApCoinCount; ++i) {
				if (plan.coinUnlock[i] == 1u << kUnlocks[k].bit)
					shine_[k] = gen::kApCoinBase + i;
			}
		}
		plan_      = plan;
		trackerAt_ = -1;
		swaps_     = plan.swaps;
		shineCoin_ = plan.shineCoin;
		coinShine_ = plan.coinShine;
#ifdef RANDOMIZER_BETA6
		writeFileGame();
#endif
		if (sh)
			info("seed %d: %u levels placed, %d Shines reachable", s, (unsigned)swaps_.size(), plan.reachable);
		else
			info("seed %d: levels in place, %d Shines reachable", s, plan.reachable);
		for (int i = 0; i < kUnlockCount; ++i)
			debug("  %s: Shine %d", kUnlocks[i].display, shine_[i]);
	}

	bool active()
	{
		refresh();
		return appliedEnabled_ == 1 && !allUnlocks();
	}

	// Is the unlock still locked?
	// An unlock once had stays: the file keeps it itself (earned.txt, with the save file),
	// as the game's flags (a blue coin's, a Shine given by one) are only saved when the
	// game saves.
	bool locked(int unlock)
	{
		if (!active() || shine_[unlock] == kFromStart || earned_[unlock])
			return false;
		if (shine_[unlock] >= 0 && placeHad(shine_[unlock])) {
			earned_[unlock] = 1;
			writeEarned();
			return false;
		}
		return true;
	}

	int unlockedCount()
	{
		int n = 0;
		for (int i = 0; i < kUnlockCount; ++i)
			n += !locked(i);
		return n;
	}

	// ------------------------------------------------------------ Mario's actions

	// What Mario's new status becomes while the unlock that gives it is missing, as the
	// randomizer decides: itself, another status (`to`), or a refusal.
	int statusRule(uint32_t& to)
	{
		if (to == 0x810446 && locked(BLOOPER)) { // surfing
			audio().play_system_sound(0x483D);
			return SMS_CANCEL;
		}
		if ((to == 0x383 || to == 0x384) && locked(GRAB))
			return SMS_CANCEL;
		if (to == 0x8A7 && locked(WALL_KICKS)) { // wall slide
			to = 0x88C;
			return SMS_CONTINUE;
		}
		if ((to == 0x895 || to == 0x896) && locked(SPIN_JUMP)) { // rotate jumps
			to = kJump;
			return SMS_CONTINUE;
		}
		if (to == 0x441 && locked(GROUND_SPIN))
			return SMS_CANCEL;
		if (to == 0x887 && locked(SIDE_FLIP)) { // u-turn jump
			to = kJump;
			return SMS_CONTINUE;
		}
		if (to == 0x2000881 && locked(DOUBLE_JUMP)) { // second jump
			to = locked(TRIPLE_JUMP) ? kJump : 0x882u;
			return SMS_CONTINUE;
		}
		if (to == 0x8008A9 && locked(GROUND_POUND))
			return SMS_CANCEL;
		if (to == 0x882 && locked(TRIPLE_JUMP)) {
			to = kJump;
			return SMS_CONTINUE;
		}
		if ((to == 0x200346 || to == 0x200349 || to == 0x8200348 || to == 0x3000036C || to == 0x3000036B
		     || to == 0x10100341)
		    && locked(CLIMBING))
			return SMS_CANCEL;
		if (to == 0x2000889 && locked(ROLLOUT)) // rotating broad jump
			return SMS_CANCEL;
		if (to == 0x800447 && locked(CART)) // the rollercoaster
			return SMS_CANCEL;
		return SMS_CONTINUE;
	}

	// ----------------------------------------------------------- nozzles and outfit
	// (API 1.5: the game must have SMS_EV_PLAYER_NOZZLE and player->set_nozzle/set_outfit)

	bool hasNozzleApi() const { return SMS_API_HAS(api().player, set_nozzle); }

	// May FLUDD be on this nozzle (SmsPlayer.nozzle's numbers)?
	bool nozzleAllowed(int nozzle)
	{
		switch (nozzle) {
		case 1: return !locked(ROCKET);
		case 2:
		case 4: return !locked(HOVER);
		case 3: return !locked(YOSHI);
		case 5: return !locked(TURBO);
		default: return true;
		}
	}

	bool otherNozzle() { return !locked(HOVER) || !locked(ROCKET) || !locked(TURBO); }

	// What Mario has at the start of a stage: the unlocked nozzles (the second one the
	// hover, else the rocket, else the turbo; the plain spray in hand when unlocked),
	// no FLUDD with none, and the shirt, sunglasses and helmet once unlocked.
	void applyStart()
	{
		if (!active() || !hasNozzleApi())
			return;
		const SmsPlayer m = mario();
		if (!m.valid)
			return;
		if (m.has_fludd) {
			const int second = !locked(HOVER) ? 4 : !locked(ROCKET) ? 1 : !locked(TURBO) ? 5 : -1;
			if (second >= 0) {
				player().set_nozzle(second, 0);
				if (!locked(SPRAY))
					player().set_nozzle(0, 0);
			} else if (locked(SPRAY)) {
				player().set_fludd(0);
			}
		}
		applyOutfit();
	}

	// No FLUDD at all without the spray or another nozzle: done as Mario is put in the
	// stage, before its HUD is set up (later, the water gauge would stay on screen).
	void applyNoFludd()
	{
		if (active() && SMS_API_HAS(api().player, set_fludd) && locked(SPRAY) && !otherNozzle())
			player().set_fludd(0);
	}

	void applyOutfit()
	{
		if (active() && SMS_API_HAS(api().player, set_outfit))
			player().set_outfit(!locked(SHINE_SHIRT), !locked(SUNGLASSES), !locked(HELMET));
	}

	// ------------------------------------------------------------------- screen

	static int popupAlpha(double frame)
	{
		float f0 = (float)(frame * frame);
		f0       = f0 * 2.0678031e-05f + -0.1f;
		if (!(f0 >= 0.0f))
			f0 = 0.0f;
		const float f1 = 1.0f - f0;
		const float f2 = (float)(f1 * 1.1111111111111112 + 1.0);
		if (!(f1 >= 0.0f))
			f0 = f2;
		int a = (int)(f0 * 255.0f);
		return a < 0 ? 0 : a > 255 ? 255 : a;
	}

	void drawPopup(const SmsEvDraw& d)
	{
		if (swapNoteAt_ >= 0 && core().time() - swapNoteAt_ < 2.5)
			draw().text(d.canvas_w / 2, 145, 20, SMS_RGB(120, 200, 255), SMS_TEXT_CENTER | SMS_TEXT_SHADOW, swapNote_.c_str());
		if (popup_ < 0)
			return;
		const double frames = (core().time() - popupAt_) * 30.0;
		if (frames > 300.0) {
			popup_ = -1;
			return;
		}
		const int alpha = popupAlpha(frames);
		if (alpha <= 0)
			return;
		char text[64];
		snprintf(text, sizeof text, "UNLOCKED %s", kUnlocks[popup_].display);
		draw().text(d.canvas_w / 2, 110, 24, SMS_RGBA(255, 255, 255, alpha), SMS_TEXT_CENTER | SMS_TEXT_SHADOW, text);
	}

	// The shuffle's first level (where a new file starts), or 0 without the shuffle. A
	// level left before its unlocks were had may have no way back in: the warp
	// (the plaza's ring, the randomizer's page) takes Mario there again.
	uint16_t startLevel() const
	{
		for (size_t i = 0; i < swaps_.size(); ++i) {
			if (swaps_[i].first == 0)
				return swaps_[i].second;
		}
		return 0;
	}

	bool canWarpToStart()
	{
		return active() && shuffle() && startLevel() != 0 && state().area == 1;
	}

	void warpToStart()
	{
		const uint16_t to = startLevel();
		startPlaced_      = to; // loaded as it is, not swapped again
		if (!game().warp(to >> 8, to & 0xFF))
			startPlaced_ = -1;
	}

	bool inWarpRing()
	{
		const SmsPlayer m = mario();
		if (!m.valid)
			return false;
		const float dx = m.position.x - kWarpPos.x, dz = m.position.z - kWarpPos.z;
		return dx * dx + dz * dz < kWarpRadius * kWarpRadius && fabsf(m.position.y - kWarpPos.y) < 200;
	}

	void drawWarpRing()
	{
		if (!canWarpToStart() || state().paused)
			return;
		const bool in = inWarpRing();
		draw().world_ring(&kWarpPos, kWarpRadius, 4, in ? SMS_RGB(255, 230, 90) : SMS_RGBA(120, 220, 255, 200));
		if (!in)
			return; // the text only once Mario is in the ring
		SmsVec3 top = kWarpPos;
		top.y += 160;
		draw().world_text(&top, 18, 1, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, "Z: BACK TO THE START LEVEL");
	}

	// No water gauge while Mario has no FLUDD (taken off when no nozzle is unlocked:
	// a stage that set its HUD up before keeps the gauge on screen otherwise).
	void applyHud()
	{
		if (!SMS_API_HAS(api().game, set_hud_hidden))
			return;
		const SmsPlayer m   = mario();
		const uint32_t want = active() && m.valid && !m.has_fludd ? SMS_HUD_WATER : 0;
		if (want != hudHidden_) {
			game().set_hud_hidden(handle(), want);
			hudHidden_ = want;
		}
	}

	// The D-pad's nozzles (PAD_READ), bottom left while Mario has FLUDD and a second
	// nozzle is unlocked: up the hover, left the rocket, right the turbo, each only once
	// unlocked; the one on FLUDD outlined.
	void drawDpad()
	{
		if (!enabled() || !hasNozzleApi() || !otherNozzle() || state().paused)
			return;
		const SmsPlayer m = mario();
		if (!m.valid || !m.has_fludd)
			return;
		SmsRect r;
		if (!draw().hud_slot(handle(), SMS_ANCHOR_BOTTOM_LEFT, 150, 130, &r))
			return;
		// the pad: a black cross, its arrows white, a round middle
		const float w = 22, l = 34, cx = r.x + r.w / 2, cy = r.y + r.h / 2 + 6; // an arm's width, a half cross
		const SmsColor pad = SMS_RGBA(15, 15, 15, 225), mark = SMS_RGBA(255, 255, 255, 235);
		draw().rect(cx - w / 2, cy - l, w, 2 * l, pad);
		draw().rect(cx - l, cy - w / 2, 2 * l, w, pad);
		draw().circle(cx, cy, 5, mark);
		struct Arm {
			float dx, dy; // the arrow's way out
			int nozzle, unlock;
			const char* icon;
		};
		const Arm arms[] = { { 0, -1, 4, HOVER, "icons/hover.png" }, { -1, 0, 1, ROCKET, "icons/rocket.png" },
			                 { 1, 0, 5, TURBO, "icons/turbo.png" }, { 0, 1, -1, -1, nullptr } };
		for (const Arm& k : arms) {
			const bool open = k.unlock >= 0 && !locked(k.unlock);
			// the arrow, yellow for the nozzle on FLUDD
			const float tx = cx + k.dx * (l - 8), ty = cy + k.dy * (l - 8), s = 6;
			const SmsColor c = open && m.nozzle == k.nozzle ? SMS_RGB(255, 220, 60) : open ? mark : SMS_RGBA(255, 255, 255, 90);
			draw().triangle(tx + k.dx * s, ty + k.dy * s, tx - k.dx * s + k.dy * s, ty - k.dy * s + k.dx * s,
			                tx - k.dx * s - k.dy * s, ty - k.dy * s - k.dx * s, c);
			if (!open)
				continue;
			// the game's own icon of the nozzle, past the arm's end
			SmsImage& img = dpadIcons_[k.nozzle == 4 ? 0 : k.nozzle == 1 ? 1 : 2];
			if (img == 0)
				img = draw().image_load(handle(), k.icon);
			int iw = 0, ih = 0;
			if (!img || !draw().image_size(img, &iw, &ih) || iw <= 0 || ih <= 0)
				continue;
			const float size = 30, sc = size / (iw > ih ? iw : ih), dw = iw * sc, dh = ih * sc;
			const float ix = cx + k.dx * (l + 4 + dw / 2), iy = cy + k.dy * (l + 4 + dh / 2);
			draw().image(img, ix - dw / 2, iy - dh / 2, dw, dh, SMS_RGB(255, 255, 255));
		}
	}

	// The randomizer's page, as its pause menu shows it: text down the left side, the
	// version and seed dimmed, the rules, then the unlocks you have. It is shown while the
	// game is paused, and with F4.
	void drawList(const SmsEvDraw& d)
	{
		const bool paused = state().paused != 0;
		if (paused && enabled()) {
			draw().text(d.canvas_w - 16, 404, 12, SMS_RGB(255, 230, 90), SMS_TEXT_RIGHT | SMS_TEXT_SHADOW, "HOLD X, THEN Y: TRACKER");
			const bool done = spoilerShownAt_ >= 0 && core().time() - spoilerShownAt_ < 3.0;
			draw().text(d.canvas_w - 16, 388, 12, SMS_RGB(255, 230, 90), SMS_TEXT_RIGHT | SMS_TEXT_SHADOW,
			            done ? "SPOILER LOG WRITTEN" : "X: WRITE THE SPOILER LOG");
		}
		if (!listShown_ && !paused)
			return;
		const int win = kWins[setting("win", 0) < 0 || setting("win", 0) > 5 ? 0 : setting("win", 0)];
		// the page's lines; an empty one is a gap
		char head[5][48];
		snprintf(head[0], sizeof head[0], "VERSION: v%s", core().mod_info(handle(), "version"));
		snprintf(head[1], sizeof head[1], "SEED: %08X", (unsigned)appliedSeed_);
		snprintf(head[2], sizeof head[2], "ALL UNLOCKS START: %s", allUnlocks() ? "TRUE" : "FALSE");
		if (win > 0)
			snprintf(head[3], sizeof head[3], "WIN CONDITION: %d SHINES", win);
		else
			snprintf(head[3], sizeof head[3], "WIN CONDITION: ALL SM");
		int open = 0;
		for (int i = 0; i < kUnlockCount; ++i)
			open += !locked(i);
		snprintf(head[4], sizeof head[4], "UNLOCKED (%d / %d)", open, kUnlockCount);
		const SmsColor dim = SMS_RGBA(255, 255, 255, 120), bright = SMS_RGB(255, 255, 255);
		struct Line {
			const char* text;
			SmsColor color;
		};
		Line lines[7 + kUnlockCount];
		int count        = 0;
		lines[count++]   = { head[0], dim };
		lines[count++]   = { head[1], dim };
		lines[count++]   = { "", bright };
		lines[count++]   = { head[2], bright };
		lines[count++]   = { head[3], bright };
		lines[count++]   = { "", bright };
		lines[count++]   = { head[4], bright };
		for (int i = 0; i < kUnlockCount; ++i) {
			if (!locked(i))
				lines[count++] = { kUnlocks[i].display, bright };
		}
		// As large as it fits, every unlock shown, the unlocks in two columns if need be.
		// Paused, it goes down the right side, between the port's save state hints and
		// FLUDD's water gauge, clear of the pause menu; otherwise (F4) at the top left.
		const int headCount          = 7;
		const float kPausedTop = 130, kPausedBottom = 360;
		static const float kSizes[] = { 14, 12, 11, 10.5f, 10, 9 };
		for (size_t k = 0; k < sizeof kSizes / sizeof kSizes[0]; ++k) {
			for (int cols = 1; cols <= 2; ++cols) {
				const float size = kSizes[k], step = size * 1.25f, gap = size;
				float headW = 0, colW = 0;
				for (int i = 0; i < count; ++i) {
					const float w = draw().text_width(size, lines[i].text);
					float& m      = i < headCount ? headW : colW;
					m             = w > m ? w : m;
				}
				// the head's two gaps are half a line
				const int rows     = (count - headCount + cols - 1) / cols;
				const float width  = (headW > colW * cols + gap * (cols - 1) ? headW : colW * cols + gap * (cols - 1)) + 16;
				const float height = step * (headCount - 1 + rows) + 4;
				SmsRect r;
				if (paused) {
					if (height > kPausedBottom - kPausedTop)
						continue;
					r = SmsRect{ d.canvas_w - 16 - width, kPausedTop, width, height };
				} else if (!draw().hud_slot(handle(), SMS_ANCHOR_TOP_LEFT, width, height, &r)) {
					continue;
				}
				float y = r.y + 2;
				for (int i = 0; i < headCount; ++i) {
					if (lines[i].text[0]) {
						draw().text(r.x + 8, y, size, lines[i].color, SMS_TEXT_SHADOW, lines[i].text);
						y += step;
					} else {
						y += step * 0.5f;
					}
				}
				for (int i = headCount; i < count; ++i) {
					const int n = i - headCount;
					draw().text(r.x + 8 + (colW + gap) * (n / rows), y + step * (n % rows), size, lines[i].color,
					            SMS_TEXT_SHADOW, lines[i].text);
				}
				return;
			}
		}
	}

	// ------------------------------------------------------------------ tracker

	static constexpr int kTrackerPages = 9;
	static const char* trackerWorld(int page)
	{
		static const char* const kWorlds[kTrackerPages] = { "Delfino Plaza", "Bianco Hills", "Ricco Harbor",
			                                                "Gelato Beach",  "Pinna Park",   "Sirena Beach",
			                                                "Noki Bay",      "Pianta Village", "Corona Mountain" };
		return kWorlds[page];
	}

	// The tracker's page a Shine is on: a world's episodes (ten Shines each, Bianco Hills
	// first) and its 100-coin Shine on the world's; Corona Mountain's last; the rest (the
	// plaza's, the airstrip's, the trades) on the plaza's.
	static int trackerShinePage(int shine)
	{
		if (shine < 70)
			return 1 + shine / 10;
		if (shine >= 100 && shine <= 106)
			return 1 + (shine - 100);
		return 0;
	}

	static int trackerCoinPage(int c)
	{
		for (int p = 1; p < kTrackerPages; ++p) {
			if (strcmp(gen::kApCoins[c].world, trackerWorld(p)) == 0)
				return p;
		}
		return 0; // the plaza's and the airstrip's
	}

	// Has the file been to the place: a Shine taken (a swapped one quietly), a blue coin
	// picked up?
	bool trackerDone(int place)
	{
		if (got_[place])
			return true;
		if (place >= gen::kApCoinBase)
			return game().flag_get(0x10000u + gen::kApCoins[place - gen::kApCoinBase].bit) != 0;
		if (shineCoin_[place] >= 0)
			return taken_[place] != 0;
		return game().has_shine(place);
	}

	// The levels a place is in (a swapped one stays where it is: the tracker marks the
	// place, not what it gives).
	std::vector<uint16_t> placeOwnLevels(int place) const
	{
		std::vector<uint16_t> out;
		if (place >= gen::kApCoinBase) {
			const gen::ApCoin& c = gen::kApCoins[place - gen::kApCoinBase];
			return std::vector<uint16_t>(c.levels, c.levels + c.levelCount);
		}
		out = levelsOfShine(place);
		for (int i = 0; out.empty() && i < gen::kLevelCount; ++i) {
			if (gen::coinShineOf(gen::kLevels[i].id) == place)
				out.push_back(gen::kLevels[i].id);
		}
		return out;
	}

	// Does the file know the way to a place? Without the shuffle it does; with it, once it
	// has been in a level the place is in (the plaza's places always). A place in no level
	// (the boathouse's trades) is the plaza's.
	bool placeKnown(int place) const
	{
		if (!shuffle())
			return true;
		const std::vector<uint16_t> lv = placeOwnLevels(place);
		if (lv.empty())
			return true;
		for (size_t i = 0; i < lv.size(); ++i) {
			const int li = indexOfLevel(lv[i]);
			if ((lv[i] >> 8) == 1 || (li >= 0 && seenLevels_[li]))
				return true;
		}
		return false;
	}

	// What the logic reaches with the unlocks had, the Shines had and the Shadow Marios
	// beaten: every place, whether it may hold an unlock with these options or not.
	void trackerLogic()
	{
		const int places = gen::kApCoinBase + gen::kApCoinCount;
		trackerReach_.assign(places, 0);
		std::fill(entranceOpen_, entranceOpen_ + 16, false);
		std::fill(entranceNew_, entranceNew_ + 16, 0);
		std::fill(entranceTotal_, entranceTotal_ + 16, 0);
		trackerAt_ = core().time();
		if (!plan_.ok)
			return;
		gen::Plan p   = plan_;
		p.coinShines  = true;
		p.plazaExtra  = true;
		p.tradeShines = true;
		p.apLogic     = true;
		uint32_t skills = p.start;
		for (int k = 0; k < kUnlockCount; ++k) {
			if (!locked(k))
				skills |= 1u << kUnlocks[k].bit;
		}
		const int had = game().flag_get(0x40000);
		int shadows   = 0;
		for (int i = 0; i < 121; ++i)
			shadows += gen::shadowShine(i) && game().has_shine(i) ? 1 : 0;
		uint32_t r = 0;
		const std::vector<int> got = gen::reachable(p, shuffle(), skills, had, false, r, had, shadows);
		for (size_t i = 0; i < got.size(); ++i) {
			// what a place gives back to the place: a swapped one gives its partner
			int place = got[i];
			if (place >= gen::kApCoinBase) {
				const int sh = coinShine_[place - gen::kApCoinBase];
				place        = sh >= 0 ? sh : place;
			} else if (place < 121 && shineCoin_[place] >= 0) {
				place = gen::kApCoinBase + shineCoin_[place];
			}
			if (place >= 0 && place < places)
				trackerReach_[place] = 1;
		}
		// The plaza's entrances: its way to the world open (what the way needs, the Shines it
		// asks), and the levels behind it that the file has not been in (the shuffle)
		if (shuffle()) {
			const int pl = indexOfLevel(0x108);
			for (int k = 0; pl >= 0 && k < gen::kLevels[pl].exitCount; ++k) {
				const gen::LevelExit& x = gen::kExits[gen::kLevels[pl].exitFirst + k];
				const int a             = x.to >> 8;
				if (a < 16 && ((x.need & ~r) == 0) && (int)x.shines <= had)
					entranceOpen_[a] = true;
			}
			entranceOpen_[7] = entranceOpen_[6]; // the hotel is entered through Sirena's way
			for (int i = 0; i < gen::kLevelCount; ++i) {
				const int a = gen::kLevels[i].id >> 8;
				if (a >= 16 || !entranceOpen_[a])
					continue;
				if ((gen::kLevels[i].id & 0xFF) != 0)
					continue; // only the world's first level: the entrance's own
				const uint16_t to = swapOf(gen::kLevels[i].id);
				const int li      = indexOfLevel(to);
				if ((to >> 8) == 1 || li < 0)
					continue;
				++entranceTotal_[a];
				entranceNew_[a] += seenLevels_[li] ? 0 : 1;
			}
		}
		// With the shuffle, only the levels the file has been in are known: the logic's
		// green stays where the way in was found
		for (int i = 0; i < places; ++i) {
			if (trackerReach_[i] && !placeKnown(i))
				trackerReach_[i] = 0;
		}
		// The places the walk never puts in a level, by the apworld's needs: a world's
		// 100-coin Shine in the scenes it does not count and Noki Bay 8's red coins, once
		// the world is open; the blue coin trades (the boathouse, on the plaza's harbor once
		// the way to Ricco Harbor is open: its 3 Shines and its goop Piranha Plant, with what
		// that way needs, kExits) by the blue coins had or reachable.
		auto gotPlace = [&](int l) { return trackerReach_[l] != 0 || trackerDone(l); };
		bool open[kTrackerPages] = {};
		int coins                = 0;
		for (int i = 0; i < 120; ++i)
			open[trackerShinePage(i)] = open[trackerShinePage(i)] || gotPlace(i);
		for (int c = 0; c < gen::kApCoinCount; ++c) {
			const bool g = gotPlace(gen::kApCoinBase + c);
			open[trackerCoinPage(c)] = open[trackerCoinPage(c)] || g;
			coins += g ? 1 : 0;
		}
		static const int kUnplaced[] = { 57, 100, 101, 102, 103, 104, 105, 106 };
		for (size_t i = 0; i < sizeof kUnplaced / sizeof kUnplaced[0]; ++i) {
			const int s = kUnplaced[i];
			if (!trackerReach_[s] && open[trackerShinePage(s)] && placeKnown(s)
			    && gen::apMet(gen::kApShines[s], skills, had, (r & 0x10000) != 0, gotPlace))
				trackerReach_[s] = 1;
		}
		for (int k = 0; k < 24; ++k) {
			if (had >= 3 && (0x104 & ~r) == 0 && coins >= 10 * (k + 1))
				trackerReach_[gen::kTradeShines[k]] = 1;
		}
	}

	// A text cut to fit `w`, ending in "..." when cut.
	static constexpr int kTrackerMapCount = (int)(sizeof kTrackerMaps / sizeof kTrackerMaps[0]);

	// A marker's colour: green while one of its places is in logic and not had, grey once
	// all are had, red otherwise.
	// Green: in logic and its level entered; red: not (yet); grey: had. Orange is a plaza
	// entrance open in logic with levels behind it not entered yet.
	enum MarkState { MARK_IN, MARK_OUT, MARK_DONE, MARK_UNKNOWN };
	MarkState placeState(int p)
	{
		return trackerDone(p) ? MARK_DONE : trackerReach_[p] ? MARK_IN : MARK_OUT;
	}
	MarkState markState(const TrackerMark& m)
	{
		bool all = true, in = false, unknown = false;
		for (int k = 0; k < m.count; ++k) {
			const MarkState s = placeState(kTrackerPlaces[m.first + k]);
			all               = all && s == MARK_DONE;
			in                = in || s == MARK_IN;
			unknown           = unknown || s == MARK_UNKNOWN;
		}
		return all ? MARK_DONE : in ? MARK_IN : unknown ? MARK_UNKNOWN : MARK_OUT;
	}

	static SmsColor markColor(MarkState s)
	{
		return s == MARK_DONE ? SMS_RGB(120, 120, 120)
		       : s == MARK_IN ? SMS_RGB(60, 230, 60)
		       : s == MARK_UNKNOWN ? SMS_RGB(255, 165, 40)
		                           : SMS_RGB(230, 50, 40);
	}

	// Where a place is, as far as the file knows: the levels it is in that the file has been
	// in (the plaza's places say nothing), each with the entrance of the plaza that the
	// shuffle put there. A few at most, then how many more.
	std::vector<std::string> placeWhere(int place) const
	{
		std::vector<std::string> out;
		const std::vector<uint16_t> lv = placeOwnLevels(place);
		int more                      = 0;
		for (size_t i = 0; i < lv.size(); ++i) {
			const int li = indexOfLevel(lv[i]);
			if ((lv[i] >> 8) == 1 || li < 0 || !seenLevels_[li])
				continue;
			if (out.size() >= 3) {
				++more;
				continue;
			}
			std::string line = "in " + levelName(lv[i]);
			if (shuffle()) {
				for (size_t j = 0; j < swaps_.size(); ++j) {
					if (swaps_[j].second == lv[i]) {
						line += swaps_[j].first == 0 ? " (where a new file starts)"
						                             : " (by the entrance to " + levelName((uint16_t)(swaps_[j].first & 0xFF00)) + ")"; // the entrance is the area's first one;
						break;
					}
				}
			}
			out.push_back(line);
		}
		if (more > 0)
			out.push_back("and " + std::to_string(more) + " more");
		return out;
	}

	// A place's name on the tracker: a Shine's as the spoiler gives it, with its number (the
	// excluded locations and exclusions.txt use it too), a blue coin's with its number.
	std::string trackerName(int place) const
	{
		if (place >= gen::kApCoinBase)
			return std::string("Blue coin ") + std::to_string(place - gen::kApCoinBase) + ": " + gen::kApCoins[place - gen::kApCoinBase].name;
		return "Shine " + std::to_string(place) + ": " + shineName(place);
	}

	// A world's episode select: what the cursor's episode holds, if the file has been in the
	// level it loads (the shuffle shows nothing of one it has not): the checks in logic there.
	void drawSelect(const SmsEvDraw& d)
	{
		const auto& st = state();
		if (!shuffle() || st.select_area < 0 || st.select_episode < 0)
			return; // without the shuffle, it would say nothing new
		const uint16_t id = (uint16_t)(st.select_area << 8 | st.select_episode);
		const uint16_t to = shuffle() ? swapOf(id) : id;
		const int li      = indexOfLevel(to);
		if (li < 0 || !seenLevels_[li])
			return;
		if (trackerAt_ < 0 || core().time() - trackerAt_ > 0.5)
			trackerLogic();
		int in = 0;
		for (size_t p = 0; p < trackerReach_.size(); ++p) {
			if (!trackerReach_[p] || trackerDone((int)p))
				continue;
			const std::vector<uint16_t> lv = placeOwnLevels((int)p);
			in += std::find(lv.begin(), lv.end(), to) != lv.end();
		}
		// what it leads to, and where that level's own ways out lead, each only once the file
		// has been in it (the shuffle's answers come one by one)
		std::vector<std::string> lines;
		lines.push_back("leads to " + levelName(to));
		for (int k = 0; k < gen::kLevels[li].exitCount && lines.size() < 5; ++k) {
			const uint16_t out = swapOf(gen::kExits[gen::kLevels[li].exitFirst + k].to);
			const int oi       = indexOfLevel(out);
			if ((out >> 8) == 1 || oi < 0 || !seenLevels_[oi] || out == to)
				continue;
			const std::string l = "then " + levelName(out);
			if (std::find(lines.begin(), lines.end(), l) == lines.end())
				lines.push_back(l);
		}
		char t[64];
		snprintf(t, sizeof t, "%d checks in logic here", in);
		lines.push_back(t);
		float y = d.canvas_h - 40.0f - 24.0f * (float)lines.size();
		for (size_t i = 0; i < lines.size(); ++i, y += 24)
			draw().text(d.canvas_w / 2, y, 18, i + 1 == lines.size() ? (in ? SMS_RGB(60, 230, 60) : SMS_RGB(200, 200, 200)) : SMS_RGB(255, 255, 255),
			            SMS_TEXT_CENTER | SMS_TEXT_SHADOW, lines[i].c_str());
	}

	// The tracker: a world's map (the Sunshine_Pop PopTracker pack's, by VGCartographer,
	// in tracker/), its places as squares where the pack puts them, green, red or grey; a
	// cursor (the stick) names the places of the square it is on.
	void drawTracker(const SmsEvDraw& d)
	{
		if (trackerAt_ < 0 || core().time() - trackerAt_ > 0.5)
			trackerLogic();
		const TrackerMap& map = kTrackerMaps[trackerPage_];
		if (trackerImagePage_ != trackerPage_) {
			if (trackerImage_)
				draw().image_free(handle(), trackerImage_);
			trackerImage_     = draw().image_load(handle(), map.image);
			trackerImagePage_ = trackerPage_;
		}
		const SmsColor white = SMS_RGB(255, 255, 255), gold = SMS_RGB(255, 220, 90);
		draw().rect(0, 0, d.canvas_w, d.canvas_h, SMS_RGBA(0, 0, 0, 230));
		// the counts of the map's places
		int in = 0, out = 0, done = 0;
		std::vector<char> seen(gen::kApCoinBase + gen::kApCoinCount, 0);
		for (const TrackerMark& m : kTrackerMarks) {
			if (m.map != trackerPage_)
				continue;
			for (int k = 0; k < m.count; ++k) {
				const int p = kTrackerPlaces[m.first + k];
				if (seen[p])
					continue;
				seen[p]           = 1;
				const MarkState s = placeState(p);
				(s == MARK_DONE ? done : s == MARK_IN ? in : out) += 1;
			}
		}
		char title[96];
		snprintf(title, sizeof title, "<  %s  >", map.name);
		draw().text(d.canvas_w / 2, 6, 20, gold, SMS_TEXT_CENTER | SMS_TEXT_SHADOW, title);
		char counts[96];
		snprintf(counts, sizeof counts, "IN LOGIC %d    OUT OF LOGIC %d    DONE %d", in, out, done);
		draw().text(d.canvas_w / 2, 30, 11, white, SMS_TEXT_CENTER | SMS_TEXT_SHADOW, counts);
		char help[200];
		snprintf(help, sizeof help,
		         "L / R: MAP   STICK: LOOK   C-STICK: ZOOM   A: HIDE DONE %s   X: HIDE OUT OF LOGIC %s   B: CLOSE",
		         trackerHideDone_ ? "(ON)" : "(OFF)", trackerHideOut_ ? "(ON)" : "(OFF)");
		draw().text(d.canvas_w / 2, d.canvas_h - 14, 10, SMS_RGBA(255, 255, 255, 170), SMS_TEXT_CENTER, help);
		draw().text(d.canvas_w - 8, d.canvas_h - 14, 8, SMS_RGBA(255, 255, 255, 110), SMS_TEXT_RIGHT,
		            "MAPS: VGCARTOGRAPHER, SUNSHINE_POP");
		int iw = 0, ih = 0;
		if (!trackerImage_ || !draw().image_size(trackerImage_, &iw, &ih) || iw <= 0 || ih <= 0) {
			draw().text(d.canvas_w / 2, d.canvas_h / 2 - 10, 12, SMS_RGB(240, 90, 80), SMS_TEXT_CENTER | SMS_TEXT_SHADOW,
			            "NO MAP: PUT THE SUNSHINE_POP PACK'S images FOLDER IN THE MOD'S tracker FOLDER");
			return;
		}
		// the map as large as fits between the title and the help line
		const float top = 46, bottom = d.canvas_h - 18;
		const float sc = fminf(d.canvas_w / iw, (bottom - top) / ih);
		const float mw = iw * sc, mh = ih * sc, mx = (d.canvas_w - mw) / 2, my = top + (bottom - top - mh) / 2;
		// zoomed: the part of the map around the cursor fills the same place
		const float z = trackerZoom_, s = sc * z;
		const float vw = iw / z, vh = ih / z;
		const float vx0 = fmaxf(0.0f, fminf(trackerCursorX_ - vw / 2, iw - vw));
		const float vy0 = fmaxf(0.0f, fminf(trackerCursorY_ - vh / 2, ih - vh));
		draw().image_region(trackerImage_, mx, my, mw, mh, vx0 / iw, vy0 / ih, (vx0 + vw) / iw, (vy0 + vh) / ih,
		                    SMS_RGB(255, 255, 255));
		// the squares, the one under the cursor outlined
		const float size = fmaxf(5.0f, 24 * sc * fminf(z, 2.5f)), half = size / 2;
		const TrackerMark* hit = nullptr;
		float best             = (40 / s) * (40 / s);
		draw().clip_push(mx, my, mw, mh);
		for (const TrackerMark& m : kTrackerMarks) {
			if (m.map != trackerPage_)
				continue;
			const MarkState ms = markState(m);
			if ((ms == MARK_DONE && trackerHideDone_) || (ms == MARK_OUT && trackerHideOut_))
				continue;
			const float x = mx + (m.x - vx0) * s, y = my + (m.y - vy0) * s;
			if (x < mx - size || x > mx + mw + size || y < my - size || y > my + mh + size)
				continue;
			draw().rect(x - half - 1, y - half - 1, size + 2, size + 2, SMS_RGB(0, 0, 0));
			draw().rect(x - half, y - half, size, size, markColor(ms));
			const float dx = m.x - trackerCursorX_, dy = m.y - trackerCursorY_, dd = dx * dx + dy * dy;
			if (dd < best) {
				best = dd;
				hit  = &m;
			}
		}
		// the plaza's entrances open with levels not entered yet: orange
		int hitEntrance = -1;
		if (trackerPage_ == 1 && shuffle()) {
			float bestE = (40 / s) * (40 / s);
			for (size_t k = 0; k < sizeof kTrackerEntrances / sizeof kTrackerEntrances[0]; ++k) {
				const TrackerEntrance& en = kTrackerEntrances[k];
				if (!entranceOpen_[en.area] || entranceNew_[en.area] == 0)
					continue;
				const float x = mx + (en.x - vx0) * s, y = my + (en.y - vy0) * s;
				const float eh = half * 1.4f;
				draw().rect(x - eh - 2, y - eh - 2, 2 * eh + 4, 2 * eh + 4, SMS_RGB(0, 0, 0));
				draw().rect(x - eh, y - eh, 2 * eh, 2 * eh, SMS_RGB(255, 165, 40));
				const float dx = en.x - trackerCursorX_, dy = en.y - trackerCursorY_, dd = dx * dx + dy * dy;
				if (dd < bestE) {
					bestE       = dd;
					hitEntrance = (int)k;
				}
			}
			if (hitEntrance >= 0)
				hit = nullptr;
		}
		draw().clip_pop();
		const float cx = mx + (trackerCursorX_ - vx0) * s, cy = my + (trackerCursorY_ - vy0) * s;
		draw().circle_outline(cx, cy, 6, 2, SMS_RGB(0, 0, 0));
		draw().circle_outline(cx, cy, 5, 1.5f, white);
		if (hitEntrance >= 0) {
			const TrackerEntrance& en = kTrackerEntrances[hitEntrance];
			const float ex = mx + (en.x - vx0) * s, ey = my + (en.y - vy0) * s;
			const char* an = SMS_API_HAS(api().game, area_name) ? game().area_name(en.area) : nullptr;
			std::string name = an && *an ? titleCase(an) : "Area " + std::to_string(en.area);
			char l2[96];
			snprintf(l2, sizeof l2, "open: %d of %d levels behind it not entered yet", entranceNew_[en.area],
			         entranceTotal_[en.area]);
			const float tw = fmaxf(draw().text_width(11, name.c_str()), draw().text_width(9.5f, l2)) + 16;
			const float th = 11 * 1.3f + 9.5f * 1.3f + 8;
			float bx = ex + half + 10, by = ey - th / 2;
			if (bx + tw > d.canvas_w - 4)
				bx = ex - half - 10 - tw;
			bx = fmaxf(4.0f, bx);
			by = fmaxf(top, fminf(by, bottom - th));
			draw().rect(bx, by, tw, th, SMS_RGBA(0, 0, 0, 220));
			draw().rect_outline(bx, by, tw, th, 1, SMS_RGBA(255, 255, 255, 120));
			draw().text(bx + 8, by + 4, 11, SMS_RGB(255, 165, 40), SMS_TEXT_SHADOW, name.c_str());
			draw().text(bx + 8, by + 4 + 11 * 1.3f, 9.5f, SMS_RGBA(255, 255, 255, 190), SMS_TEXT_SHADOW, l2);
			return;
		}
		if (!hit)
			return;
		const float hx = mx + (hit->x - vx0) * s, hy = my + (hit->y - vy0) * s;
		draw().rect_outline(hx - half - 3, hy - half - 3, size + 6, size + 6, 2, white);
		// the square's places, in a box by it: each with the levels it is in that the file has
		// been in, and the entrance of the plaza that leads there
		struct Row {
			std::string text;
			SmsColor color;
			float size;
		};
		std::vector<Row> rows;
		for (int k = 0; k < hit->count; ++k) {
			const int p = kTrackerPlaces[hit->first + k];
			rows.push_back(Row{ trackerName(p), markColor(placeState(p)), 11 });
			for (const std::string& line : placeWhere(p))
				rows.push_back(Row{ line, SMS_RGBA(255, 255, 255, 190), 9.5f });
		}
		float w = 0, h = 8;
		for (size_t i = 0; i < rows.size(); ++i) {
			w = fmaxf(w, draw().text_width(rows[i].size, rows[i].text.c_str()));
			h += rows[i].size * 1.3f;
		}
		w += 16;
		float bx = hx + half + 8, by = hy - h / 2;
		if (bx + w > d.canvas_w - 4)
			bx = hx - half - 8 - w;
		bx = fmaxf(4.0f, bx);
		by = fmaxf(top, fminf(by, bottom - h));
		draw().rect(bx, by, w, h, SMS_RGBA(0, 0, 0, 220));
		draw().rect_outline(bx, by, w, h, 1, SMS_RGBA(255, 255, 255, 120));
		float y = by + 4;
		for (size_t i = 0; i < rows.size(); ++i) {
			draw().text(bx + 8, y, rows[i].size, rows[i].color, SMS_TEXT_SHADOW, rows[i].text.c_str());
			y += rows[i].size * 1.3f;
		}
	}

	// The tracker's controls, while the game is paused: Y opens and closes it, B closes
	// it, L and R (or the D-pad) turn its maps, the stick moves its cursor. Open, it keeps
	// the controller from the pause menu. Returns whether it took the controller.
	bool trackerInput(SmsPadStatus& pad)
	{
		const uint16_t b = pad.buttons, pressed = b & (uint16_t)~lastTrackerButtons_;
		lastTrackerButtons_ = b;
		if (!trackerOpen_ && trackerSwallow_) {
			trackerSwallow_ &= b; // until let go
			pad.buttons &= (uint16_t)~trackerSwallow_;
		}
		if (!state().paused || !enabled()) {
			trackerOpen_ = false;
			return false;
		}
		bool centre = false;
		if (!trackerOpen_) {
			if (!(b & SMS_PAD_X) || !(pressed & SMS_PAD_Y))
				return false;
			trackerOpen_ = true;
			trackerAt_   = -1;
			centre       = true;
			markSeen(state().area, state().episode); // the level Mario is in is known (a file from before this)
			// the map of the world Mario is in first
			trackerPage_   = 1;
			const char* an = SMS_API_HAS(api().game, area_name) ? game().area_name(state().area) : nullptr;
			for (int p = 0; p < kTrackerMapCount && an; ++p) {
				if (strncasecmp(an, kTrackerMaps[p].name, strlen(kTrackerMaps[p].name)) == 0)
					trackerPage_ = p;
			}
			// by the area's number first: the names may differ (the hotel is not Sirena Beach)
			static const int8_t kAreaPage[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 8 };
			if (state().area >= 0 && state().area < 10)
				trackerPage_ = kAreaPage[state().area];
		} else if (pressed & (SMS_PAD_Y | SMS_PAD_B)) {
			trackerOpen_      = false;
			trackerSwallow_   = b; // B or Y closes the tracker, not the pause menu under it
		} else if (pressed & SMS_PAD_A) {
			trackerHideDone_ = !trackerHideDone_;
		} else if (pressed & SMS_PAD_X) {
			trackerHideOut_ = !trackerHideOut_;
		} else if (pressed & (SMS_PAD_RIGHT | SMS_PAD_R)) {
			trackerPage_ = (trackerPage_ + 1) % kTrackerMapCount;
			centre       = true;
		} else if (pressed & (SMS_PAD_LEFT | SMS_PAD_L)) {
			trackerPage_ = (trackerPage_ + kTrackerMapCount - 1) % kTrackerMapCount;
			centre       = true;
		}
		// the cursor, in the map's pixels (the map loads when drawn: its size then)
		int iw = 0, ih = 0;
		if (!(trackerImage_ && trackerImagePage_ == trackerPage_ && draw().image_size(trackerImage_, &iw, &ih))) {
			iw = 2000;
			ih = 1300;
		}
		if (centre || trackerCursorX_ > iw || trackerCursorY_ > ih) {
			trackerCursorX_ = iw / 2.0f;
			trackerCursorY_ = ih / 2.0f;
		}
		if (centre)
			trackerZoom_ = 1;
		// the C-stick zooms: up in, down out
		const float cs = pad.cstick_y / 127.0f;
		if (fabsf(cs) > 0.2f)
			trackerZoom_ = fmaxf(1.0f, fminf(6.0f, trackerZoom_ * (1 + cs * 0.04f)));
		// across the map in about two seconds
		const float sx = pad.stick_x / 127.0f, sy = pad.stick_y / 127.0f;
		if (fabsf(sx) > 0.2f)
			trackerCursorX_ = fminf((float)iw, fmaxf(0.0f, trackerCursorX_ + sx * iw / 120 / trackerZoom_));
		if (fabsf(sy) > 0.2f)
			trackerCursorY_ = fminf((float)ih, fmaxf(0.0f, trackerCursorY_ - sy * ih / 120 / trackerZoom_));
		pad.buttons   = 0;
		pad.stick_x   = pad.stick_y = pad.cstick_x = pad.cstick_y = 0;
		pad.trigger_l = pad.trigger_r = pad.analog_a = pad.analog_b = 0;
		return true;
	}

	// ------------------------------------------------------------------- stages

	// The level the shuffle loads in place of `level` (itself when it has none).
	uint16_t swapOf(uint16_t level) const
	{
		for (size_t i = 0; i < swaps_.size(); ++i) {
			if (swaps_[i].first == level)
				return swaps_[i].second;
		}
		return level;
	}

	// Has the file met the win condition (Corona Mountain opens)?
	bool winReached()
	{
		const int w = kWins[setting("win", 0) < 0 || setting("win", 0) > 5 ? 0 : setting("win", 0)];
		if (w > 0)
			return game().shines() >= w;
		static const int kShadowMarios[] = { 6, 16, 26, 36, 46, 56, 66 };
		for (int i = 0; i < 7; ++i) {
			if (!game().has_shine(kShadowMarios[i]))
				return false;
		}
		return true;
	}

	// Open plaza gates (an option, not the randomizer's): the plaza's ways to Bianco Hills,
	// Ricco Harbor and Gelato Beach open from the start, without their goop Piranha
	// Plants: the Grand Pianta statue raised (0x10384) and the gates open (0x10385,
	// 0x10386, 0x10387; TModelGate). Set on the file as it is loaded or started, before
	// the plaza picks its episode from them.
	void openPlazaGates()
	{
		if (!enabled() || !setting("open_gates", 0))
			return;
		static const uint32_t kGates[] = { 0x10384, 0x10385, 0x10386, 0x10387 };
		for (size_t i = 0; i < sizeof kGates / sizeof kGates[0]; ++i) {
			if (game().flag_get(kGates[i]) == 0)
				game().flag_set(kGates[i], 1);
		}
	}

	// decideNextScenario's plaza episode, with its "every Shadow Mario" case left out
	// (as the randomizer patches it).
	int plazaEpisodeWithoutShadows()
	{
		if (game().flag_get(0x103AE))
			return 2;
		if (game().flag_get(0x10389))
			return 8;
		if (game().flag_get(0x10386) && game().flag_get(0x10387))
			return game().shines() >= 10 ? 7 : 6;
		if (game().flag_get(0x10385))
			return 5;
		if (game().flag_get(0x10384))
			return 1;
		return 0;
	}

	// Does the game carry API 1.5 beta `beta` (or a later one, or the release)?
	bool gameHasBeta(uint32_t beta) const
	{
		if (api().minor != 5)
			return api().minor > 5;
		return api().size >= offsetof(SmsApi, prerelease) + sizeof(uint32_t)
		       && (api().prerelease == 0 || api().prerelease >= beta);
	}
	bool gameHasStageLoading() const { return gameHasBeta(4); }

#ifdef RANDOMIZER_BETA6
	// The file's game, kept with the save file as the randomizer keeps it in the save:
	// its seed and the settings that shape it.
	void writeFileGame()
	{
		if (!fileOpen_ || !SMS_API_HAS(&api(), files))
			return;
		char buf[256];
		const int n = snprintf(buf, sizeof buf, "seed=%d\nall_unlocks=%d\nwin=%d\nlevels=%d\ncoin_shines=%d\ntrade_shines=%d\n"
		                       "coin_unlocks=%d\nshine_swaps=%d\nentrance_mode=%d\n",
		                       seed(), allUnlocks() ? 1 : 0, setting("win", 0), shuffle() ? 1 : 0, coinShines() ? 1 : 0,
		                       tradeShines() ? 1 : 0, coinUnlocks() ? 1 : 0, shineSwaps() ? 1 : 0, entranceSetting());
		api().files->write(handle(), SMS_FILES_CURRENT, "game.txt", buf, (uint32_t)n);
	}

	// The file's game into the settings (the Mods menu shows it); false when the file
	// has none (a file from before the mod).
	bool readFileGame()
	{
		if (!SMS_API_HAS(&api(), files))
			return false;
		char buf[256];
		int n = api().files->read(handle(), SMS_FILES_CURRENT, "game.txt", buf, sizeof buf - 1);
		if (n <= 0)
			return false;
		buf[n < (int)sizeof buf - 1 ? n : (int)sizeof buf - 1] = 0;
		int levels = -1, mode = -1; // a file from before the entrance modes has only levels (on, or off)
		static const char* const kKeys[] = { "seed", "all_unlocks", "win", "coin_shines", "trade_shines",
		                                     "coin_unlocks", "shine_swaps" };
		for (char* line = strtok(buf, "\n"); line; line = strtok(nullptr, "\n")) {
			char* eq = strchr(line, '=');
			if (!eq)
				continue;
			*eq = 0;
			if (strcmp(line, "levels") == 0)
				levels = atoi(eq + 1);
			if (strcmp(line, "entrance_mode") == 0)
				mode = atoi(eq + 1);
			for (size_t k = 0; k < sizeof kKeys / sizeof kKeys[0]; ++k) {
				if (strcmp(line, kKeys[k]) == 0)
					config().set_int(handle(), kKeys[k], atoi(eq + 1));
			}
		}
		if (mode < 0 && levels >= 0)
			mode = levels ? 5 : 0;
		if (mode >= 0)
			config().set_int(handle(), "entrance_mode", mode);
		config().save(handle());
		readTaken();
		return true;
	}

	// Every Shine reachable in the level loaded for `level`: its own, then those down
	// its ways out, each through the shuffle (the randomizer's FUN_80603640).
	void episodeShines(uint16_t level, bool* seen, std::vector<int>& out) const
	{
		const int li = indexOfLevel(swapOf(level));
		if (li < 0 || seen[li])
			return;
		seen[li]                = true;
		const gen::LevelData& L = gen::kLevels[li];
		for (int k = 0; k < L.shineCount; ++k)
			out.push_back(gen::kShines[L.shineFirst + k].id >> 8);
		for (int k = 0; k < L.exitCount; ++k)
			episodeShines(gen::kExits[L.exitFirst + k].to, seen, out);
	}

	// Does taking this Shine give an unlock (a swapped one gives its blue coin's)?
	bool unlockShine(int shine) const
	{
		const int place = shine >= 0 && shine < 121 && shineCoin_[shine] >= 0 ? gen::kApCoinBase + shineCoin_[shine] : shine;
		for (int i = 0; i < kUnlockCount; ++i) {
			if (shine_[i] == place)
				return true;
		}
		return false;
	}
#endif

	static int indexOfLevel(uint16_t id)
	{
		for (int i = 0; i < gen::kLevelCount; ++i) {
			if (gen::kLevels[i].id == id)
				return i;
		}
		return -1;
	}

#ifdef RANDOMIZER_STAGE_LOADING
	// The stage about to load, as the randomizer's own hook on it decides: a few ways out
	// go somewhere set, the level is swapped for the one the shuffle put there, the
	// episode flag follows, the plaza opens Corona Mountain once the win condition is
	// met, and Mario comes back to the plaza by the way in he took.
	void stageLoading(SmsEvStageLoading& e)
	{
		loadingSeen_     = true;
		setupDone_       = false;
		unlockShines_.clear();
		swapShines_.clear();
		coinTints_.clear();
		coinSparkles_.clear();
		const bool shine = shineTaken_;
		shineTaken_      = false;
		if (!enabled())
			return;
		refresh();
		restoreGot();
		const int askedArea = e.area, askedEpisode = e.episode;
		if (game().flag_get(0x30002))
			return; // after a stage's clear the game's choice stands, as with the randomizer
		const bool sh = shuffle();
		// Mario respawning in the level he is in (already shuffled) is not shuffled again
		const bool again = (e.reason == SMS_LEAVE_LIFE_LOST || e.reason == SMS_LEAVE_GAME_OVER)
		                   && e.area == targetArea_ && e.episode == targetEpisode_;
		bool placed = again;
		// a warp (a mod's game->warp, the port's warp menu) goes where it was asked, not
		// through the shuffle; a save state's load can't be sent elsewhere
		if (e.reason == SMS_LEAVE_WARP)
			placed = true;
#ifdef RANDOMIZER_16_BETA2
		if (e.reason == SMS_LEAVE_STATE_LOAD)
			return;
#endif
		if (sh && startPlaced_ == (e.area << 8 | e.episode)) {
			placed = true; // FILE_SELECTED sent it to the shuffle's first level
			startPlaced_ = -1;
		} else if (sh && !again) {
			// the randomizer's set ways out, by where Mario comes from
			if (e.from_area == 0x05 && e.area == 0x0D) {
				// Pinna Beach to Pinna Park: the park's episode for the beach's
				const int li = indexOfLevel((uint16_t)(0x500 | e.from_episode));
				for (int k = 0; li >= 0 && k < gen::kLevels[li].exitCount; ++k) {
					const uint16_t to = gen::kExits[gen::kLevels[li].exitFirst + k].to;
					if ((to >> 8) == 0x0D) {
						e.episode = to & 0xFF;
						break;
					}
				}
			} else if (e.from_area == 0x3A && e.from_episode == 0) {
				e.area    = 0x0D;
				e.episode = 5;
				placed    = true;
			} else if (e.from_area == 0x18 && e.from_episode == 0 && !shine) {
				e.area    = 0;
				e.episode = 0;
			} else if (e.from_area == 0x06 && e.from_episode == 4) {
				e.area    = 0x0E;
				e.episode = 1;
			}
		}
		if (sh && e.from_area == 1)
			entered_ = (uint16_t)(e.area << 8 | e.episode);
		if (sh && !placed) {
			const uint16_t to = swapOf((uint16_t)(e.area << 8 | e.episode));
			e.area            = to >> 8;
			if (e.area != 1) // the plaza: the episode the game chose
				e.episode = to & 0xFF;
			game().flag_set(0x40003, episodeFlag(to >> 8, to & 0xFF));
		} else if (sh && !again) {
			game().flag_set(0x40003, episodeFlag(e.area, e.episode));
		}
		// Open plaza gates: the flags are set again here, and a plaza that came up as its first
		// episode (the gates shut, goop everywhere) takes the episode they give
		if (e.area == 1 && e.episode == 0 && setting("open_gates", 0)) {
			openPlazaGates();
			e.episode = plazaEpisodeWithoutShadows();
		}
		// The randomizer turns off the game's own way to Corona Mountain (every Shadow
		// Mario beaten): the plaza's episode is then what decideNextScenario gives
		// without it, and only the win condition opens the mountain.
		if (e.area == 1 && e.episode == 9 && game().flag_get(0x103AE) == 0)
			e.episode = plazaEpisodeWithoutShadows();
		if (winReached()) {
			if (game().flag_get(0x103AE) == 0) {
				if (e.area == 1)
					e.episode = 9; // Delfino Plaza with Corona Mountain open
			} else {
				static const uint32_t kPlazaFlags[] = { 0x10384, 0x10385, 0x10386, 0x10387, 0x10389, 0x103A9 };
				for (size_t i = 0; i < sizeof kPlazaFlags / sizeof kPlazaFlags[0]; ++i)
					game().flag_set(kPlazaFlags[i], 1);
			}
		}
		if (sh) {
			e.from_area    = entered_ >> 8;
			e.from_episode = entered_ & 0xFF;
		}
		targetArea_    = e.area;
		targetEpisode_ = e.episode;
		markSeen(e.area, e.episode);
		debug("loading %d,%d -> %d,%d (from %d,%d, reason %d)", askedArea, askedEpisode, e.area, e.episode, e.from_area,
		      e.from_episode, e.reason);
	}
#endif

	// ------------------------------------------------------------------- load

	int load() override
	{
		loadExclusions();
		listKey_ = bindKey("unlocks", "F4");
		posKey_  = bindKey("position", "F2");
		static const std::vector<const char*> winNames = { "All Shadow Marios", "16 Shines", "30 Shines",
			                                               "50 Shines", "60 Shines", "96 Shines" };
		static const std::vector<const char*> modeNames = { "Off", "Worlds among themselves", "Secret courses among themselves",
		                                                    "Worlds and secret courses, each among its own", "Secret courses among themselves, the rest mixed", "All mixed" };
		optionBool("enabled", "Randomizer", true);
		optionNumber("seed", "Seed", (float)kDefaultSeed, 1, (float)kMaxSeed, 1);
		// New seed: draws a seed and puts it in the Seed row in place of the one there
		SmsOption newSeed = SmsOption();
		newSeed.size      = sizeof newSeed;
		newSeed.key       = "new_seed";
		newSeed.label     = "New seed";
		newSeed.type      = SMS_OPTION_ACTION;
		newSeed.on_change = [](SmsMod*, const char*, void* user) { static_cast<Randomizer*>(user)->drawSeed(); };
		newSeed.user      = this;
		config().add_option(handle(), &newSeed);
		// Write spoiler log: the seed's unlocks and levels, in the mod's data folder
		SmsOption spoiler = SmsOption();
		spoiler.size      = sizeof spoiler;
		spoiler.key       = "spoiler";
		spoiler.label     = "Write spoiler log";
		spoiler.type      = SMS_OPTION_ACTION;
		spoiler.on_change = [](SmsMod*, const char*, void* user) { static_cast<Randomizer*>(user)->writeSpoiler(); };
		spoiler.user      = this;
		config().add_option(handle(), &spoiler);
		optionChoice("win", "Win condition", winNames, 0);
		optionChoice("entrance_mode", "Entrances", modeNames, 0);
		optionBool("all_unlocks", "Start with all unlocks", false);
		optionBool("random_seed", "Use random seed", false);
		optionBool("infinite_lives", "Infinite lives", false);
		optionBool("open_gates", "Open plaza gates", false);
		optionBool("coin_shines", "100-coin Shines hold unlocks", false);
		optionBool("trade_shines", "Blue coin trades hold unlocks", false);
		optionBool("coin_unlocks", "Blue coins hold unlocks", false);
		optionBool("shine_swaps", "Some Shines and blue coins trade places", false);
		optionBool("tint_swaps", "Shines that give a blue coin are blue", true);
		optionBool("tint_unlock_coins", "Blue coins that hold an unlock look like a purple Shine", true);
		optionBool("tint_swap_coins", "Blue coins that give a Shine look like one", true);

		// Mario's actions
		on<SmsEvPlayerStatus>(SMS_EV_PLAYER_STATUS, [this](SmsEvPlayerStatus& e) -> int {
			if (!active())
				return SMS_CONTINUE;
			return statusRule(e.to);
		});
#ifdef RANDOMIZER_16_BETA2
		// every world's episode select offers its eight episodes from the start (the
		// logic needs none of them to be opened in order)
		on<SmsEvEpisodeSelect>(SMS_EV_EPISODE_SELECT, [this](SmsEvEpisodeSelect& e) {
			if (enabled() && e.max_offered > 0) // with all unlocks too
				e.offered = e.max_offered;
		});
#else
		// The game starts the rotate jumps itself (considerJumpRotate), without asking
		// first: turned into a jump once they have started.
		on<SmsEvPlayerStatus>(SMS_EV_PLAYER_STATUS_CHANGED, [this](SmsEvPlayerStatus& e) -> int {
			if (active() && (e.to == 0x895 || e.to == 0x896) && locked(SPIN_JUMP))
				player().set_status(kJump, 0);
			return SMS_CONTINUE;
		});
#endif

		// A new file starts without the intro: with the shuffle, in the level the randomizer
		// put in the airstrip's place (what the seed's first Shines are worked out from);
		// without it, in Delfino Plaza.
		on<SmsEvStage>(SMS_EV_FILE_SELECTED, [this](SmsEvStage& e) {
			if (!enabled() || e.area != 0)
				return;
			if (setting("random_seed", 0) && !gameHasBeta(6))
				drawSeed(); // Use random seed: a new file gets its own (SAVE_FILE draws it on beta 6)
			refresh();
			const uint16_t first = shuffle() ? swapOf(0) : (uint16_t)0x100;
			e.area               = first >> 8;
			e.episode            = first & 0xFF;
			startPlaced_         = shuffle() ? first : -1;
		});

#ifdef RANDOMIZER_STAGE_LOADING
		if (gameHasStageLoading()) {
			on<SmsEvStageLoading>(SMS_EV_STAGE_LOADING, [this](SmsEvStageLoading& e) { stageLoading(e); });
			on<SmsEvShineActor>(SMS_EV_SHINE_GET, [this](SmsEvShineActor&) { shineTaken_ = true; });
		}
#endif

#ifdef RANDOMIZER_BETA6
		if (gameHasBeta(6)) {
			// each save file keeps its game: a new one takes the seed (a random one with Use
			// random seed), a loaded one gives back its own
			on<SmsEvSaveFile>(SMS_EV_SAVE_FILE, [this](SmsEvSaveFile& f) {
				debug("save file %d: what %d, random seed %d", f.file, f.what, setting("random_seed", 0));
				livesKept_ = -1; // another file: its own lives
				if (f.what == SMS_SAVE_FILE_ERASED)
					eraseSlot(f.file);
				else if (f.what == SMS_SAVE_FILE_COPIED)
					copySlot(f.from, f.file);
				if (f.what == SMS_SAVE_FILE_NEW || f.what == SMS_SAVE_FILE_LOADED)
					openPlazaGates();
				if (f.what == SMS_SAVE_FILE_NEW) {
					fileOpen_ = true;
					slot_     = f.file;
					newFile_  = enabled();
					std::fill(seenLevels_.begin(), seenLevels_.end(), 0); // a new game: no level known yet
					std::fill(earned_.begin(), earned_.end(), 0);
					std::fill(got_.begin(), got_.end(), 0);
					writeGot();
					writeEarned();
					writeTaken();
					writeSeen();
					std::fill(taken_.begin(), taken_.end(), 0);
					// Use random seed: drawn here unless the setup over the file's menu did,
					// and the player saw (and maybe changed) it there
					if (enabled() && setting("random_seed", 0) && !setupDone_)
						drawSeed();
					appliedSeed_ = -1; // refresh() writes the file's game
					refresh();
					writeFileGame();
				} else if (f.what == SMS_SAVE_FILE_LOADED) {
					fileOpen_ = true;
					slot_     = f.file;
					if (!readFileGame())
						writeFileGame(); // a file from before the mod: it keeps the settings' game
					refresh();
					restoreGot();
				}
			});

			// the spam spray: without it, A while R is held does not spray in bursts
			on<SmsEvNozzleFire>(SMS_EV_NOZZLE_FIRE, [this](SmsEvNozzleFire& e) -> int {
				return e.how == SMS_FIRE_BURST && e.nozzle == 0 && locked(SPAM_SPRAY) ? SMS_CANCEL : SMS_CONTINUE;
			});

			// without the rollercoaster cart, Pinna Park's coaster stage (0x3A, episodes 0
			// and 1) starts as any other, not in the coaster
			on<SmsEvPlayerSpawn>(SMS_EV_PLAYER_SPAWN, [this](SmsEvPlayerSpawn& e) {
				const int area = loadingSeen_ ? targetArea_ : state().area;
				const int ep   = loadingSeen_ ? targetEpisode_ : state().episode;
				if (e.kind == SMS_START_COASTER && area == 0x3A && ep < 2 && locked(CART))
					e.kind = SMS_START_WAIT;
				applyNoFludd();

			});

			// the Shines that give an unlock: the randomizer's model, cycling colour
			on<SmsEvShineSetup>(SMS_EV_SHINE_SETUP, [this](SmsEvShineSetup& e) {
				if (!enabled() || e.shine < 0)
					return;
				// a model of the mod's, if its file is there ("-" when not)
				auto modelFile = [this](const char* name, std::string& cache) {
					if (cache.empty()) {
						const std::string path = std::string(core().mod_dir(handle())) + "/" + name;
						FILE* f                = fopen(path.c_str(), "rb");
						cache                  = f ? name : "-";
						if (f)
							fclose(f);
					}
					return cache != "-";
				};
				if (active() && unlockShine(e.shine)) {
					static std::string model;
					if (modelFile("unlockshine.bmd", model))
						e.model = model.c_str();
					if (!e.collected)
						unlockShines_.push_back(e.object);
				} else if (tintSwaps() && e.shine < 121 && shineCoin_[e.shine] >= 0) {
					// a Shine that gives a blue coin: blue (a model of its own: a model's colour
					// is shared by the Shines that have it)
					static std::string model;
					if (modelFile("swapshine.bmd", model))
						e.model = model.c_str();
					if (!e.collected)
						swapShines_.push_back(e.object);
				}
			});
			// a blue coin that holds an unlock, or gives a Shine (the swaps): tinted, each its own option
			on<SmsEvItemSetup>(SMS_EV_ITEM_SETUP, [this](SmsEvItemSetup& e) {
				if (!swapsOn() || e.kind != SMS_ITEM_BLUE_COIN || e.flag < 0x10000u || !SMS_API_HAS(api().game, object_set_tint))
					return;
				for (int c = 0; c < gen::kApCoinCount; ++c) {
					if (0x10000u + gen::kApCoins[c].bit != e.flag)
						continue;
					bool unlock = false;
					for (int i = 0; active() && i < kUnlockCount; ++i)
						unlock = unlock || shine_[i] == gen::kApCoinBase + c;
					if (unlock && setting("tint_unlock_coins", 1)) {
						coinTints_.push_back({ e.object, SMS_RGBA(190, 70, 255, 200), 0 }); // purple
						// API 1.8 beta 3: a purple Shine, with its sparkles (tinted once its model exists)
						if (SMS_API_HAS(api().game, object_particle)) {
							e.game_model = SMS_GAME_MODEL_SHINE;
							coinSparkles_.push_back(e.object);
						}
					}
					else if (coinShine_[c] >= 0 && setting("tint_swap_coins", 1)) {
						// API 1.8 beta 3: the game's own Shine model, with its sparkles asked for each tick.
						if (SMS_API_HAS(api().game, object_particle)) {
							e.game_model = SMS_GAME_MODEL_SHINE;
							coinSparkles_.push_back(e.object);
							return;
						}
						// Before: a Shine's model with only one joint (coinshine.bmd, made from the disc's shine.bmd:
						// the game places only a coin's joint 0), in its own yellow; without the file, the coin
						// itself in gold.
						static std::string model;
						if (model.empty()) {
							FILE* f = fopen((std::string(core().mod_dir(handle())) + "/coinshine.bmd").c_str(), "rb");
							model   = f ? "coinshine.bmd" : "-";
							if (f)
								fclose(f);
						}
						if (model != "-") {
							e.model = model.c_str();
						} else {
							coinTints_.push_back({ e.object, SMS_RGBA(255, 215, 0, 200), 0 });
						}
					}
					return;
				}
			});
			on<SmsEvTick>(SMS_EV_TICK, [this](SmsEvTick&) {
				// a Shine's four effects on joint 2 of each coin drawn as a Shine (they last only while asked for)
				if (SMS_API_HAS(api().game, object_particle) && state().mode == SMS_MODE_STAGE) {
					static const uint32_t kShineEffects[4] = { 0x127, 0x128, 0x129, 0x12A }; // PARTICLE_MS_SHINE_PROMI, _SENKO, _KIRA, _BOW
					for (size_t i = 0; i < coinSparkles_.size(); ++i)
						for (int k = 0; k < 4; ++k)
							game().object_particle(coinSparkles_[i], kShineEffects[k], 2);
				}
				// the coins' tints: not as the coin is set up (it has no model yet), but once
				// the stage runs; the object returns 0 until it has one, so it is asked again
				if (SMS_API_HAS(api().game, object_set_tint) && state().mode == SMS_MODE_STAGE) {
					for (size_t i = 0; i < coinTints_.size();) {
						const CoinTint& t = coinTints_[i];
						int ok            = 0;
						if (t.variant == 0 || t.variant == 4) {
							ok = game().object_set_tint(t.object, t.color);
						} else {
							// 1 the material's colour, 2 the TEV registers, 3 both
							if (t.variant != 2)
								ok = game().object_set_color(t.object, SMS_COLOR_MATERIAL, t.color);
							if (t.variant != 1) {
								for (int r = SMS_COLOR_TEV0; r <= SMS_COLOR_TEV2; ++r)
									ok = game().object_set_color(t.object, r, t.color) || ok;
							}
						}
						if (ok)
							coinTints_.erase(coinTints_.begin() + i);
						else
							++i;
					}
				}
				if (!SMS_API_HAS(api().game, object_set_color))
					return;
				for (size_t i = 0; i < swapShines_.size(); ++i)
					game().object_set_color(swapShines_[i], SMS_COLOR_MATERIAL, SMS_RGB(70, 140, 255));
				if (unlockShines_.empty())
					return;
				// The hue goes back and forth from green to red through cyan, blue and
				// magenta (0.25 to 1.0), never yellow or orange: those are a plain Shine's
				// colours (the randomizer goes round every hue, 0.005 a frame).
				const double t      = fmod(core().time() * 0.15, 2.0);
				const double h      = 0.25 + 0.75 * (t < 1.0 ? t : 2.0 - t);
				const SmsColor c    = unlockShineColor(h);
				for (size_t i = 0; i < unlockShines_.size(); ++i)
					game().object_set_color(unlockShines_[i], SMS_COLOR_MATERIAL, c);
			});

			// Yoshi's egg is there once Yoshi is unlocked, wherever the game puts one; the
			// plaza's egg event is set up only before he has hatched there, and its own egg
			// never comes (as the randomizer patches TEggYoshi::load and setupObjects)
			on<SmsEvYoshiEgg>(SMS_EV_YOSHI_EGG, [this](SmsEvYoshiEgg& e) {
				if (!enabled())
					return;
				const bool yoshi = !locked(YOSHI);
				if (e.kind == SMS_EGG_STAGE)
					e.present = yoshi ? 1 : 0;
				else if (e.kind == SMS_EGG_PLAZA_EVENT)
					e.present = yoshi && game().flag_get(0x1038F) == 0 ? 1 : 0;
				else if (e.kind == SMS_EGG_EVENT_EGG)
					e.present = 0;
			});
			on<SmsEvFlag>(SMS_EV_FLAG_CHANGED, [this](SmsEvFlag& e) -> int {
				if (enabled() && e.id == 0x60003 && e.value == 3)
					e.value = 0;
				if (enabled() && !ownFlag_ && slot_ >= 0 && e.value != 0 && e.old == 0 && e.id >= 0x10000 && e.id <= 0x10365) {
					int place = -1;
					if (e.id < 0x10000 + 120) {
						place = (int)(e.id - 0x10000);
					} else {
						for (int c = 0; c < gen::kApCoinCount && place < 0; ++c) {
							if (0x10000u + gen::kApCoins[c].bit == e.id)
								place = gen::kApCoinBase + c;
						}
					}
					if (place >= 0 && !got_[place]) {
						got_[place] = 1;
						writeGot();
					}
				}
				if (!ownFlag_ && e.value != 0 && e.old == 0 && e.id >= 0x10078 && e.id <= 0x10365 && swapsOn()) {
					for (int c = 0; c < gen::kApCoinCount; ++c) {
						if (0x10000u + gen::kApCoins[c].bit != e.id)
							continue;
						const int sh = coinShine_[c];
						if (sh < 0) {
							popupFor(gen::kApCoinBase + c);
						} else {
							// its Shine instead: the blue coin count back, the Shine had
							ownFlag_ = true;
							game().flag_set(0x40001, game().flag_get(0x40001) - 1);
							if (!game().has_shine(sh)) {
								game().flag_set(0x10000u + sh, 1);
								game().flag_set(0x40000, game().flag_get(0x40000) + 1);
							}
							ownFlag_ = false;
							showSwapNote("+1 SHINE");
							popupFor(sh);
						}
						break;
					}
				}
				return SMS_CONTINUE;
			});

#ifdef RANDOMIZER_16_BETA2
			// a swapped Shine is taken quietly (no Shine Get, Mario plays on) and gives a
			// blue coin; once
			on<SmsEvShineTouch>(SMS_EV_SHINE_TOUCH, [this](SmsEvShineTouch& t) {
				if (!swapsOn() || t.shine < 0 || t.shine >= 121 || shineCoin_[t.shine] < 0)
					return;
				t.take = SMS_SHINE_TAKE_QUIET;
				if (taken_[t.shine])
					return;
				taken_[t.shine] = 1;
				writeTaken();
				ownFlag_ = true;
				game().flag_set(0x40001, game().flag_get(0x40001) + 1);
				ownFlag_ = false;
				audio().play_system_sound(SMS_SOUND_COIN);
				showSwapNote("+1 BLUE COIN");
				popupFor(gen::kApCoinBase + shineCoin_[t.shine]);
			});
#endif

			// the Shine an episode stands for, as the randomizer has it: the first the file
			// has not got of every Shine reachable in the level the shuffle loads there
			// (its own, then those down its ways out, shuffled too); so the episode shows
			// done only once all of them are collected
			on<SmsEvEpisodeShine>(SMS_EV_EPISODE_SHINE, [this](SmsEvEpisodeShine& e) {
				if (!enabled())
					return;
				refresh();
				std::vector<int> shines;
				bool seen[gen::kLevelCount] = {};
				episodeShines((uint16_t)(e.area << 8 | e.episode), seen, shines);
				// only a Shine the game has an episode name for (scScenarioNameTable: 0 to
				// 69); the episode select reads past it, and faults, for the others
				shines.erase(std::remove_if(shines.begin(), shines.end(), [](int s) { return s < 0 || s >= 70; }),
				             shines.end());
				if (shines.empty())
					return;
				e.shine = shines[0];
				for (size_t i = 0; i < shines.size(); ++i) {
					if (!game().has_shine(shines[i])) {
						e.shine = shines[i];
						break;
					}
				}
			});
		}
#endif

		// a game without SMS_EV_STAGE_LOADING: the shuffle as the stage is left, where the
		// episode is known (not a world's way in, whose episode select comes later)
		on<SmsEvStage>(SMS_EV_STAGE_LEAVING, [this](SmsEvStage& e) {
			if (!enabled() || loadingSeen_)
				return;
			refresh();
			// Mario respawning in the level he is in (already shuffled) is not shuffled again
			const bool again = (e.reason == SMS_LEAVE_LIFE_LOST || e.reason == SMS_LEAVE_GAME_OVER)
			                   && e.area == targetArea_ && e.episode == targetEpisode_;
			if (!again && startPlaced_ < 0 && shuffle() && e.episode != SMS_EPISODE_SELECT && e.episode < 255) {
				const uint16_t to = swapOf((uint16_t)(e.area << 8 | e.episode));
				e.area            = to >> 8;
				e.episode         = to & 0xFF;
			}
			startPlaced_   = -1;
			targetArea_    = e.area;
			targetEpisode_ = e.episode;
			markSeen(e.area, e.episode);
			if (e.area == 1 && e.episode == 255 && winReached() && game().flag_get(0x103AE) == 0)
				e.episode = 9; // Delfino Plaza with Corona Mountain open
		});

		// FLUDD: which nozzles Mario may use, as the randomizer decides
		on<SmsEvPlayerNozzle>(SMS_EV_PLAYER_NOZZLE, [this](SmsEvPlayerNozzle& e) -> int {
			if (!active())
				return SMS_CONTINUE;
			if (e.reason == SMS_NOZZLE_SWAP) {
				// swapping needs the plain spray together with another nozzle (or neither)
				if (!locked(SPRAY) != otherNozzle()) {
					audio().play_system_sound(0x483D);
					return SMS_CANCEL;
				}
				return SMS_CONTINUE;
			}
			if (e.reason == SMS_NOZZLE_BOX)
				return nozzleAllowed(e.to) ? SMS_CONTINUE : SMS_CANCEL;
			// a switch, or the nozzle a stage gives at its start: without the spray or
			// any other nozzle there is no FLUDD
			if (locked(SPRAY) && !otherNozzle() && SMS_API_HAS(api().player, set_fludd))
				player().set_fludd(0);
			return nozzleAllowed(e.to) ? SMS_CONTINUE : SMS_CANCEL;
		});

		on<SmsEvStage>(SMS_EV_STAGE_START, [this](SmsEvStage&) {
			applyStart();
			if (newFile_ && !setupAtStart_) {
				newFile_ = false;
				openSetup();
			}
		});
		on<SmsEvFrame>(SMS_EV_FRAME, [this](SmsEvFrame&) {
			if (setupOpen_ && !setupAtMenu_)
				setupInput();
		});

#ifdef RANDOMIZER_FILE_MENU
		// An empty file's menu at file select (START alone, "New"): the setup opens over
		// it. Its A on START and its B close the setup and let go of the controller in
		// SMS_EV_PAD_READ that frame, so the press reaches the game: A starts the new
		// game as set, B closes the file's menu with nothing started.
		if (api().minor >= 6) {
			on<SmsEvFileMenu>(SMS_EV_FILE_MENU, [this](SmsEvFileMenu& e) {
				if (e.open && e.is_empty && enabled() && !setupOpen_) {
					setupAtMenu_ = true;
					openSetup();
					setupAtMenu_ = setupOpen_;
				} else if (!e.open && setupOpen_ && setupAtMenu_) {
					closeSetup(); // the menu went (the memory card taken out)
					setupAtMenu_ = false;
				}
			});
		}
#endif

		// Infinite lives (an option, not the randomizer's): the lives never go down (a
		// 1-up still adds one), and the last one lost is not a game over
		on<SmsEvTick>(SMS_EV_TICK, [this](SmsEvTick&) {
			if (!setting("infinite_lives", 0)) {
				livesKept_ = -1;
				return;
			}
			const int lives = game().lives();
			if (livesKept_ < 0 || lives > livesKept_)
				livesKept_ = lives;
			else if (lives < livesKept_)
				game().flag_set(0x20001, livesKept_);
		});
		on(SMS_EV_GAME_OVER, [this](SmsEvent&) -> int { return setting("infinite_lives", 0) ? SMS_CANCEL : SMS_CONTINUE; });

#ifdef RANDOMIZER_BETA6
		// without wall kicks, Mario's knock against a wall makes no sound (the randomizer
		// gates it in TMario::jumpingBasic)
		if (gameHasBeta(2)) {
			on<SmsEvSound>(SMS_EV_SOUND, [this](SmsEvSound& e) -> int {
				switch (e.id) {
				case 0x1802: case 0x1803: case 0x1948: case 0x1949: case 0x194A: case 0x194B:
					return locked(WALL_KICKS) ? SMS_CANCEL : SMS_CONTINUE;
				}
				return SMS_CONTINUE;
			});
		}
#endif
#ifdef RANDOMIZER_BETA7
		if (gameHasBeta(7)) {
			setupAtStart_ = true;
			// a new file's setup comes first: the file select waits while it is open
			on<SmsEvFileStarting>(SMS_EV_FILE_STARTING, [this](SmsEvFileStarting& e) -> int {
				if (!e.is_new || !enabled() || setupDone_)
					return SMS_CONTINUE;
				if (!setupOpen_)
					openSetup();
				if (setupOpen_)
					return SMS_CANCEL;
				setupDone_ = true; // Start (or no controller to take): the game goes on
				newFile_   = false;
				return SMS_CONTINUE;
			});
		}
#endif

		// the message
		on<SmsEvShine>(SMS_EV_SHINE_COLLECTED, [this](SmsEvShine& e) {
			if (!active())
				return;
			applyOutfit();
			for (int i = 0; i < kUnlockCount; ++i) {
				if (shine_[i] == e.shine) {
					popup_   = i;
					popupAt_ = core().time();
					break;
				}
			}
		});

		on<SmsEvDraw>(SMS_EV_DRAW, [this](SmsEvDraw& d) {
			if (setupOpen_) {
				drawSetup(d);
				return;
			}
			if (d.mode == SMS_MODE_TITLE && enabled()) {
				drawSelect(d);
				return;
			}
			if (d.mode != SMS_MODE_STAGE || !enabled())
				return;
			refresh();
			if (planFailed_) {
				// the seed has no game with these options: said while playing too
				draw().text(d.canvas_w / 2, 10, 14, SMS_RGB(255, 90, 90), SMS_TEXT_CENTER | SMS_TEXT_SHADOW,
				            "RANDOMIZER: NO GAME WITH THESE OPTIONS (EXCLUSIONS OR PLANDO?)");
			}
			applyHud();
			drawPopup(d);
			drawDpad();
			drawWarpRing();
			drawList(d);
			if (trackerOpen_)
				drawTracker(d);
		});

		on<SmsEvKey>(SMS_EV_KEY, [this](SmsEvKey& k) -> int {
			if (k.scancode == posKey_) {
				const SmsPlayer m = mario();
				if (k.down && !k.repeat && m.valid)
					info("position: area %d episode %d, %.0f %.0f %.0f", state().area, state().episode, m.position.x,
					     m.position.y, m.position.z);
				return SMS_CANCEL;
			}
			if (k.scancode != listKey_)
				return SMS_CONTINUE;
			if (k.down && !k.repeat)
				listShown_ = !listShown_;
			return SMS_CANCEL;
		});

		// the Y-button camera comes from a Shine: asking for it without it plays the
		// "not yet" sound (the randomizer blocks the camera's Y meaning, 0x4000)
		on<SmsEvPadRead>(SMS_EV_PAD_READ, [this](SmsEvPadRead& e) {
			if (setupOpen_ && setupAtMenu_) {
				// the setup over a file's menu reads its controls here, so that closing it
				// lets go of the controller in time for the game to see the press
				setupInput();
				if (!setupOpen_)
					setupAtMenu_ = false;
				return;
			}
			if (!e.pads)
				return;
			// X on the paused page writes the spoiler log when let go, unless it was held for
			// the tracker (X, then Y)
			const bool xNow = (e.pads[0].buttons & SMS_PAD_X) != 0;
			if (!xNow && xHeld_) {
				if (!xUsed_ && state().paused && enabled() && !trackerOpen_) {
					writeSpoiler();
					spoilerShownAt_ = core().time();
				}
				xUsed_ = false;
			}
			xHeld_ = xNow;
			if (trackerInput(e.pads[0])) {
				xUsed_ = xUsed_ || xNow;
				lastY_ = lastZ_ = false;
				lastDpad_ = 0;
				return;
			}
			const bool y = (e.pads[0].buttons & SMS_PAD_Y) != 0;
			if (y && locked(Y_CAMERA) && !state().paused) { // paused, Y is the port's settings
				if (!lastY_)
					audio().play_system_sound(0x483D);
				e.pads[0].buttons &= (uint16_t)~SMS_PAD_Y;
			}
			lastY_ = y;
			const bool z = (e.pads[0].buttons & SMS_PAD_Z) != 0;
			if (z && !lastZ_ && canWarpToStart() && !state().paused && inWarpRing()) {
				e.pads[0].buttons &= (uint16_t)~SMS_PAD_Z;
				warpToStart();
			}
			lastZ_ = z;
			lastX_ = false;
			// The D-pad puts an unlocked second nozzle on FLUDD (up the hover, left the
			// rocket, right the turbo): a box of one already had cannot be the way out
			// of a place that needs another.
			const uint16_t dpad = e.pads[0].buttons & (SMS_PAD_UP | SMS_PAD_LEFT | SMS_PAD_RIGHT);
			const uint16_t pressed = dpad & (uint16_t)~lastDpad_;
			lastDpad_ = dpad;
			if (pressed && enabled() && hasNozzleApi()) { // also with every unlock from the start
				const int want = (pressed & SMS_PAD_UP) ? 4 : (pressed & SMS_PAD_LEFT) ? 1 : 5;
				const SmsPlayer m = mario();
				if (m.valid && m.has_fludd && m.current_nozzle != 3 && m.nozzle != want) {
					if (nozzleAllowed(want))
						player().set_nozzle(want, 1);
					else
						audio().play_system_sound(0x483D);
				}
			}
		});

		info("loaded; %s shows the unlocks", input().key_name(listKey_));
		return 0;
	}
};

SMS_MOD_CLASS(Randomizer)
