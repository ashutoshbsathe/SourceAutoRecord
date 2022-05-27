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

#include "RLAServer.hpp"
#include "Features/Tas/TasPlayer.hpp"
#include "Event.hpp"
#include "Scheduler.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"

#include "Utils/SDK.hpp"
#include "Modules/Server.hpp"
#include "Features/Renderer.hpp"
#include "Features/Session.hpp"

#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <condition_variable>

#ifndef _WIN32
#	define SOCKET int
#	define INVALID_SOCKET -1
#	define SOCKET_ERROR -1
#	define closesocket close
#	define WSACleanup() (void)0
#endif

#define RLA_CLIENT_SOCKET 6969

Variable sar_rla_server("sar_rla_server", "0", "Enable the remote RLA server.\n");

struct RLAClientData {
	SOCKET sock;
	std::deque<uint8_t> cmdbuf;
};

static SOCKET rla_g_listen_sock = INVALID_SOCKET;
static std::vector<RLAClientData> rla_g_clients;
static std::atomic<bool> rla_g_should_stop;

static RLAStatus rla_g_last_status;
static RLAStatus rla_g_current_status;
static std::mutex rla_g_status_mutex;

static std::condition_variable resume_networking;
static std::mutex access_observation;
static bool observation_ready;

uint8_t pixels[97200];
int width = 180, height = 180;
Vector vel, pos;
QAngle ang;

/*
static uint32_t popRaw32(std::deque<uint8_t> &buf) {
	uint32_t val = 0;
	for (int i = 0; i < 4; ++i) {
		val <<= 8;
		val |= buf[0];
		buf.pop_front();
	}
	return val;
}
*/

static void encodeRaw32(std::vector<uint8_t> &buf, uint32_t val) {
	buf.push_back((val >> 24) & 0xFF);
	buf.push_back((val >> 16) & 0xFF);
	buf.push_back((val >> 8)  & 0xFF);
	buf.push_back((val >> 0)  & 0xFF);
}

static void sendAll(const std::vector<uint8_t> &buf) {
	for (auto &cl : rla_g_clients) {
		send(cl.sock, (const char *)buf.data(), buf.size(), 0);
	}
}

static void sendAll(const uint8_t *buf, unsigned int num_bytes) {
    for(auto &cl : rla_g_clients) {
        int sent = send(cl.sock, buf, num_bytes, 0);
    }
}

/*
static void fullUpdate(RLAClientData &cl, bool first_packet = false) {
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
	union { float f; int i; } rate = { rla_g_last_status.playback_rate };
	buf.push_back(2);
	encodeRaw32(buf, rate.i);

	if (rla_g_last_status.active) {
		// active (0)
		buf.push_back(0);
		encodeRaw32(buf, rla_g_last_status.rla_path[0].size());
		for (char c : rla_g_last_status.rla_path[0]) {
			buf.push_back(c);
		}
		encodeRaw32(buf, rla_g_last_status.rla_path[1].size());
		for (char c : rla_g_last_status.rla_path[1]) {
			buf.push_back(c);
		}

		// state (3/4/5)
		switch (rla_g_last_status.playback_state) {
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
		encodeRaw32(buf, rla_g_last_status.playback_tick);
	} else {
		// inactive (1)
		buf.push_back(1);
	}

	send(cl.sock, (const char *)buf.data(), buf.size(), 0);
}
*/

static void RLAInit(RLAClientData &cl) {
    std::vector<uint8_t> buf;
    
    // game location (255)
    std::string dir = std::filesystem::current_path().string();
    std::replace(dir.begin(), dir.end(), '\\', '/');
    // first byte 255 
    buf.push_back(255);
    // length of the path 
    encodeRaw32(buf, (uint32_t)dir.size());
    // game location 
    for(char c : dir) {
        buf.push_back(c);
    }

	// playback rate (2)
	union { float f; int i; } rate = { rla_g_last_status.playback_rate };
	buf.push_back(2);
	encodeRaw32(buf, rate.i);

	send(cl.sock, (const char *)buf.data(), buf.size(), 0);
}

