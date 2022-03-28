// This *has* to come first because <winsock2> doesn't like being
// imported after <windows>. I fucking hate this platform
#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <sys/socket.h>
#	include <sys/select.h>
#	include <netinet/in.h>
#	include <unistd.h>
#endif

#include "TasServer.hpp"
#include "TasPlayer.hpp"
#include "Event.hpp"
#include "Scheduler.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
//#include "Modules/Client.hpp"
#include "Modules/Server.hpp"

#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

#ifndef _WIN32
#	define SOCKET int
#	define INVALID_SOCKET -1
#	define SOCKET_ERROR -1
#	define closesocket close
#	define WSACleanup() (void)0
#endif

#define TAS_CLIENT_SOCKET 6555

Variable sar_tas_server("sar_tas_server", "0", "Enable the remote TAS server.\n");

struct ClientData {
	SOCKET sock;
	std::deque<uint8_t> cmdbuf;
};

static SOCKET g_listen_sock = INVALID_SOCKET;
static std::vector<ClientData> g_clients;
static std::atomic<bool> g_should_stop;

static TasStatus g_last_status;
static TasStatus g_current_status;
static std::mutex g_status_mutex;


static uint32_t popRaw32(std::deque<uint8_t> &buf) {
	uint32_t val = 0;
	for (int i = 0; i < 4; ++i) {
		val <<= 8;
		val |= buf[0];
		buf.pop_front();
	}
	return val;
}

static void encodeRaw32(std::vector<uint8_t> &buf, uint32_t val) {
	buf.push_back((val >> 24) & 0xFF);
	buf.push_back((val >> 16) & 0xFF);
	buf.push_back((val >> 8)  & 0xFF);
	buf.push_back((val >> 0)  & 0xFF);
}

static void sendAll(const std::vector<uint8_t> &buf) {
	for (auto &cl : g_clients) {
		send(cl.sock, (const char *)buf.data(), buf.size(), 0);
	}
}

static void fullUpdate(ClientData &cl, bool first_packet = false) {
	std::vector<uint8_t> buf;

	if (first_packet) {
		// game location (255)
		std::string dir = std::filesystem::current_path().string();
		std::replace(dir.begin(), dir.end(), '\\', '/');
		buf.push_back(255);
		encodeRaw32(buf, (uint32_t)dir.size());
		for (char c : dir) {
			buf.push_back(c);
		}
	}

	// playback rate (2)
	union { float f; int i; } rate = { g_last_status.playback_rate };
	buf.push_back(2);
	encodeRaw32(buf, rate.i);

	if (g_last_status.active) {
		// active (0)
		buf.push_back(0);
		encodeRaw32(buf, g_last_status.tas_path[0].size());
		for (char c : g_last_status.tas_path[0]) {
			buf.push_back(c);
		}
		encodeRaw32(buf, g_last_status.tas_path[1].size());
		for (char c : g_last_status.tas_path[1]) {
			buf.push_back(c);
		}

		// state (3/4/5)
		switch (g_last_status.playback_state) {
		case PlaybackState::PLAYING:
			buf.push_back(3);
			break;
		case PlaybackState::PAUSED:
			buf.push_back(4);
			break;
		case PlaybackState::SKIPPING:
			buf.push_back(5);
			break;
		}

		// current tick (6)
		buf.push_back(6);
		encodeRaw32(buf, g_last_status.playback_tick);
	} else {
		// inactive (1)
		buf.push_back(1);
	}

	send(cl.sock, (const char *)buf.data(), buf.size(), 0);
}

static void update() {
	g_status_mutex.lock();
	TasStatus status = g_current_status;
	g_status_mutex.unlock();

	if (status.active != g_last_status.active || status.tas_path[0] != g_last_status.tas_path[0] || status.tas_path[1] != g_last_status.tas_path[1]) {
		// big change; we might as well just do a full update
		g_last_status = status;
		for (auto &cl : g_clients) fullUpdate(cl);
		return;
	}

	if (status.playback_rate != g_last_status.playback_rate) {
		// playback rate (2)

		union { float f; int i; } rate = { status.playback_rate };

		std::vector<uint8_t> buf{2};
		encodeRaw32(buf, rate.i);
		sendAll(buf);

		g_last_status.playback_rate = status.playback_rate;
	}

	if (status.active && status.playback_state != g_last_status.playback_state) {
		// state (3/4/5)

		switch (status.playback_state) {
		case PlaybackState::PLAYING:
			sendAll({3});
			break;
		case PlaybackState::PAUSED:
			sendAll({4});
            {
                auto player = server->GetPlayer(GET_SLOT() + 1);
                console->Print("player = %p\n", player);
                auto vel = server->GetLocalVelocity(player);
                auto pos = server->GetAbsOrigin(player);
                auto ang = server->GetAbsAngles(player);
                float player_data[] = {vel.x, vel.y, vel.z, pos.x, pos.y, pos.z, ang.x, ang.y, ang.z};
                char const *to_send = reinterpret_cast<char const *>(player_data);
                console->Print("paused = %d\n", tasPlayer->IsPaused());
                console->Print("vel = %f %f %f\n", vel.x, vel.y, vel.z);
                console->Print("pos = %f %f %f\n", pos.x, pos.y, pos.z);
                console->Print("ang = %f %f %f\n", ang.x, ang.y, ang.z);
                std::vector<uint8_t> arr;
                for(int i = 0; i < 36; i++)
                    arr.push_back(i);
                sendAll(arr);
            }
			break;
		case PlaybackState::SKIPPING:
			sendAll({5});
			break;
		}

		g_last_status.playback_state = status.playback_state;
	}

	if (status.active && status.playback_tick != g_last_status.playback_tick) {
		// tick (6)

		std::vector<uint8_t> buf{6};
		encodeRaw32(buf, status.playback_tick);
		sendAll(buf);

		g_last_status.playback_tick = status.playback_tick;
	}
}

