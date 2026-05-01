#include "Game.h"

#include <utility>
#include <Loop/LoopManager.h>
#include <Chatter.h>
#include "../../InputChatter.h"
#include "../../FSLVGL.h"

extern bool gameStarted;
extern Game* startedGame;

// S65: hostScreen is now an `LVScreen*` (was `GamesScreen*`). Any LVScreen
// can host a Game - the engine just restarts that screen on game pop. The
// legacy GamesScreen list-view and the new PhoneGamesScreen retro grid are
// both valid hosts because both inherit LVScreen and both implement start().
Game::Game(LVScreen* hostScreen, const char* root, std::vector<ResDescriptor> resources) : resMan(root), resources(std::move(resources)), hostScreen(hostScreen),
loadTask("loadTask", [](Task* t){
	 auto game = (Game*) t->arg;
	 game->loadFunc();
}, 4096, this), render(this, Chatter.getDisplay()->getBaseSprite()), collision(this){

}

Game::~Game(){
	Audio.stop();
}

void Game::load(){
	if(loaded || loadTask.running) return;

	loadTask.start(0, 0);
}

void Game::loadFunc(){
	resMan.load(resources);
	onLoad();
	loaded = true;
}

void Game::start(){
	if(started) return;

	if(!loaded){
		ESP_LOGE("Game", "Attempting to load game that wasn't loaded.");
		return;
	}

	started = true;
	LoopManager::addListener(this);

	gameStarted = true;
	startedGame = this;
	Input::getInstance()->removeListener(InputChatter::getInputInstance());

	onStart();
}

void Game::stop(){
	if(!started) return;
	onStop();

	started = false;
	LoopManager::removeListener(this);

	gameStarted = false;
	startedGame = nullptr;
	Input::getInstance()->addListener(InputChatter::getInputInstance());
}

bool Game::isLoaded() const{
	return loaded;
}

File Game::getFile(std::string path){
	return resMan.getResource(std::move(path));
}

void Game::addObject(std::shared_ptr<GameObject> obj){
	objects.insert(obj);
}

void Game::removeObject(std::shared_ptr<GameObject> obj){
	collision.removeObject(*obj);
	objects.erase(obj);
}

void Game::loop(uint micros){
	if(popped) goto poppedLabel;

	collision.update(micros);
	onLoop((float)micros / 1000000.0f);
	if(popped) goto poppedLabel;

	render.update(micros);
	onRender(Chatter.getDisplay()->getBaseSprite());
	if(popped) goto poppedLabel;

	// collision.drawDebug(CircuitPet.getDisplay()->getBaseSprite());

	Chatter.getDisplay()->commit();

	return;

	poppedLabel:

	stop();
	// Snapshot the host pointer because `delete this` invalidates it.
	// `volatile` here just mirrors the original code's defensive style; both
	// `LVScreen*` and `GamesScreen*` are valid receivers for `start()`.
	volatile auto host = hostScreen;
	delete this;
	FSLVGL::loadCache();
	const_cast<LVScreen*>(host)->start();
}

void Game::onStart(){ }

void Game::onStop(){ }

void Game::onLoad(){ }

void Game::onLoop(float deltaTime){ }

void Game::onRender(Sprite* canvas){ }

void Game::pop(){
	popped = true;
}

LVScreen* Game::getHostScreen(){
	return hostScreen;
}