/*
static void update() {
	rla_g_status_mutex.lock();
	RLAStatus status = rla_g_current_status;
	rla_g_status_mutex.unlock();

	if (status.active != rla_g_last_status.active || status.rla_path[0] != rla_g_last_status.rla_path[0] || status.rla_path[1] != rla_g_last_status.rla_path[1]) {
		// big change; we might as well just do a full update
		rla_g_last_status = status;
		for (auto &cl : rla_g_clients) fullUpdate(cl);
		return;
	}

	if (status.playback_rate != rla_g_last_status.playback_rate) {
		// playback rate (2)

		union { float f; int i; } rate = { status.playback_rate };

		std::vector<uint8_t> buf{2};
		encodeRaw32(buf, rate.i);
		sendAll(buf);

		rla_g_last_status.playback_rate = status.playback_rate;
	}

	if (status.active && status.playback_state != rla_g_last_status.playback_state) {
		// state (3/4/5)

		switch (status.playback_state) {
		case PlaybackState::PLAYING:
			sendAll({3});
			break;
		case PlaybackState::PAUSED:
			sendAll({4});
			break;
		case PlaybackState::SKIPPING:
			sendAll({5});
			break;
		}

		rla_g_last_status.playback_state = status.playback_state;
	}

	if (status.active && status.playback_tick != rla_g_last_status.playback_tick) {
		// tick (6)

		std::vector<uint8_t> buf{6};
		encodeRaw32(buf, status.playback_tick);
		sendAll(buf);

		rla_g_last_status.playback_tick = status.playback_tick;
	}
}
*/

static bool RLAProcessAction(RLAClientData &cl) {
    if(cl.cmdbuf.size() == 0) return true; // client hasn't sent anything 
    uint8_t first = cl.cmdbuf[0];
    TasFramebulk rcvd_bulk, end;
    std::vector<TasFramebulk> fbQueue;
    rcvd_bulk.tick = 0;
    //THREAD_PRINT("First = %d\n", first);
    cl.cmdbuf.pop_front();
    observation_ready = false;
    if(first > 127) {
        //THREAD_PRINT("Reset command received\n");
        // now we prepare a dummy first framebulk and execute it after `restart_level`
        // TODO: let this be user configurable 
        rcvd_bulk.moveAnalog.x = 0;
        rcvd_bulk.moveAnalog.y = 0;
        rcvd_bulk.viewAnalog.x = 0;
        rcvd_bulk.viewAnalog.y = 0;

        end.tick = 600;
        end.commands.push_back("sar_rla_observe");

        fbQueue.push_back(rcvd_bulk);
        fbQueue.push_back(end);

        Scheduler::OnMainThread([=](){
            tasPlayer->scriptVersion = 2;
            tasPlayer->Stop(); // stops the pause from previous TAS pause
            // tasPlayer->UpdateServer(); // this could be potentially not required and slow down the performance ? TODO: ask mlugg 
            tasPlayer->SetStartInfo(TasStartType::ChangeLevel, engine->GetCurrentMapName());
            tasPlayer->numSessionsBeforeStart += 1; // this is apparently required so that it actually plays the session
            // ref -- https://github.com/p2sr/SourceAutoRecord/blob/d25912e4aa0f21476e6c8bca4cfae5ec032a502e/src/Features/Tas/TasParser.cpp#L248
            tasPlayer->scriptVersion = 1;
            tasPlayer->isCoop = false;
            tasPlayer->coopControlSlot = -1;
            tasPlayer->SetFrameBulkQueue(0, fbQueue);
            tasPlayer->Activate();
            //THREAD_PRINT("RLAServer: Restarting level\n");
        });
    }
    else {
        //THREAD_PRINT("Other values = [%d %d %d %d]\n", cl.cmdbuf[1], cl.cmdbuf[2], cl.cmdbuf[3], cl.cmdbuf[4]);

        rcvd_bulk.moveAnalog.x = -1 + cl.cmdbuf[1] * 1.0/128;
        rcvd_bulk.moveAnalog.y = -1 + cl.cmdbuf[2] * 1.0/128;
        rcvd_bulk.viewAnalog.x = -1 + cl.cmdbuf[3] * 1.0/128;
        rcvd_bulk.viewAnalog.y = -1 + cl.cmdbuf[4] * 1.0/128;

        rcvd_bulk.buttonStates[5] = first % 2;
        first /= 2;
        rcvd_bulk.buttonStates[4] = first % 2;
        first /= 2;
        rcvd_bulk.buttonStates[3] = first % 2;
        first /= 2;
        rcvd_bulk.buttonStates[2] = first % 2;
        first /= 2;
        rcvd_bulk.buttonStates[1] = first % 2;
        first /= 2;
        rcvd_bulk.buttonStates[0] = first % 2;
        first /= 2;

        end.tick = 4;
        end.commands.push_back("sar_rla_observe");

        fbQueue.push_back(rcvd_bulk);
        fbQueue.push_back(end);

        cl.cmdbuf.pop_front();cl.cmdbuf.pop_front();cl.cmdbuf.pop_front();cl.cmdbuf.pop_front();
        Scheduler::OnMainThread([=](){
            tasPlayer->Stop(); // stops the pause from previous TAS pause
            tasPlayer->SetStartInfo(TasStartType::StartImmediately, "");
            tasPlayer->scriptVersion = 1;
            tasPlayer->isCoop = false;
            tasPlayer->coopControlSlot = -1;
            // tasPlayer->UpdateServer(); // this could be potentially not required and slow down the performance ? TODO: ask mlugg 
            tasPlayer->SetFrameBulkQueue(0, fbQueue);
            tasPlayer->Activate();
            //THREAD_PRINT("RLAServer: Executing framebulk\n");
        });
    }
    std::unique_lock<std::mutex> observation_lock(access_observation);
    while (!observation_ready)
        resume_networking.wait(observation_lock);
    // guaranteed observation is ready
    // if something bad happens, let the client handle it
    sendAll({4});
    float player_data[] = {vel.x, vel.y, vel.z, pos.x, pos.y, pos.z, ang.x, ang.y, ang.z};
    uint8_t *arr_player_data = reinterpret_cast<uint8_t *>(player_data);
   
    std::vector<uint8_t> to_send;
    to_send.push_back(1); // TODO: Send data about player death etc.
    for(int i = 0; i < 36; i++) {
        to_send.push_back(arr_player_data[i]);
    }
    //THREAD_PRINT("paused = ", tasPlayer->IsPaused() ? "true" : "false", "\n");
    //THREAD_PRINT("vel = [%f, %f, %f]\n", vel.x, vel.y, vel.z);
    //THREAD_PRINT("pos = [%f, %f, %f]\n", pos.x, pos.y, pos.z);
    //THREAD_PRINT("ang = [%f, %f, %f]\n", ang.x, ang.y, ang.z);

    sendAll(pixels, 32400); // somehow windows doesn't like to send 32768 bytes 
    sendAll(pixels + 32400, 32400);
    sendAll(pixels + 64800, 32400);
    sendAll(to_send);
    return true;
}

