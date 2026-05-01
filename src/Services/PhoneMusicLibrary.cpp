#include "PhoneMusicLibrary.h"

// ---------------------------------------------------------------------------
// Frequency cheatsheet (rounded to the nearest Hz the piezo can resolve):
//   C3=131  D3=147  E3=165  F3=175  G3=196  A3=220  B3=247
//   C4=262  D4=294  E4=330  F4=349  G4=392  A4=440  B4=494
//   C5=523  D5=587  E5=659  F5=698  G5=784  A5=880  B5=988
//   C6=1046 D6=1175 E6=1319 F6=1397 G6=1568 A6=1760 B6=1976
// All ten tunes below are non-looping; PhoneMusicPlayer auto-advances to
// the next track when one ends.
// ---------------------------------------------------------------------------

// =====================================================================
// 0. Neon Drive — driving synthwave pulse-arp in A minor.
//   Pattern: low pulse + arpeggio sweep up + sweep back.
// =====================================================================
static const PhoneRingtoneEngine::Note kNeonDriveNotes[] = {
	{ 220, 110 }, { 220, 110 }, { 330, 110 }, { 440, 110 },
	{ 523, 110 }, { 659, 110 }, { 784, 110 }, { 880, 220 },
	{   0,  60 },
	{ 784, 110 }, { 659, 110 }, { 523, 110 }, { 440, 110 },
	{ 330, 110 }, { 220, 110 }, { 220, 220 },
	{   0, 100 },
	{ 247, 110 }, { 247, 110 }, { 349, 110 }, { 494, 110 },
	{ 587, 110 }, { 698, 110 }, { 880, 110 }, { 988, 220 },
	{   0,  60 },
	{ 880, 110 }, { 698, 110 }, { 587, 110 }, { 494, 110 },
	{ 349, 110 }, { 247, 110 }, { 220, 280 },
};
static const PhoneRingtoneEngine::Melody kNeonDrive = {
	kNeonDriveNotes,
	(uint16_t)(sizeof(kNeonDriveNotes) / sizeof(kNeonDriveNotes[0])),
	15,
	false,
	"Neon Drive"
};

// =====================================================================
// 1. Pixel Sunrise — bright C-major arpeggio over a walking bass line.
// =====================================================================
static const PhoneRingtoneEngine::Note kPixelSunriseNotes[] = {
	{ 262, 180 }, { 392, 180 }, { 523, 180 }, { 659, 360 },
	{   0,  80 },
	{ 294, 180 }, { 440, 180 }, { 587, 180 }, { 698, 360 },
	{   0,  80 },
	{ 330, 180 }, { 494, 180 }, { 659, 180 }, { 784, 360 },
	{   0,  80 },
	{ 392, 180 }, { 523, 180 }, { 784, 180 }, { 1046, 480 },
	{   0, 120 },
	{ 880, 180 }, { 784, 180 }, { 659, 180 }, { 523, 360 },
	{   0,  80 },
	{ 392, 180 }, { 330, 180 }, { 262, 480 },
};
static const PhoneRingtoneEngine::Melody kPixelSunrise = {
	kPixelSunriseNotes,
	(uint16_t)(sizeof(kPixelSunriseNotes) / sizeof(kPixelSunriseNotes[0])),
	15,
	false,
	"Pixel Sunrise"
};

// =====================================================================
// 2. Cyber Dawn — moody D-minor melody, slow tempo.
// =====================================================================
static const PhoneRingtoneEngine::Note kCyberDawnNotes[] = {
	{ 294, 260 }, { 349, 260 }, { 440, 520 },
	{   0,  80 },
	{ 392, 260 }, { 349, 260 }, { 294, 520 },
	{   0,  80 },
	{ 220, 260 }, { 262, 260 }, { 349, 520 },
	{   0,  80 },
	{ 392, 260 }, { 440, 260 }, { 523, 520 },
	{   0,  80 },
	{ 587, 260 }, { 523, 260 }, { 440, 260 }, { 349, 260 },
	{ 294, 520 },
	{   0, 200 },
	{ 220, 780 },
};
static const PhoneRingtoneEngine::Melody kCyberDawn = {
	kCyberDawnNotes,
	(uint16_t)(sizeof(kCyberDawnNotes) / sizeof(kCyberDawnNotes[0])),
	20,
	false,
	"Cyber Dawn"
};

// =====================================================================
// 3. Crystal Cave — haunting A-minor pentatonic phrase with a tail echo.
// =====================================================================
static const PhoneRingtoneEngine::Note kCrystalCaveNotes[] = {
	{ 440, 200 }, { 523, 200 }, { 659, 200 }, { 880, 600 },
	{   0, 100 },
	{ 784, 200 }, { 659, 200 }, { 523, 200 }, { 440, 600 },
	{   0, 100 },
	{ 392, 200 }, { 523, 200 }, { 659, 200 }, { 784, 600 },
	{   0, 100 },
	{ 880, 200 }, { 1046, 200 }, { 1319, 600 },
	{   0, 200 },
	{ 880, 200 }, { 659, 200 }, { 440, 800 },
};
static const PhoneRingtoneEngine::Melody kCrystalCave = {
	kCrystalCaveNotes,
	(uint16_t)(sizeof(kCrystalCaveNotes) / sizeof(kCrystalCaveNotes[0])),
	18,
	false,
	"Crystal Cave"
};

