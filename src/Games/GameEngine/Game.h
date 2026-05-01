#ifndef CIRCUITPET_FIRMWARE_GAME_H
#define CIRCUITPET_FIRMWARE_GAME_H

#include "ResourceManager.h"
#include "GameObject.h"
#include <Util/Task.h>
#include <set>
#include <vector>
#include "GameSystem.h"
#include "Collision/CollisionSystem.h"
#include "Rendering/RenderSystem.h"
#include "../../Interface/LVScreen.h"
#include <Loop/LoopListener.h>
#include <Audio/ChirpSystem.h>

// S65 (MAKERphone): the games engine no longer hard-couples to the legacy
// GamesScreen. Any LVScreen can host a Game now - on game pop, the engine
// just calls `host->start()` to bring the host screen back. This is what
// lets the new PhoneGamesScreen serve as a phone-styled launcher without
// us needing to subclass / fake the legacy list-screen.
class Game : private LoopListener {
friend GameSystem;
public:
	virtual ~Game();

	void load();
	bool isLoaded() const;

	void start();
	void stop();
	void pop();

	/**
	 * The screen that pushed us, which the engine `start()`s again when the
	 * game pops. Either the legacy GamesScreen list view or the new
	 * PhoneGamesScreen retro grid - the engine does not care.
	 */
	LVScreen* getHostScreen();

	// Legacy alias kept so older call sites (and any downstream code that
	// has not been migrated yet) still compile cleanly. Returns the same
	// pointer as getHostScreen(); callers that need a true GamesScreen*
	// will need to dynamic_cast<GamesScreen*>(...) themselves.
	LVScreen* getGamesScreen() { return getHostScreen(); }

protected:
	Game(LVScreen* hostScreen, const char* root, std::vector<ResDescriptor> resources);

	virtual void onStart();
	virtual void onStop();
	virtual void onLoad();
	virtual void onLoop(float deltaTime);
	virtual void onRender(Sprite* canvas);

	File getFile(std::string path);

	void addObject(std::shared_ptr<GameObject> obj);
	void removeObject(std::shared_ptr<GameObject> obj);

	CollisionSystem collision;

	ChirpSystem Audio;

private:
	ResourceManager resMan;
	const std::vector<ResDescriptor> resources;

	bool loaded = false;
	Task loadTask;

	volatile bool popped = false;
	bool started = false;

	RenderSystem render;

	std::set<std::shared_ptr<GameObject>> objects;

	void loop(uint micros) final;
	void loadFunc();

	// Host screen the engine restarts when the game pops. May be either
	// the legacy GamesScreen list or the new phone-styled PhoneGamesScreen
	// grid - both are LVScreens, both have a `start()` method, both look
	// identical from this engine's perspective.
	LVScreen* hostScreen;
};


#endif //CIRCUITPET_FIRMWARE_GAME_H
