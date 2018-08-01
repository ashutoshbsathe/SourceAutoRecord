#include "TheStanleyParable.hpp"

#include "Offsets.hpp"

TheStanleyParable::TheStanleyParable()
{
    this->version = SourceGame::TheStanleyParable;
}
void TheStanleyParable::LoadOffsets()
{
    Portal2::LoadOffsets();

    using namespace Offsets;

    // engine.so

    Cbuf_AddText = 45; // CEngineClient::ClientCmd
    tickcount = 74; // CClientState::ProcessTick
    interval_per_tick = 82; // CClientState::ProcessTick
    HostState_OnClientConnected = 1523; // CClientState::SetSignonState
    demoplayer = 92; // CClientState::Disconnect
    demorecorder = 105; // CClientState::Disconnect
    m_szLevelName = 56; // CEngineTool::GetCurrentMap
    Key_SetBinding = 59; // unbind

    // server.so

    g_InRestore = 32; // CServerGameDLL::GameFrame
    gpGlobals = 84; // CServerGameDLL::GameFrame
    ServiceEventQueue = 328; // CServerGameDLL::GameFrame
    g_EventQueue = 24; // ServiceEventQueue

    // client.so

    GetClientMode = 11; // CHLClient::HudProcessInput
    IN_ActivateMouse = 15; // CHLClient
    g_Input = 1; // CHLClient::IN_ActivateMouse
    GetButtonBits = 2; // CInput
    JoyStickApplyMovement = 64; // CInput
    in_jump = 210; // CInput::GetButtonBits
    KeyDown = 337; // CInput::JoyStickApplyMovement
    KeyUp = 384; // CInput::JoyStickApplyMovement

    // vguimatsurface.so

    StartDrawing = 692; // CMatSystemSurface::PaintTraverseEx
    FinishDrawing = 627; // CMatSystemSurface::PaintTraverseEx
}
void TheStanleyParable::LoadRules()
{
}
const char* TheStanleyParable::GetVersion()
{
    return "The Stanley Parable (6130)";
}