/*
static bool processCommands(RLAClientData &cl) {
	while (true) {
		if (cl.cmdbuf.size() == 0) return true;

		size_t extra = cl.cmdbuf.size() - 1;

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
					rlaPlayer->PlayFile(filename1, filename2);
				});
			}
			break;

		case 1: // stop playback
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				rlaPlayer->Stop(true);
			});
			break;

		case 2: // request playback rate change
			if (extra < 4) return true;
			cl.cmdbuf.pop_front();
			{
				union { uint32_t i; float f; } rate = { popRaw32(cl.cmdbuf) };
				Scheduler::OnMainThread([=](){
					sar_rla_playback_rate.SetValue(rate.f);
				});
			}
			break;

		case 3: // request state=playing
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				rlaPlayer->Resume();
			});
			break;

		case 4: // request state=paused
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([=](){
				rlaPlayer->Pause();
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
					sar_rla_skipto.SetValue(tick);
					if (pause_after) sar_rla_pauseat.SetValue(tick);
				});
			}
			break;

		case 6: // set next pause tick
			if (extra < 4) return true;
			cl.cmdbuf.pop_front();
			{
				int tick = popRaw32(cl.cmdbuf);
				Scheduler::OnMainThread([=](){
					sar_rla_pauseat.SetValue(tick);
				});
			}
			break;

		case 7: // advance tick
			cl.cmdbuf.pop_front();
			Scheduler::OnMainThread([](){
				rlaPlayer->AdvanceFrame();
			});
			break;

		default:
			return false; // Bad command - disconnect
		}
	}
}
*/ 