// =====================================================================
// 4. Hyperloop — fast techno bassline punctuated by high pings.
// =====================================================================
static const PhoneRingtoneEngine::Note kHyperloopNotes[] = {
	{ 220,  90 }, { 220,  90 }, { 1319,  60 }, { 220,  90 },
	{ 220,  90 }, { 220,  90 }, { 1319,  60 }, { 220,  90 },
	{ 247,  90 }, { 247,  90 }, { 1568,  60 }, { 247,  90 },
	{ 247,  90 }, { 247,  90 }, { 1568,  60 }, { 247,  90 },
	{ 196,  90 }, { 196,  90 }, { 1175,  60 }, { 196,  90 },
	{ 196,  90 }, { 196,  90 }, { 1175,  60 }, { 196,  90 },
	{ 220,  90 }, { 247,  90 }, { 294,  90 }, { 330,  90 },
	{ 349,  90 }, { 392,  90 }, { 440,  90 }, { 494,  90 },
	{ 523, 360 },
	{   0, 120 },
};
static const PhoneRingtoneEngine::Melody kHyperloop = {
	kHyperloopNotes,
	(uint16_t)(sizeof(kHyperloopNotes) / sizeof(kHyperloopNotes[0])),
	10,
	false,
	"Hyperloop"
};

// =====================================================================
// 5. Starfall — descending major scale exploration with a final spark.
// =====================================================================
static const PhoneRingtoneEngine::Note kStarfallNotes[] = {
	{ 1319, 160 }, { 1175, 160 }, { 1046, 160 }, { 988, 160 },
	{  880, 160 }, {  784, 160 }, {  698, 160 }, { 659, 160 },
	{  587, 160 }, {  523, 160 }, {  494, 160 }, { 440, 160 },
	{  392, 160 }, {  349, 160 }, {  330, 160 }, { 294, 320 },
	{   0, 100 },
	{ 1046, 200 }, { 1319, 200 }, { 1760, 600 },
	{   0, 200 },
	{  880, 200 }, { 1046, 200 }, { 1319, 600 },
};
static const PhoneRingtoneEngine::Melody kStarfall = {
	kStarfallNotes,
	(uint16_t)(sizeof(kStarfallNotes) / sizeof(kStarfallNotes[0])),
	15,
	false,
	"Starfall"
};

// =====================================================================
// 6. Retro Quest — JRPG-flavoured adventure march.
// =====================================================================
static const PhoneRingtoneEngine::Note kRetroQuestNotes[] = {
	{ 392, 180 }, { 392, 180 }, { 523, 180 }, { 659, 360 },
	{ 587, 180 }, { 523, 180 }, { 392, 360 },
	{   0,  80 },
	{ 440, 180 }, { 440, 180 }, { 587, 180 }, { 698, 360 },
	{ 659, 180 }, { 587, 180 }, { 440, 360 },
	{   0,  80 },
	{ 494, 180 }, { 587, 180 }, { 698, 180 }, { 784, 360 },
	{ 698, 180 }, { 659, 180 }, { 587, 360 },
	{   0,  80 },
	{ 523, 180 }, { 659, 180 }, { 784, 180 }, { 1046, 540 },
	{   0, 200 },
};
static const PhoneRingtoneEngine::Melody kRetroQuest = {
	kRetroQuestNotes,
	(uint16_t)(sizeof(kRetroQuestNotes) / sizeof(kRetroQuestNotes[0])),
	18,
	false,
	"Retro Quest"
};

// =====================================================================
// 7. Moonlit Drift — slow ballad of wide leaps.
// =====================================================================
static const PhoneRingtoneEngine::Note kMoonlitDriftNotes[] = {
	{ 262, 320 }, { 523, 320 }, { 392, 480 },
	{   0,  80 },
	{ 294, 320 }, { 587, 320 }, { 440, 480 },
	{   0,  80 },
	{ 330, 320 }, { 659, 320 }, { 494, 480 },
	{   0,  80 },
	{ 349, 320 }, { 698, 320 }, { 523, 480 },
	{   0, 100 },
	{ 880, 320 }, { 784, 320 }, { 659, 320 }, { 523, 320 },
	{ 440, 480 },
	{   0, 200 },
	{ 262, 800 },
};
static const PhoneRingtoneEngine::Melody kMoonlitDrift = {
	kMoonlitDriftNotes,
	(uint16_t)(sizeof(kMoonlitDriftNotes) / sizeof(kMoonlitDriftNotes[0])),
	22,
	false,
	"Moonlit Drift"
};

