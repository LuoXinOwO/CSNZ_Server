#include "serverinstance.h"

#include "manager/packetmanager.h"
#include "manager/usermanager.h"
#include "manager/userdatabase.h"
#include "manager/channelmanager.h"
#include "manager/itemmanager.h"
#include "manager/shopmanager.h"
#include "manager/luckyitemmanager.h"
#include "manager/hostmanager.h"
#include "manager/dedicatedservermanager.h"
#include "manager/questmanager.h"
#include "manager/minigamemanager.h"
#include "manager/clanmanager.h"
#include "manager/rankmanager.h"

#include "net/receivepacket.h"
#include "common/buildnum.h"
#include "common/net/netdefs.h"
#include "common/utils.h"

#include "csvtable.h"
#include "serverconfig.h"
#include "servercommands.h"
#ifdef USE_GUI
#include "gui/igui.h"
#endif

using namespace std;

CServerConfig* g_pServerConfig;
CCSVTable* g_pItemTable;
CCSVTable* g_pMapListTable;
CCSVTable* g_pGameModeListTable;

CServerInstance::CServerInstance()
{
	m_bIsServerActive = false;
	m_CurrentTime = 0;
	m_pCurrentLocalTime = NULL;
	m_nUptime = 0;

	m_TCPServer.SetCriticalSection(&g_ServerCriticalSection);
	m_TCPServer.SetListener(this);
	m_UDPServer.SetCriticalSection(&g_ServerCriticalSection);
	m_UDPServer.SetListener(this);
}

CServerInstance::~CServerInstance()
{
	Manager().ShutdownAll();

	delete g_pItemTable;
	delete g_pServerConfig;
}

bool CServerInstance::Init()
{
	if (m_bIsServerActive)
		return true;

	if (!LoadConfigs())
	{
		Console().Error("Server initialization failed.\n");
		m_bIsServerActive = false;
		return false;
	}

	g_pItemTable = new CCSVTable("Data/Item.csv", rapidcsv::LabelParams(0, 0), rapidcsv::SeparatorParams(), rapidcsv::ConverterParams(true), rapidcsv::LineReaderParams(), true);
	g_pMapListTable = new CCSVTable("Data/MapList.csv", rapidcsv::LabelParams(0, 0), rapidcsv::SeparatorParams(), rapidcsv::ConverterParams(true), rapidcsv::LineReaderParams(), true);
	g_pGameModeListTable = new CCSVTable("Data/GameModeList.csv", rapidcsv::LabelParams(0, 0), rapidcsv::SeparatorParams(), rapidcsv::ConverterParams(true), rapidcsv::LineReaderParams(), true);

	if (!Manager().InitAll() ||
		!m_TCPServer.Start(g_pServerConfig->tcpPort, g_pServerConfig->tcpSendBufferSize) ||
		!m_UDPServer.Start(g_pServerConfig->udpPort))
	{
		Console().Error("Server initialization failed.\n");
		m_bIsServerActive = false;
		return false;
	}
	else if (g_pItemTable->IsLoadFailed())
	{
		Console().Error("Server initialization failed. Couldn't load Item.csv.\n");
		m_bIsServerActive = false;
		return false;
	}
	else if (g_pMapListTable->IsLoadFailed())
	{
		Console().Error("Server initialization failed. Couldn't load MapList.csv.\n");
		m_bIsServerActive = false;
		return false;
	}
	else if (g_pGameModeListTable->IsLoadFailed())
	{
		Console().Error("Server initialization failed. Couldn't load GameModeList.csv.\n");
		m_bIsServerActive = false;
		return false;
	}

	Console().Log("Server starts listening. Server developers: Jusic, Hardee, NekoMeow. Thx to Ochii for CSO2 server.\nFor more information visit discord.gg/EvUAY6D\n");
	Console().Log("Server build: %s, %s\n", build_number(),
#ifdef PUBLIC_RELEASE
		"Public Release");
#else
		"Private Release");
#endif

	m_bIsServerActive = true;

	/// @fixme: explanation why we call this
	OnSecondTick();

	return true;
}