static bool processCommands(ClientData &cl) {
    /*
     * It honestly might be cheaper to dump the TasFramebulk in a file 
     * Run it using `bind l "sar_tas_stop; sar_tas_play rl_challenge_1_thr=0.45; sar_tas_playback_rate 100; sar_tas_pauseat 1599;"`
     *
     * Better idea: 
     * Wrap the TasFramebulk as follows:
     * Line 1: 0><rcvd_bulk>
     * Line 2: n>|||sar_tas_pause|
     * Then at every rcvd_bulk, we simply do `sar_tas_stop; sar_tas_play dummy` that's it !!!!
     */
	while (true) {
		if (cl.cmdbuf.size() == 0) return true;

		int extra = cl.cmdbuf.size() - 1;
        char message[] = "message";

        TasFramebulk rcvd_bulk;
        rcvd_bulk.tick = 0;
        rcvd_bulk.moveAnalog.x = 0;
        rcvd_bulk.moveAnalog.y = 1;
        rcvd_bulk.viewAnalog.x = 0;
        rcvd_bulk.viewAnalog.y = 0;

        TasFramebulk end;
        end.tick = 4;
        end.commands.push_back("sar_tas_pause");
        
        std::vector<TasFramebulk> fbQueue;
        fbQueue.push_back(rcvd_bulk);
        fbQueue.push_back(end);
        Scheduler::OnMainThread([=](){
            tasPlayer->Stop(); // stop the pause from previous TAS player
            tasPlayer->SetFrameBulkQueue(0, fbQueue);
            tasPlayer->Activate();
        });
        return true;
        // This should be executed only after "m" TAS Framebulks
        // Last framebulk has `sar_tas_pause` in the commands field
        // so when the game is paused, we know that "m" Framebulks are over
        /*
        console->Print("cmdbuf[0] = %d, size = %d\ncmdbuf = [ ", cl.cmdbuf[0], cl.cmdbuf.size());
        for(uint8_t byte: cl.cmdbuf) {
            console->Print("%d ", byte);
        }
        console->Print("]\n");
        console->Print("tasPlayer->framebulkQueue[0].size() = %d, ", tasPlayer->GetFrameBulkQueue(0).size());
        console->Print("tasPlayer->framebulkQueue[1].size() = %d\n", tasPlayer->GetFrameBulkQueue(1).size());
        */ 
        // Note that I had to implement IsPaused because SAR code can just use `paused` lmao
        // send(cl.sock, to_send, 36, 0);

        return true;
		switch (cl.cmdbuf[0]) {
		case 0: // request playback
			if (extra < 8) return true;
			{
				std::deque<uint8_t> copy = cl.cmdbuf;

				copy.pop_front();

				uint32_t len1 = popRaw32(copy);
				if (extra < 8 + len1) return true;

				std::string filename1;
				for (size_t i = 0; i < len1; ++i) {
					filename1 += copy[0];
					copy.pop_front();
				}

				uint32_t len2 = popRaw32(copy);
				if (extra < 8 + len1 + len2) return true;

				std::string filename2;
				for (size_t i = 0; i < len2; ++i) {
					filename2 += copy[0];
					copy.pop_front();
				}

				cl.cmdbuf = copy; // We actually had everything we needed, so switch to the modified buffer

				Scheduler::OnMainThread([=](){
					tasPlayer->PlayFile(filename1, filename2);
				});
			}
			break;

		case 1: // stop playback
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				tasPlayer->Stop(true);
			});
			break;

		case 2: // request playback rate change
			if (extra < 4) return true;
			cl.cmdbuf.pop_front();
			{
				union { uint32_t i; float f; } rate = { popRaw32(cl.cmdbuf) };
				Scheduler::OnMainThread([=](){
					sar_tas_playback_rate.SetValue(rate.f);
				});
			}
			break;

		case 3: // request state=playing
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				tasPlayer->Resume();
			});
			break;

		case 4: // request state=paused
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				tasPlayer->Pause();
			});
			break;

		case 5: // request state=fast-forward
			if (extra < 5) return true;
			cl.cmdbuf.pop_front();
			{
				int tick = popRaw32(cl.cmdbuf);
				bool pause_after = cl.cmdbuf[0];
				cl.cmdbuf.pop_front();
				Scheduler::OnMainThread([=](){
					sar_tas_skipto.SetValue(tick);
					if (pause_after) sar_tas_pauseat.SetValue(tick);
				});
			}
			break;

		case 6: // set next pause tick
			if (extra < 4) return true;
			cl.cmdbuf.pop_front();
			{
				int tick = popRaw32(cl.cmdbuf);
				Scheduler::OnMainThread([=](){
					sar_tas_pauseat.SetValue(tick);
				});
			}
			break;

		case 7: // advance tick
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([](){
				tasPlayer->AdvanceFrame();
			});
			break;

		default:
			return false; // Bad command - disconnect
		}
	}
}