// =====================================================================
// 8. Arcade Hero — fast chiptune flourish.
// =====================================================================
static const PhoneRingtoneEngine::Note kArcadeHeroNotes[] = {
	{ 523, 100 }, { 659, 100 }, { 784, 100 }, { 1046, 200 },
	{ 988, 100 }, { 880, 100 }, { 784, 100 }, { 659, 200 },
	{ 587, 100 }, { 698, 100 }, { 880, 100 }, { 1175, 200 },
	{ 1046, 100 }, { 988, 100 }, { 880, 100 }, { 784, 200 },
	{   0,  80 },
	{ 1046, 100 }, { 1175, 100 }, { 1319, 100 }, { 1568, 300 },
	{   0,  60 },
	{ 1319, 100 }, { 1046, 100 }, {  784, 100 }, {  523, 300 },
	{   0, 100 },
	{ 1046, 600 },
};
static const PhoneRingtoneEngine::Melody kArcadeHero = {
	kArcadeHeroNotes,
	(uint16_t)(sizeof(kArcadeHeroNotes) / sizeof(kArcadeHeroNotes[0])),
	10,
	false,
	"Arcade Hero"
};

// =====================================================================
// 9. Sunset Boulevard — long synthwave groove. The "credits roll" of
// the catalogue: longer than the others, with a clear A-B-A shape.
// =====================================================================
static const PhoneRingtoneEngine::Note kSunsetBlvdNotes[] = {
	// A theme — slow ascending pulse
	{ 220, 200 }, { 330, 200 }, { 440, 200 }, { 523, 400 },
	{ 440, 200 }, { 330, 200 }, { 220, 400 },
	{   0, 120 },
	{ 247, 200 }, { 349, 200 }, { 494, 200 }, { 587, 400 },
	{ 494, 200 }, { 349, 200 }, { 247, 400 },
	{   0, 160 },
	// B theme — bright high pads
	{ 659, 240 }, { 784, 240 }, { 880, 240 }, { 1046, 480 },
	{ 988, 240 }, { 880, 240 }, { 784, 480 },
	{   0, 100 },
	{ 698, 240 }, { 880, 240 }, { 988, 240 }, { 1175, 480 },
	{ 1046, 240 }, { 880, 240 }, { 698, 480 },
	{   0, 160 },
	// A reprise — fade-out
	{ 220, 200 }, { 330, 200 }, { 440, 200 }, { 523, 400 },
	{ 440, 200 }, { 330, 200 }, { 220, 800 },
};
static const PhoneRingtoneEngine::Melody kSunsetBlvd = {
	kSunsetBlvdNotes,
	(uint16_t)(sizeof(kSunsetBlvdNotes) / sizeof(kSunsetBlvdNotes[0])),
	18,
	false,
	"Sunset Blvd"
};

// =====================================================================
// Registry — fixed order matches the Id enum.
// =====================================================================
static const PhoneRingtoneEngine::Melody* const kAll[] = {
	&kNeonDrive,
	&kPixelSunrise,
	&kCyberDawn,
	&kCrystalCave,
	&kHyperloop,
	&kStarfall,
	&kRetroQuest,
	&kMoonlitDrift,
	&kArcadeHero,
	&kSunsetBlvd,
};

uint8_t PhoneMusicLibrary::count(){
	return (uint8_t) PhoneMusicLibrary::Count;
}

const char* PhoneMusicLibrary::nameOf(Id id){
	switch(id){
		case NeonDrive:        return "Neon Drive";
		case PixelSunrise:     return "Pixel Sunrise";
		case CyberDawn:        return "Cyber Dawn";
		case CrystalCave:      return "Crystal Cave";
		case Hyperloop:        return "Hyperloop";
		case Starfall:         return "Starfall";
		case RetroQuest:       return "Retro Quest";
		case MoonlitDrift:     return "Moonlit Drift";
		case ArcadeHero:       return "Arcade Hero";
		case SunsetBoulevard:  return "Sunset Blvd";
		default:               return "Unknown";
	}
}

const PhoneRingtoneEngine::Melody& PhoneMusicLibrary::get(Id id){
	uint8_t i = (uint8_t) id;
	if(i >= (uint8_t) PhoneMusicLibrary::Count) i = 0;
	return *kAll[i];
}

const PhoneRingtoneEngine::Melody& PhoneMusicLibrary::byIndex(uint8_t idx){
	if(idx >= (uint8_t) PhoneMusicLibrary::Count){
		idx = (uint8_t)(idx % (uint8_t) PhoneMusicLibrary::Count);
	}
	return *kAll[idx];
}

const PhoneRingtoneEngine::Melody* const* PhoneMusicLibrary::tracks(){
	// Mirrors the `kAll` registry but exposed as the const-pointer-of-
	// const-pointer-to-Melody contract that PhoneMusicPlayer::setTracks()
	// expects. Using kAll directly would require a reinterpret_cast
	// because LVGL's Melody* const* type is not implicitly convertible
	// from a static array of Melody* const, so we re-publish the same
	// pointers via this accessor.
	return kAll;
}
