#include "PhoneT9Vocab.h"
#include <ctype.h>

namespace {

// ===========================================================================
// Five-letter pool (exactly 5 uppercase A..Z per entry).
// ---------------------------------------------------------------------------
// This is the pool `PhoneWordle::pickWord()` draws from. The first 90
// entries are the original seed list shipped in S96 -- they're kept
// here verbatim so a roll on the new pool is a strict superset of the
// old behaviour. The remaining ~110 entries were added in S175 to
// roughly double the pool. All entries are common, family-friendly
// English five-letter words.
// ===========================================================================
static const char* kFiveLetter[] = {
		// --- S96 seed (90) ---
		"APPLE", "BREAD", "CHAIR", "DREAM", "EAGLE",
		"FROST", "GIANT", "HONEY", "IGLOO", "JOLLY",
		"KNIFE", "LEMON", "MUSIC", "NIGHT", "OCEAN",
		"PIANO", "QUIET", "RIVER", "SUGAR", "TIGER",
		"UNCLE", "VOICE", "WATER", "YOUTH", "ZEBRA",
		"BRAVE", "CLOUD", "DANCE", "EMBER", "FLAME",
		"GLOBE", "HORSE", "INDEX", "KAYAK", "LIGHT",
		"MAGIC", "NORTH", "OASIS", "PIXEL", "QUEEN",
		"ROBOT", "SMILE", "TRAIN", "ULTRA", "VIVID",
		"WHEEL", "YACHT", "BEACH", "CANDY", "DEPTH",
		"RETRO", "SPARK", "SUNNY", "VAPOR", "BRICK",
		"CLOCK", "DRIVE", "FRESH", "GLARE", "JEWEL",
		"MINER", "PAINT", "QUEST", "SHINE", "TOWER",
		"WORLD", "ZESTY", "AMBER", "BERRY", "CABIN",
		"DIZZY", "ECHOY", "FANCY", "GLOOM", "HOTEL",
		"IVORY", "JOKER", "LATCH", "MERRY", "NOVEL",
		"OLIVE", "PEARL", "RAVEN", "SOLAR", "TONIC",
		"WINDY", "BLAZE", "CRAFT", "DAILY", "FLOAT",

		// --- S175 expansion (110) ---
		"ABBEY", "ANGEL", "ARROW", "BACON", "BADGE",
		"BAKER", "BENCH", "BIRTH", "BLADE", "BLAST",
		"BLISS", "BLOOM", "BLUSH", "BONUS", "BRAIN",
		"BRAND", "BRASS", "BRIDE", "BROOK", "BRUSH",
		"BUILD", "BURST", "CABLE", "CARGO", "CARRY",
		"CHALK", "CHARM", "CHASE", "CHESS", "CHILD",
		"CLAIM", "CLASH", "CLEAN", "CLEAR", "CLIFF",
		"CLIMB", "CLOSE", "CLOVE", "CRANE", "CRASH",
		"CREAM", "CREEP", "CREST", "CROWN", "CRUSH",
		"CURVE", "DAISY", "DEBUT", "DELTA", "DENIM",
		"DIARY", "DODGE", "DOUGH", "DOZEN", "DRAFT",
		"DRAIN", "DRESS", "DRIFT", "DRINK", "EARTH",
		"EBONY", "ELBOW", "ENJOY", "ENTRY", "ENVOY",
		"EPOCH", "EVENT", "EVERY", "EVOKE", "EXACT",
		"EXILE", "EXIST", "EXTRA", "FABLE", "FAITH",
		"FATAL", "FAULT", "FAVOR", "FEAST", "FETCH",
		"FEVER", "FIBER", "FIELD", "FIFTH", "FIFTY",
		"FIGHT", "FIRST", "FIXED", "FLEET", "FLINT",
		"FLOCK", "FLOOR", "FLOUR", "FLUID", "FOCAL",
		"FOCUS", "FORCE", "FORGE", "FORTH", "FOUND",
		"FRAME", "FRANK", "FROCK", "FRUIT", "FUNNY",
		"GHOST", "GIDDY", "GLASS", "GLAZE", "GLEAM"
};

constexpr uint16_t kFiveLetterCount =
		sizeof(kFiveLetter) / sizeof(kFiveLetter[0]);

// ===========================================================================
// Mixed-length pool (5..7 uppercase A..Z per entry).
// ---------------------------------------------------------------------------
// This is the pool `PhoneHangman::pickWord()` draws from. The first 60
// entries are the original seed list shipped in S87. The remaining
// ~80 entries were added in S175 to roughly double the pool and to
// shift the length distribution toward the harder 6 / 7-letter words
// the gallows clearly accommodates (its visible reveal supports up to
// `MaxWordLen` = 7).
// ===========================================================================
static const char* kMixed[] = {
		// --- S87 seed (60) ---
		"APPLE", "BREAD", "CHAIR", "DREAM", "EAGLE",
		"FROST", "GIANT", "HONEY", "IGLOO", "JOLLY",
		"KNIFE", "LEMON", "MUSIC", "NIGHT", "OCEAN",
		"PIANO", "QUIET", "RIVER", "SUGAR", "TIGER",
		"UNCLE", "VOICE", "WATER", "YOUTH", "ZEBRA",
		"BRAVE", "CLOUD", "DANCE", "EMBER", "FLAME",
		"GLOBE", "HORSE", "INDEX", "KAYAK", "LIGHT",
		"MAGIC", "NORTH", "OASIS", "PIXEL", "QUEEN",
		"ROBOT", "SMILE", "TRAIN", "ULTRA", "VIVID",
		"WHEEL", "YACHT", "BEACH", "CANDY", "DEPTH",
		"PLANET", "RETRO", "SPARK", "SUNNY", "TURTLE",
		"VAPOR", "WONDER", "BRIDGE", "CASTLE", "DOLPHIN",

		// --- S175 expansion: 6-letter (40) ---
		"ANCHOR", "ANIMAL", "ARMORY", "AUTUMN", "BANDIT",
		"BANNER", "BARREL", "BASKET", "BATTLE", "BEAVER",
		"BEHAVE", "BRANCH", "BRONZE", "BUTTON", "CACTUS",
		"CAMERA", "CANVAS", "CARROT", "CHEESE", "CHERRY",
		"CHISEL", "CIRCUS", "COMBAT", "COMEDY", "COPPER",
		"COTTON", "COWBOY", "CRAYON", "CREDIT", "DAGGER",
		"DAMAGE", "DANGER", "DECADE", "DEGREE", "DESERT",
		"DESIGN", "DETAIL", "DOCTOR", "DRAGON", "DRIVER",

		// --- S175 expansion: 7-letter (30) ---
		"ABILITY", "ANCIENT", "BALANCE", "BALLOON", "BANQUET",
		"BATTERY", "BEDROOM", "BIOLOGY", "BLANKET", "CABINET",
		"CAPITAL", "CAPTAIN", "CARAVAN", "CARTOON", "CHICKEN",
		"CIRCUIT", "COMPANY", "COMPASS", "CONCEPT", "CONCERT",
		"CONTACT", "COTTAGE", "COUNTER", "CRACKER", "CRYSTAL",
		"CULTURE", "CURTAIN", "DENTIST", "DESTINY", "DIAMOND"
};

constexpr uint16_t kMixedCount = sizeof(kMixed) / sizeof(kMixed[0]);

}  // namespace

uint16_t PhoneT9Vocab::fiveLetterCount(){
	return kFiveLetterCount;
}

const char* PhoneT9Vocab::fiveLetter(uint16_t idx){
	if(idx >= kFiveLetterCount) return nullptr;
	return kFiveLetter[idx];
}

uint16_t PhoneT9Vocab::mixedCount(){
	return kMixedCount;
}

const char* PhoneT9Vocab::mixed(uint16_t idx){
	if(idx >= kMixedCount) return nullptr;
	return kMixed[idx];
}

uint8_t PhoneT9Vocab::digitsForLetter(char letter){
	const char c = (letter >= 'A' && letter <= 'Z')
			? (char) (letter - 'A' + 'a')
			: letter;
	switch(c){
		case 'a': case 'b': case 'c': return 2;
		case 'd': case 'e': case 'f': return 3;
		case 'g': case 'h': case 'i': return 4;
		case 'j': case 'k': case 'l': return 5;
		case 'm': case 'n': case 'o': return 6;
		case 'p': case 'q': case 'r': case 's': return 7;
		case 't': case 'u': case 'v': return 8;
		case 'w': case 'x': case 'y': case 'z': return 9;
		default: return 0;
	}
}