static void processConnections() {
	fd_set set;
	FD_ZERO(&set);

	SOCKET max = rla_g_listen_sock;

	FD_SET(rla_g_listen_sock, &set);
	for (auto client : rla_g_clients) {
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

	if (FD_ISSET(rla_g_listen_sock, &set)) {
		SOCKET cl = accept(rla_g_listen_sock, nullptr, nullptr);
		if (cl != INVALID_SOCKET) {
			rla_g_clients.push_back({ cl, {} });
            RLAInit(rla_g_clients[rla_g_clients.size() - 1]);
		}
	}

	for (size_t i = 0; i < rla_g_clients.size(); ++i) {
		auto &cl = rla_g_clients[i];

		if (!FD_ISSET(cl.sock, &set)) continue;

		char buf[1024];
		int len = recv(cl.sock, buf, sizeof buf, 0); // TODO: We can always expect 5 bytes

		if (len == 0 || len == SOCKET_ERROR) { // Connection closed or errored
			rla_g_clients.erase(rla_g_clients.begin() + i);
			--i;
			continue;
		}

        cl.cmdbuf.clear();
		//cl.cmdbuf.insert(cl.cmdbuf.end(), std::begin(buf), std::begin(buf) + len);
		cl.cmdbuf.insert(cl.cmdbuf.begin(), std::begin(buf), std::begin(buf) + len);
        for(int i = 0; i < len; i++) {
            //THREAD_PRINT("buf[%d] = %d\n", i, buf[i]);
        }
        //THREAD_PRINT("cl.cmdbuf = [ ");
        for(auto i = cl.cmdbuf.begin(); i != cl.cmdbuf.end(); i++) {
            //THREAD_PRINT("%d ", *i);
        }
        //THREAD_PRINT("]\n");
		if (!RLAProcessAction(cl)) {
			// Client sent a bad command; terminate connection
			closesocket(cl.sock);
			rla_g_clients.erase(rla_g_clients.begin() + i);
			--i;
			continue;
		}
        cl.cmdbuf.clear();
	}
}

static void mainThread() {
	THREAD_PRINT("Starting RLA server\n");

#ifdef _WIN32
	WSADATA wsa_data;
	int err = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (err){
		THREAD_PRINT("Could not initialize RLA client: WSAStartup failed (%d)\n", err);
		return;
	}
#endif

	rla_g_listen_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (rla_g_listen_sock == INVALID_SOCKET) {
		THREAD_PRINT("Could not initialize RLA client: socket creation failed\n");
		WSACleanup();
		return;
	}

	// why tf is this enabled by default on Windows
	int v6only = 0;
	setsockopt(rla_g_listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof v6only);

	struct sockaddr_in6 saddr{
		AF_INET6,
		htons(RLA_CLIENT_SOCKET),
		0,
		in6addr_any,
		0,
	};

	if (bind(rla_g_listen_sock, (struct sockaddr *)&saddr, sizeof saddr) == SOCKET_ERROR) {
		THREAD_PRINT("Could not initialize RLA client: socket bind failed\n");
		closesocket(rla_g_listen_sock);
		WSACleanup();
		return;
	}

	if (listen(rla_g_listen_sock, 4) == SOCKET_ERROR) {
		THREAD_PRINT("Could not initialize RLA client: socket listen failed\n");
		closesocket(rla_g_listen_sock);
		WSACleanup();
		return;
	}

	while (!rla_g_should_stop.load()) {
		processConnections();
		//update();
	}

	THREAD_PRINT("Stopping RLA server\n");

	for (auto &cl : rla_g_clients) {
		closesocket(cl.sock);
	}

	closesocket(rla_g_listen_sock);
	WSACleanup();
}

static std::thread rla_g_net_thread;
static bool rla_g_running;

ON_EVENT(FRAME) {
	bool should_run = sar_rla_server.GetBool();
	if (rla_g_running && !should_run) {
		rla_g_should_stop.store(true);
		if (rla_g_net_thread.joinable()) rla_g_net_thread.join();
		rla_g_running = false;
	} else if (!rla_g_running && should_run) {
		rla_g_should_stop.store(false);
		rla_g_net_thread = std::thread(mainThread);
		rla_g_running = true;
	}
}

ON_EVENT_P(SAR_UNLOAD, -100) {
	sar_rla_server.SetValue(false);
	rla_g_should_stop.store(true);
	if (rla_g_net_thread.joinable()) rla_g_net_thread.join();
}

void RLAServer::SetStatus(RLAStatus s) {
	rla_g_status_mutex.lock();
	rla_g_current_status = s;
	rla_g_status_mutex.unlock();
}

CON_COMMAND(sar_rla_observe, "sar_rla_observe -- prepares observation for the RL agent\n") {
    tasPlayer->Pause();
    console->Print("sar_rla_observe: Start\n");
    Memory::VMT<void(__rescall *)(void *, int, int, int, int, void *, ImageFormat)>(*Renderer::cached_g_videomode, Offsets::ReadScreenPixels)(*Renderer::cached_g_videomode, 0, 0, width, height, pixels, IMAGE_FORMAT_BGR888);
    console->Print("sar_rla_observe: Captured frame\n");
    auto player = server->GetPlayer(GET_SLOT() + 1);
    vel = server->GetLocalVelocity(player);
    pos = server->GetAbsOrigin(player);
    ang = server->GetAbsAngles(player);
    console->Print("sar_rla_observe: Observation completed\n");
    resume_networking.notify_all(); 
    observation_ready = true;
    console->Print("sar_rla_observe: Notified networking thread. Finished\n");
}