static void processConnections() {
	fd_set set;
	FD_ZERO(&set);

	SOCKET max = g_listen_sock;

	FD_SET(g_listen_sock, &set);
	for (auto client : g_clients) {
		FD_SET(client.sock, &set);
		if (max < client.sock) max = client.sock;
	}

	// 0.05s timeout
	timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 50000;

	int nsock = select(max + 1, &set, nullptr, nullptr, &tv);
	if (nsock == SOCKET_ERROR || !nsock) {
		return;
	}

	if (FD_ISSET(g_listen_sock, &set)) {
		SOCKET cl = accept(g_listen_sock, nullptr, nullptr);
		if (cl != INVALID_SOCKET) {
			g_clients.push_back({ cl, {} });
			fullUpdate(g_clients[g_clients.size() - 1], true);
		}
	}

	for (size_t i = 0; i < g_clients.size(); ++i) {
		auto &cl = g_clients[i];

		if (!FD_ISSET(cl.sock, &set)) continue;

		char buf[1024];
		int len = recv(cl.sock, buf, sizeof buf, 0);

		if (len == 0 || len == SOCKET_ERROR) { // Connection closed or errored
			g_clients.erase(g_clients.begin() + i);
			--i;
			continue;
		}

		cl.cmdbuf.insert(cl.cmdbuf.end(), std::begin(buf), std::begin(buf) + len);

		if (!processCommands(cl)) {
			// Client sent a bad command; terminate connection
			closesocket(cl.sock);
			g_clients.erase(g_clients.begin() + i);
			--i;
			continue;
		}
	}
}

static void mainThread() {
	THREAD_PRINT("Starting TAS server\n");

#ifdef _WIN32
	WSADATA wsa_data;
	int err = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (err){
		THREAD_PRINT("Could not initialize TAS client: WSAStartup failed (%d)\n", err);
		return;
	}
#endif

	g_listen_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (g_listen_sock == INVALID_SOCKET) {
		THREAD_PRINT("Could not initialize TAS client: socket creation failed\n");
		WSACleanup();
		return;
	}

	// why tf is this enabled by default on Windows
	int v6only = 0;
	setsockopt(g_listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof v6only);

	struct sockaddr_in6 saddr{
		AF_INET6,
		htons(TAS_CLIENT_SOCKET),
		0,
		in6addr_any,
		0,
	};

	if (bind(g_listen_sock, (struct sockaddr *)&saddr, sizeof saddr) == SOCKET_ERROR) {
		THREAD_PRINT("Could not initialize TAS client: socket bind failed\n");
		closesocket(g_listen_sock);
		WSACleanup();
		return;
	}

	if (listen(g_listen_sock, 4) == SOCKET_ERROR) {
		THREAD_PRINT("Could not initialize TAS client: socket listen failed\n");
		closesocket(g_listen_sock);
		WSACleanup();
		return;
	}

	while (!g_should_stop.load()) {
		processConnections();
		update();
	}

	THREAD_PRINT("Stopping TAS server\n");

	for (auto &cl : g_clients) {
		closesocket(cl.sock);
	}

	closesocket(g_listen_sock);
	WSACleanup();
}

static std::thread g_net_thread;
static bool g_running;

ON_EVENT(FRAME) {
	bool should_run = sar_tas_server.GetBool();
	if (g_running && !should_run) {
		g_should_stop.store(true);
		if (g_net_thread.joinable()) g_net_thread.join();
		g_running = false;
	} else if (!g_running && should_run) {
		g_should_stop.store(false);
		g_net_thread = std::thread(mainThread);
		g_running = true;
	}
}

ON_EVENT_P(SAR_UNLOAD, -100) {
	sar_tas_server.SetValue(false);
	g_should_stop.store(true);
	if (g_net_thread.joinable()) g_net_thread.join();
}

void TasServer::SetStatus(TasStatus s) {
	g_status_mutex.lock();
	g_current_status = s;
	g_status_mutex.unlock();
}