bool CServerInstance::Reload()
{
	// reinit all managers and server config without shutting down the server
	// use case: you updated config data and want to apply it without shutting down the server (it can be dangerous)

	// reload server config
	if (g_pServerConfig)
	{
		UnloadConfigs();
		if (!LoadConfigs())
		{
			return false;
		}
	}

	Manager().ShutdownAll();

	if (!Manager().InitAll())
		return false;

	return true;
}

bool CServerInstance::LoadConfigs()
{
	g_pServerConfig = new CServerConfig();
	return g_pServerConfig->Load();
}

void CServerInstance::UnloadConfigs()
{
	delete g_pServerConfig;
}

void CServerInstance::OnTCPConnectionCreated(IExtendedSocket* socket)
{
	if (g_UserDatabase.IsIPBanned(socket->GetIP()))
	{
		Console().Log("Client (%d, %s) disconnected from the server due to banned ip\n", socket->GetID(), socket->GetIP().c_str());
		DisconnectClient(socket);

		// return false;
	}

	// return true;
}

void CServerInstance::OnTCPConnectionClosed(IExtendedSocket* socket)
{
	int bytesSent = socket->GetBytesSent();
	int bytesReceived = socket->GetBytesReceived();
	int sock = socket->GetSocket();

	// clean up user
	IUser* user = g_UserManager.GetUserBySocket(socket);
	int userID = 0;
	string userName = "NULL";
	if (user)
	{
		userID = user->GetID();
		userName = user->GetUsername();
		g_UserManager.RemoveUser(user);

		Console().Log("User logged out (%d, '%s', 0x%X)\n", userID, userName.c_str(), user);
	}
	else
	{
		g_DedicatedServerManager.RemoveServer(socket);
	}

	/// @todo remove all events referred to deleted socket object
}

void CServerInstance::OnTCPMessage(IExtendedSocket* socket, CReceivePacket* msg)
{
	g_Event.AddEventPacket(socket, msg);
}

void CServerInstance::OnTCPError(int errorCode)
{
	//g_ChannelManager.EndAllGames();
	//SetServerActive(false);
}

void CServerInstance::OnUDPMessage(Buffer& buf, unsigned short port)
{
	// 7, 14 is well known
	// 14 = type 0 (Punch)
	// 7 = type 1 (HeartBeat)
	if (buf.getBuffer().size() < 7)
	{
		Console().Error("[CServerInstance::OnUDPMessage] invalid packet???\n");
		return;
	}

	char signature = buf.readUInt8();
	if (signature != UDP_HOLEPUNCH_PACKET_SIGNATURE_1)
	{
		Console().Log(OBFUSCATE("[CServerInstance::OnUDPMessage] signature error\n"));
		return;
	}

	int userID = buf.readUInt32_LE();
	int type = buf.readUInt8();

	IUser* user = g_UserManager.GetUserById(userID);
	if (!user)
	{
		Console().Log(OBFUSCATE("[CServerInstance::OnUDPMessage] User '%d' Sent UDP Packet but not inside g_UserManager\n"), userID);
		return;
	}

	if (type == 0) {
		int portID = buf.readUInt8();
		int localAddr = ~buf.readUInt32_BE(); // TODO: Fix this...
		string localIpAddress = ip_to_string(localAddr);
		int localPort = buf.readUInt16_LE();
		int tries = buf.readUInt8();

		Console().Log("OnUDPMessage(0) - userID: %d, portID: %d, localAddr: %d (%s), localPort: %d, tries: %d\n", userID, portID, localAddr, localIpAddress.c_str(), localPort, tries);

		if (user->UpdateHolepunch(portID, localIpAddress, localPort, port) == -1)
		{
			Console().Warn("UpdateHolepunch Failed: %d, %d, %d\n", portID, localPort, port);
		}

		Buffer replyBuffer;
		replyBuffer.writeUInt8('W');
		replyBuffer.writeUInt8(0);
		replyBuffer.writeUInt8(1);
		m_UDPServer.SendTo(replyBuffer);
	}
	else if (type == 1) {
		int tries = buf.readUInt8();

		Console().Log("OnUDPMessage(1) - userID: %d, tries: %d\n", userID, tries);
	}
}

