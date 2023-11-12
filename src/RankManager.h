#pragma once

#include "IRankManager.h"

class CRankManager : public CBaseManager<IRankManager>
{
public:
	CRankManager();

	bool OnRankPacket(CReceivePacket* msg, IExtendedSocket* socket);
	
private:
	bool OnRankInfoRequest(CReceivePacket* msg, IUser* user);
	bool OnRankInRoomRequest(CReceivePacket* msg, IUser* user);
	bool OnRankSearchNicknameRequest(CReceivePacket* msg, IUser* user);
	bool OnRankLeagueRequest(CReceivePacket* msg, IUser* user);
	bool OnRankUserInfoRequest(CReceivePacket* msg, IUser* user);
};