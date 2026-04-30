#include "Storage.h"

Repositories Storage = {
		MessageRepo("/Repo/Msg"),
		ConvoRepo("/Repo/Convo", 12),
		Repo<Friend>("/Repo/Friends", 12),
		Repo<PhoneContact>("/Repo/Contacts", 12)
};

void Repositories::begin(){
	Messages.begin();
	Convos.begin(true);
	Friends.begin(true);
	PhoneContacts.begin(true);
}