void CServerInstance::OnUDPError(int errorCode)
{
}

void CServerInstance::OnCommand(const string& command)
{
	Console().Log("Command: %s\n", command.c_str());

	istringstream iss(command);
	vector<string> args((istream_iterator<string>(iss)), istream_iterator<string>());

	if (args.empty())
		return;

	CCommand* cmd = CmdList().GetCommand(args[0]);
	if (cmd)
	{
		cmd->Exec(args);
	}
}

void* EventThread(void*)
{
	while (g_pServerInstance->IsServerActive())
	{
		g_Event.WaitForSignal();

		if (g_pServerInstance->IsServerActive())
			g_pServerInstance->OnEvent();
	}
	
	return NULL;
}

void* ReadConsoleThread(void*)
{
	while (g_pServerInstance->IsServerActive())
	{
		string cmd;
		getline(cin, cmd);

		// TODO: ignore empty line?

		g_Event.AddEventConsoleCommand(cmd);
	}
	
	return NULL;
}

void CServerInstance::SetServerActive(bool active)
{
	m_bIsServerActive = active;

	if (!m_bIsServerActive)
	{
		// wake up event thread
		g_Event.Signal();
	}
}

bool CServerInstance::IsServerActive()
{
	return m_bIsServerActive;
}

void CServerInstance::OnEvent()
{
	/// @todo
	bool empty;
	Event_s ev = g_Event.GetNextEvent(empty);
	while (!empty)
	{
		g_ServerCriticalSection.Enter();

		switch (ev.type)
		{
		case SERVER_EVENT_CONSOLE_COMMAND:
			OnCommand(ev.cmd);
			break;
		case SERVER_EVENT_TCP_PACKET:
			OnPackets(ev.socket, ev.msg);
			break;
		case SERVER_EVENT_SECOND_TICK:
			OnSecondTick();
			break;
		case SERVER_EVENT_FUNCTION:
			OnFunction(ev.func);
			break; 
		}

		if (ev.type == SERVER_EVENT_TCP_PACKET)
		{
			delete ev.msg;
		}

		g_ServerCriticalSection.Leave();

		ev = g_Event.GetNextEvent(empty);
	}
}

void CServerInstance::OnPackets(IExtendedSocket* s, CReceivePacket* msg)
{
	/// @todo don't use pointer to deleted object
	if (find(m_TCPServer.GetClients().begin(), m_TCPServer.GetClients().end(), s) == m_TCPServer.GetClients().end())
	{
		// skip packets with deleted socket object
		return;
	}

	switch (msg->GetID())
	{
	case PacketId::Version:
		g_UserManager.OnVersionPacket(msg, s);
		break;
	case PacketId::CreateCharacter:
		g_UserManager.OnCharacterPacket(msg, s);
		break;
	case PacketId::Login:
		g_UserManager.OnLoginPacket(msg, s);
		break;
	case PacketId::RequestServerList:
		g_ChannelManager.OnChannelListPacket(s);
		break;
	case PacketId::RequestTransfer:
		g_ChannelManager.OnRoomListPacket(msg, s);
		break;
	case PacketId::RecvCrypt:
		g_UserManager.OnCryptPacket(msg, s);
		break;
	case PacketId::Room:
		g_ChannelManager.OnRoomRequest(msg, s);
		break;
	case PacketId::Shop:
		g_ShopManager.OnShopPacket(msg, s);
		break;
	case PacketId::UMsg:
		g_UserManager.OnUserMessage(msg, s);
		break;
	case PacketId::Host:
		g_HostManager.OnPacket(msg, s);
		break;
	case PacketId::Favorite:
		g_UserManager.OnFavoritePacket(msg, s);
		break;
	case PacketId::Option:
		g_UserManager.OnOptionPacket(msg, s);
		break;
	case PacketId::Udp:
		g_UserManager.OnUdpPacket(msg, s);
		break;
	case PacketId::Item:
		g_ItemManager.OnItemPacket(msg, s);
		break;
	case PacketId::MiniGame:
		g_MiniGameManager.OnPacket(msg, s);
		break;
	case PacketId::MileageBingo:
		break;
	case PacketId::UpdateInfo:
		g_UserManager.OnUpdateInfoPacket(msg, s);
		break;
	case PacketId::Clan:
		g_ClanManager.OnPacket(msg, s);
		break;
	case PacketId::Statistic:
		g_PacketManager.SendStatistic(s);
		break;
	case PacketId::Rank:
		g_RankManager.OnRankPacket(msg, s);
		break;
	case PacketId::Hack:
		//printf("shit");
		break;
	case PacketId::Report:
		g_UserManager.OnReportPacket(msg, s);
		break;
	case PacketId::Alarm:
		g_UserManager.OnAlarmPacket(msg, s);
		break;
	case PacketId::Quest:
		g_QuestManager.OnPacket(msg, s);
		break;
	case PacketId::Title:
		g_QuestManager.OnTitlePacket(msg, s);
		break;
	case PacketId::HostServer:
		g_DedicatedServerManager.OnPacket(msg, s);
		break;
	case PacketId::Messenger:
		g_UserManager.OnMessengerPacket(msg, s);
		break;
	case PacketId::UserSurvey:
		g_UserManager.OnUserSurveyPacket(msg, s);
		break;
	case PacketId::Addon:
		g_UserManager.OnAddonPacket(msg, s);
		break;
	case PacketId::Ban:
		g_UserManager.OnBanPacket(msg, s);
		break;
	case PacketId::League:
		g_UserManager.OnLeaguePacket(msg, s);
		break;
	default:
		Console().Warn("Unimplemented packet: %d\n", msg->GetID());
		break;
	}
}

void CServerInstance::OnSecondTick()
{
	// update current time
	time_t prevTime = m_CurrentTime;
	m_CurrentTime = time(NULL);
	m_pCurrentLocalTime = localtime(&m_CurrentTime);
	m_CurrentTime /= 60; // get current time in minutes(last CSO builds use timestamp in minutes)
	m_nUptime++;

	UpdateConsoleStatus();

#ifdef USE_GUI
	GUI()->UpdateInfo(m_bIsServerActive, m_TCPServer.GetClients().size(), m_nUptime, GetMemoryInfo());
#endif

	Manager().SecondTick(m_CurrentTime);

	if (m_CurrentTime - prevTime > 0)
	{
		OnMinuteTick();
	}
}

void CServerInstance::OnMinuteTick()
{
	Console().Log("%s\n", GetMainInfo());

	Manager().MinuteTick(m_CurrentTime);
}

void CServerInstance::OnFunction(function<void()>& func)
{
	func();
}

double CServerInstance::GetMemoryInfo()
{
#ifdef WIN32
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));

	SIZE_T mem = pmc.WorkingSetSize;
	if (mem >= 10e9)
		Console().Warn("[ALERT] Server is using more than 1G of memory.\n");

	return mem / (1024.0 * 1024.0);
#else
	Console().Log("CServerInstance::GetMemoryInfo: not implemented\n");
	return 0;
#endif
}

const char* CServerInstance::GetMainInfo()
{
	return va("Memory usage: %.02fmb. Connected users: %d. Logged in users: %d.", GetMemoryInfo(), static_cast<int>(m_TCPServer.GetClients().size()), static_cast<int>(g_UserManager.GetUsers().size()));
}

void CServerInstance::DisconnectClient(IExtendedSocket* socket)
{
	if (!socket)
		return;

	m_TCPServer.DisconnectClient(socket);
}

std::vector<IExtendedSocket*> CServerInstance::GetClients()
{
	return m_TCPServer.GetClients();
}

IExtendedSocket* CServerInstance::GetSocketByID(unsigned int id)
{
	for (auto s : m_TCPServer.GetClients())
		if (s->GetID() == id)
			return s;

	return NULL;
}

void CServerInstance::UpdateConsoleStatus()
{
	Console().SetStatus(GetMainInfo());
	Console().UpdateStatus();
}

time_t CServerInstance::GetCurrentTime()
{
	return m_CurrentTime; // timestamp in minutes
}

tm* CServerInstance::GetCurrentLocalTime()
{
	return m_pCurrentLocalTime;
}