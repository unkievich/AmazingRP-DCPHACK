#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <map>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <direct.h>
#include <sstream>
#include <random>
#include "Ext/imgui.h"
#include "Ext/imgui_impl_dx9.h"
#include "Ext/imgui_impl_win32.h"
#include "Ext/imgui_internal.h" 
#include "Ext/MinHook.h"
#include "Ext/BitStream.h"
#include "Ext/icons.h"
#include "sampapi/common/CVector.h"
#include "sampapi/common/CMatrix.h"
#include "sampapi/0.3.7-R3-1/CNetGame.h"
#include "sampapi/0.3.7-R3-1/CObjectPool.h"
#include "sampapi/0.3.7-R3-1/CObject.h"
#include "sampapi/0.3.7-R3-1/CPlayerPool.h"
#include "sampapi/0.3.7-R3-1/CRemotePlayer.h"
#include "sampapi/0.3.7-R3-1/CPed.h" 
#include "sampapi/0.3.7-R3-1/CVehicle.h"
#include <thread>
#include <filesystem>

std::string Utf8ToAnsi(const std::string& str) {
    if (str.empty()) return "";

    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (wchars_num == 0) return "";
    std::vector<wchar_t> wstr(wchars_num);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wchars_num);

    int chars_num = WideCharToMultiByte(1251, 0, &wstr[0], -1, NULL, 0, NULL, NULL);
    if (chars_num == 0) return "";
    std::vector<char> astr(chars_num);
    WideCharToMultiByte(1251, 0, &wstr[0], -1, &astr[0], chars_num, NULL, NULL);

    return std::string(&astr[0]);
}

#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb_image.h"

IDirect3DDevice9* g_pd3dDevice = nullptr;

struct SkinTextureCache {
    IDirect3DTexture9* texture = nullptr;
    bool tried_loading = false;
};
std::map<int, SkinTextureCache> g_SkinTextures;

#pragma comment(lib, "d3d9.lib")

std::map<ImGuiID, float> anims;
std::map<ImGuiID, float> slider_vals;

float menu_color[3] = { 0.0f, 0.57f, 1.0f };

namespace fs = std::filesystem;
using namespace sampapi;
using namespace sampapi::v037r3;

CVector g_PieTpTarget = { 0.0f, 0.0f, 0.0f };
bool g_PieTpActive = false;

typedef bool(__fastcall* RakClient_RPC_t)(void* pThis, void* edx, int* rpcId, BitStream* bitStream, int priority, int reliability, char orderingChannel, bool shiftTimestamp);
RakClient_RPC_t oRakClientRPC = NULL;

struct TeleportPoint {
    const char* name;
    float x, y, z;
};

std::vector<TeleportPoint> locs_transport = {
    { "Avtovokzal Batyrevo", 1933.0f, 2141.0f, 16.0f },
    { "Station Batyrevo", 1638.0f, 2535.0f, 15.0f },
    { "Station Yuzhny", 2736.0f, -2447.0f, 21.0f },
    { "Station Arzamas", 532.0f, 1675.0f, 12.0f }
};

std::vector<TeleportPoint> locs_orgs = {
    { "DPS", 449.0f, 735.0f, 12.0f },
    { "FSB", -315.0f, 708.0f, 12.0f },
    { "Government", 1826.0f, 2140.0f, 15.0f },
    { "Army", 1692.0f, 1710.0f, 15.0f },
    { "Voenkomat", 1893.0f, 2378.0f, 16.0f },
    { "FSIN", -7.0f, -2645.0f, 37.0f },
    { "ESS (Medic)", 2196.0f, -2188.0f, 21.0f },
    { "PPS", 2523.0f, -2466.0f, 21.0f },
    { "Carrier", 201.0f, 2721.0f, 17.0f },
    { "Mil. Wagon", 2541.0f, -1824.0f, 24.0f },
    { "TRK Amazing", 2127.0f, -1950.0f, 20.0f }
};

#ifndef ImMax
#define ImMax(a, b) (((a) > (b)) ? (a) : (b))
#endif

int PiePopupSelectMenu(const ImVec2& center, const char* popup_id, const char** items, int items_count, int* p_selected)
{
    int ret = -1;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    if (ImGui::BeginPopup(popup_id))
    {
        const ImVec2 drag_delta = ImVec2(ImGui::GetIO().MousePos.x - center.x, ImGui::GetIO().MousePos.y - center.y);
        const float drag_dist2 = drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float RADIUS_MIN = 30.0f;
        const float RADIUS_MAX = 120.0f;
        const float RADIUS_INTERACT_MIN = 20.0f;
        const int ITEMS_MIN = 6;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->PushClipRectFullScreen();
        draw_list->PathArcTo(center, (RADIUS_MIN + RADIUS_MAX) * 0.5f, 0.0f, IM_PI * 2.0f * 0.99f, 32);
        draw_list->PathStroke(ImColor(0, 0, 0), true, RADIUS_MAX - RADIUS_MIN);

        const float item_arc_span = 2 * IM_PI / ImMax(ITEMS_MIN, items_count);
        float drag_angle = atan2f(drag_delta.y, drag_delta.x);
        if (drag_angle < -0.5f * item_arc_span)
            drag_angle += 2.0f * IM_PI;

        int item_hovered = -1;
        for (int item_n = 0; item_n < items_count; item_n++)
        {
            const char* item_label = items[item_n];
            const float item_ang_min = item_arc_span * (item_n + 0.02f) - item_arc_span * 0.5f;
            const float item_ang_max = item_arc_span * (item_n + 0.98f) - item_arc_span * 0.5f;

            bool hovered = false;
            if (drag_dist2 >= RADIUS_INTERACT_MIN * RADIUS_INTERACT_MIN)
            {
                if (drag_angle >= item_ang_min && drag_angle < item_ang_max)
                    hovered = true;
            }
            bool selected = p_selected && (*p_selected == item_n);

            int arc_segments = (int)(32 * item_arc_span / (2 * IM_PI)) + 1;
            draw_list->PathArcTo(center, RADIUS_MAX - style.ItemInnerSpacing.x, item_ang_min, item_ang_max, arc_segments);
            draw_list->PathArcTo(center, RADIUS_MIN + style.ItemInnerSpacing.x, item_ang_max, item_ang_min, arc_segments);

            ImColor col_hovered = ImColor((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 200);
            ImColor col_base = ImColor(40, 40, 45, 200);
            ImColor col_selected = ImColor(80, 80, 80, 200);

            draw_list->PathFillConvex(hovered ? col_hovered : selected ? col_selected : col_base);

            ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, item_label);
            ImVec2 text_pos = ImVec2(
                center.x + cosf((item_ang_min + item_ang_max) * 0.5f) * (RADIUS_MIN + RADIUS_MAX) * 0.5f - text_size.x * 0.5f,
                center.y + sinf((item_ang_min + item_ang_max) * 0.5f) * (RADIUS_MIN + RADIUS_MAX) * 0.5f - text_size.y * 0.5f);
            draw_list->AddText(text_pos, ImColor(255, 255, 255), item_label);

            if (hovered)
                item_hovered = item_n;
        }
        draw_list->PopClipRect();

        if (ImGui::IsMouseReleased(0))
        {
            ImGui::CloseCurrentPopup();
            ret = item_hovered;
            if (p_selected)
                *p_selected = item_hovered;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    return ret;
}

bool tp_pie_open = false;
int tp_pie_step = 0;
int tp_pie_category = -1;
ImVec2 tp_pie_center = { 0,0 };

class cPedIntelligence final {
public:
    auto flushImmediately(const bool bUnk) -> bool {
        return reinterpret_cast<bool(__thiscall*)(class cPedIntelligence* pThis, const bool bUnk)>(0x601640)(this, bUnk);
    }
    auto clearTasks(const bool bFirst, const bool bSecond) -> void {
        reinterpret_cast<void(__thiscall*)(class cPedIntelligence* pThis, const bool bFirst, const bool bSecond)>(0x601420)(this, bFirst, bSecond);
    }
};

class cPed final {
public:
    auto getPedIntelligence(void) const -> class cPedIntelligence* {
        return *reinterpret_cast<class cPedIntelligence**>(reinterpret_cast<const unsigned __int32>(this) + 0x47C);
    }
};

class cSA final {
public:
    static auto getPlayerPed(void) -> class cPed* {
        return reinterpret_cast<class cPed* (__cdecl*)(const __int8 i8PlayerID)>(0x56E210)(0);
    }
    static auto disembarkPed(const class cPed* pPed) -> void {
        if (pPed != nullptr) {
            class cPedIntelligence* pIntelligence{ pPed->getPedIntelligence() };
            if (pIntelligence != nullptr) {
                pIntelligence->flushImmediately(true);
                pIntelligence->clearTasks(true, true);
            }
        }
    }
};

#pragma pack(push, 1)
struct stOnFootData {
    uint16_t sLeftRightKeys;
    uint16_t sUpDownKeys;
    uint16_t sKeys;
    float fPosition[3];
    float fQuaternion[4];
    uint8_t byteHealth;
    uint8_t byteArmor;
    uint8_t byteCurrentWeapon;
    uint8_t byteSpecialAction;
    float fMoveSpeed[3];
    float fSurfingOffsets[3];
    uint16_t sSurfingVehicleID;
    short sCurrentAnimationID;
    short sAnimFlags;
};

struct stInCarData {
    uint16_t sVehicleID;
    uint16_t sLeftRightKeys;
    uint16_t sUpDownKeys;
    uint16_t sKeys;
    float fQuaternion[4];
    float fPosition[3];
    float fMoveSpeed[3];
    float vehicleHealth;
    uint8_t bytePlayerHealth;
    uint8_t byteArmor;
    uint8_t byteCurrentWeapon;
    uint8_t byteSiren;
    uint8_t byteLandingGearState;
    uint16_t sTrailerID;
    union {
        uint16_t HydraThrustAngle[2];
        float fTrainSpeed;
    };
};

struct stPassengerData {
    uint16_t sVehicleID;
    uint8_t byteSeatID;
    uint8_t byteCurrentWeapon;
    uint8_t byteHealth;
    uint8_t byteArmor;
    uint16_t sLeftRightKeys;
    uint16_t sUpDownKeys;
    uint16_t sKeys;
    float fPosition[3];
};

struct stUnoccupiedData {
    uint16_t sVehicleID;
    uint8_t  byteSeatID;
    float    fRoll[3];
    float    fDirection[3];
    float    fPosition[3];
    float    fMoveSpeed[3];
    float    fTurnSpeed[3];
    float    fHealth;
};
#pragma pack(pop)

#ifndef RPC_EnterVehicle
#define RPC_EnterVehicle 26
#define RPC_ExitVehicle 154
#endif

#define ID_BULLET_SYNC 206

#pragma pack(push, 1)
struct stBulletData {
    uint8_t  byteType;
    uint16_t sTargetID;
    float    fOrigin[3];
    float    fTarget[3];
    float    fCenter[3];
    uint8_t  byteWeaponID;
};
#pragma pack(pop)

int troll_target_id = -1;

enum PacketEnumeration {
    ID_VEHICLE_SYNC = 200,
    ID_PLAYER_SYNC = 207,
    ID_UNOCCUPIED_SYNC = 209,
    ID_PASSENGER_SYNC = 211
};

enum PacketPriority {
    SYSTEM_PRIORITY,
    HIGH_PRIORITY,
    MEDIUM_PRIORITY,
    LOW_PRIORITY,
    NUMBER_OF_PRIORITIES
};

enum PacketReliability {
    UNRELIABLE = 6,
    UNRELIABLE_SEQUENCED,
    RELIABLE,
    RELIABLE_ORDERED,
    RELIABLE_SEQUENCED
};

typedef bool(__fastcall* RakClient_Send_t)(void* pThis, void* edx, BitStream* bitStream, int priority, int reliability, char orderingChannel);
typedef bool(__fastcall* RakClient_RPC_t)(void* pThis, void* edx, int* rpcId, BitStream* bitStream, int priority, int reliability, char orderingChannel, bool shiftTimestamp);

const char* keyNames[254] = { "None", "L-Mouse", "R-Mouse", "Cancel", "M-Mouse", "X1-Mouse", "X2-Mouse", "Unknown", "Back", "Tab", "Unknown", "Unknown", "Clear", "Enter", "Unknown", "Unknown", "Shift", "Ctrl", "Alt", "Pause", "Caps", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Esc", "Unknown", "Unknown", "Unknown", "Mode", "Space", "PgUp", "PgDn", "End", "Home", "Left", "Up", "Right", "Down", "Select", "Print", "Execute", "Print", "Ins", "Del", "Help", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "L-Win", "R-Win", "Apps", "Unknown", "Sleep", "Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Multiply", "Add", "Separator", "Subtract", "Decimal", "Divide", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "NumLock", "Scroll", "OEM =","OEM Massjou", "OEM Turo","OEM Tokya","OEM Omyu","OEM Jisho", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "L-Shift", "R-Shift", "L-Ctrl", "R-Ctrl", "L-Alt", "R-Alt" };

enum BindMode {
    BIND_TOGGLE = 0,
    BIND_HOLD = 1
};

struct Keybind;

std::vector<Keybind*> g_Keybinds;

void RegisterBind(Keybind* bind) {
    if (std::find(g_Keybinds.begin(), g_Keybinds.end(), bind) == g_Keybinds.end()) {
        g_Keybinds.push_back(bind);
    }
}

struct Keybind {
    const char* name;
    bool* value;
    int key;
    int mode;
    bool waiting;
    float anim_val;
    std::function<void(bool)> callback;

    Keybind(const char* n, bool* v, std::function<void(bool)> cb = nullptr)
        : name(n), value(v), key(0), mode(BIND_TOGGLE), waiting(false), anim_val(0.0f), callback(cb) {
        RegisterBind(this);
    }
};

bool enable_cef_fast_mine = false;
Keybind bind_cef_fast_mine("CEF Fast Mine", &enable_cef_fast_mine);

void ToggleGymBot(bool state);

bool enable_gym_bot = false;
int gym_mode = 0;
int gym_delay = 60;
Keybind bind_gym_bot("Gym Bot", &enable_gym_bot, ToggleGymBot);

void GymBotWorker() {
    while (enable_gym_bot) {
        if (gym_mode == 0) {
            keybd_event(0x41, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            keybd_event(0x41, 0, KEYEVENTF_KEYUP, 0);

            std::this_thread::sleep_for(std::chrono::milliseconds(gym_delay));

            keybd_event(0x44, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            keybd_event(0x44, 0, KEYEVENTF_KEYUP, 0);

            std::this_thread::sleep_for(std::chrono::milliseconds(gym_delay));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

struct AppLog {
    ImGuiTextBuffer     Buf;
    ImGuiTextFilter     Filter;
    ImVector<int>       LineOffsets;
    bool                AutoScroll;

    AppLog() {
        AutoScroll = true;
        Clear();
    }

    void Clear() {
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void AddLog(const char* fmt, ...) IM_FMTARGS(2) {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    void Draw(const char* title, bool* p_open = NULL) {
        if (!ImGui::Begin(title, p_open)) {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear")) Clear();
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ImGui::SameLine();
        Filter.Draw("Filter", -100.0f);

        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        if (copy) ImGui::LogToClipboard();

        if (Filter.IsActive()) {
            const char* buf_begin = Buf.begin();
            const char* line = buf_begin;
            for (int line_no = 0; line != NULL; line_no++) {
                const char* line_end = (line_no < LineOffsets.Size) ? buf_begin + LineOffsets[line_no] : NULL;
                if (Filter.PassFilter(line, line_end))
                    ImGui::TextUnformatted(line, line_end);
                line = line_end && line_end[1] ? line_end + 1 : NULL;
            }
        }
        else {
            ImGui::TextUnformatted(Buf.begin());
        }

        if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }
};

AppLog g_CefConsole;
//bool show_cef_console = false;
//Keybind bind_cef_console("Show CEF Console", &show_cef_console);

struct cef_string_utf8_t {
    char* str;
    size_t length;
    void (*dtor)(char* str);
};

typedef int(__cdecl* cef_string_utf16_to_utf8_t)(const char16_t* src, size_t src_len, cef_string_utf8_t* output);
cef_string_utf16_to_utf8_t o_cef_string_utf16_to_utf8 = nullptr;

void AutoPressWorker(int keyCode, int durationMs, int type) {
    BYTE scanCode = MapVirtualKey(keyCode, 0);

    keybd_event(keyCode, scanCode, KEYEVENTF_KEYUP, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::this_thread::sleep_for(std::chrono::milliseconds(100 + (rand() % 100)));

    if (type == 0) {
        int totalHoldTime = durationMs + 300;

        keybd_event(keyCode, scanCode, 0, 0);

        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() >= totalHoldTime)
                break;

            keybd_event(keyCode, scanCode, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        keybd_event(keyCode, scanCode, KEYEVENTF_KEYUP, 0);
    }

    else if (type == 1) {
        keybd_event(keyCode, scanCode, 0, 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(60 + (rand() % 30)));

        keybd_event(keyCode, scanCode, KEYEVENTF_KEYUP, 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        printf("[BOT] TAP key %d done.\n", keyCode);
    }
}

void OpenConsole() {
    AllocConsole();

    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX fontInfo;
    fontInfo.cbSize = sizeof(CONSOLE_FONT_INFOEX);
    GetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
    wcscpy_s(fontInfo.FaceName, L"Lucida Console");
    fontInfo.dwFontSize.Y = 14;
    SetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);

    SetConsoleTitleA("Telegram: @meltedhack");
    printf("[SYSTEM] Console allocated (CP1251).\n");
}

float mine_hold_time = 1.0f;

bool enable_cef_spoof_admin = false;
int cef_admin_level = 7;

bool enable_cef_spoofing = false;

char spoof_nickname[32] = "Admin_User";
int spoof_id = 1337;
int spoof_money = 100000000;
char spoof_bank[32] = "50000000";
int spoof_lvl = 100;
int spoof_exp = 10;
int spoof_admin_lvl = 7;
int spoof_vip = 1;
int spoof_wanted = 0;
int spoof_fraction = 1;
int spoof_rank = 15;
bool spoof_all_licenses = true;
bool spoof_max_skills = true;
bool spoof_tab_admin = true;

void custom_cef_string_dtor(char* str) {
    if (str) delete[] str;
}

void ModifyJsonValue(std::string& json, const std::string& key, int newValue) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos != std::string::npos) {
        pos += searchKey.length();
        while (pos < json.length() && isspace(json[pos])) pos++;

        size_t endPos = pos;
        while (endPos < json.length() && (isdigit(json[endPos]) || json[endPos] == '-')) {
            endPos++;
        }

        std::string newValStr = std::to_string(newValue);
        json.replace(pos, endPos - pos, newValStr);
    }
}

void ReplaceJsonInt(std::string& json, const std::string& key, int newValue, size_t start_pos = 0) {
    size_t keyPos = json.find("\"" + key + "\":", start_pos);
    if (keyPos != std::string::npos) {
        size_t valPos = keyPos + key.length() + 3;
        size_t endPos = valPos;
        while (endPos < json.length() && (isdigit(json[endPos]) || json[endPos] == '-' || json[endPos] == '.')) {
            endPos++;
        }
        if (endPos > valPos) {
            json.replace(valPos, endPos - valPos, std::to_string(newValue));
        }
    }
}

void ReplaceJsonString(std::string& json, const std::string& key, const std::string& newValue, size_t start_pos = 0) {
    size_t keyPos = json.find("\"" + key + "\":", start_pos);
    if (keyPos != std::string::npos) {
        size_t valStart = json.find("\"", keyPos + key.length() + 3);
        if (valStart != std::string::npos) {
            valStart++;
            size_t valEnd = json.find("\"", valStart);
            if (valEnd != std::string::npos) {
                json.replace(valStart, valEnd - valStart, newValue);
            }
        }
    }
}

int GetLocalPlayerIdR3() {
    static DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
    if (!sampDll) return -1;
    DWORD* info = (DWORD*)(sampDll + 0x26E8DC);
    if (!info || !*info) return -1;
    DWORD* pools = (DWORD*)(*info + 0x3DE);
    if (!pools || !*pools) return -1;
    DWORD* playerPool = (DWORD*)(*pools + 0x8);
    if (!playerPool || !*playerPool) return -1;
    return (int)*(unsigned short*)(*playerPool + 0xF1C);
}

int __cdecl hk_cef_string_utf16_to_utf8(const char16_t* src, size_t src_len, cef_string_utf8_t* output) {
    int result = o_cef_string_utf16_to_utf8(src, src_len, output);

    if (result && output && output->str && output->length > 0) {

        if (output->str) {
            printf("[CEF] %s\n", Utf8ToAnsi(output->str).c_str());
        }

        std::string strData = output->str;
        bool modified = false;

        if (enable_cef_spoof_admin) {
            if (strData.find("cef_esc_show") != std::string::npos) {
                ReplaceJsonInt(strData, "adminLevel", spoof_admin_lvl);
                modified = true;
            }
        }

        if (enable_cef_spoofing) {

            if (strData.find("update_account_stats") != std::string::npos) {
                size_t genPos = strData.find("\"general\":");
                if (genPos != std::string::npos) {
                    ReplaceJsonInt(strData, "adminLevel", spoof_admin_lvl, genPos);
                    ReplaceJsonInt(strData, "level", spoof_lvl, genPos);
                    ReplaceJsonInt(strData, "vip", spoof_vip, genPos);
                    ReplaceJsonInt(strData, "cashMoney", spoof_money, genPos);
                    ReplaceJsonString(strData, "bankMoney", std::string(spoof_bank), genPos);
                    ReplaceJsonInt(strData, "id", spoof_id, genPos);
                    ReplaceJsonInt(strData, "fractionId", spoof_fraction, genPos);
                    ReplaceJsonInt(strData, "fractionRankId", spoof_rank, genPos);
                    ReplaceJsonInt(strData, "wantedLevel", spoof_wanted, genPos);

                    char nickFull[64];
                    sprintf_s(nickFull, "%s [%d]", spoof_nickname, spoof_id);
                    ReplaceJsonString(strData, "nickname", std::string(nickFull), genPos);
                }

                if (spoof_all_licenses) {
                    size_t licPos = strData.find("\"licenses\":");
                    if (licPos != std::string::npos) {
                        size_t endLic = strData.find("}", licPos);
                        for (size_t i = licPos; i < endLic; i++) {
                            if (strData[i] == ':' && strData[i + 1] == '0') strData[i + 1] = '1';
                        }
                    }
                }

                if (spoof_max_skills) {
                    size_t skillPos = strData.find("\"characterSkills\":");
                    if (skillPos != std::string::npos) {
                        size_t endSkill = strData.find("}", skillPos);
                        for (size_t i = skillPos; i < endSkill; i++) {
                            if (strData[i] == ':' && isdigit(strData[i + 1])) { }
                        }
                        ReplaceJsonInt(strData, "power", 100, 0);
                        ReplaceJsonInt(strData, "stamina", 100, 0);
                    }
                }

                modified = true;
                printf("[CEF SPOOF] Spoofed account stats!\n");
            }

            if (spoof_tab_admin && strData.find("players_list_update") != std::string::npos) {
                int localId = GetLocalPlayerIdR3();
                std::string idKey = "\"id\":" + std::to_string(localId);

                size_t myEntry = strData.find(idKey);
                if (myEntry != std::string::npos) {
                    size_t startObj = strData.rfind("{", myEntry);
                    size_t endObj = strData.find("}", myEntry);

                    if (startObj != std::string::npos && endObj != std::string::npos) {
                        ReplaceJsonInt(strData, "adminLevel", spoof_admin_lvl, startObj);
                        ReplaceJsonInt(strData, "level", spoof_lvl, startObj);
                        ReplaceJsonInt(strData, "isLeader", 1, startObj);
                        ReplaceJsonString(strData, "nickname", std::string(spoof_nickname), startObj);
                        modified = true;
                        printf("[CEF SPOOF] Spoofed TAB entry for ID %d\n", localId);
                    }
                }
            }
        }

        if (strstr(output->str, "cef_quick_time_events_show")) {
            int keyCode = 0;
            int typeVal = 0;
            int serverTime = 0;
            char* keyTag = strstr(output->str, "\"keyCode\":");
            if (keyTag) { sscanf_s(keyTag + 10, "%d", &keyCode); }
            char* typeTag = strstr(output->str, "\"type\":");
            if (typeTag) { sscanf_s(typeTag + 7, "%d", &typeVal); }
            char* timeTag = strstr(output->str, "\"time\":");
            if (timeTag) { sscanf_s(timeTag + 7, "%d", &serverTime); }

            if (enable_cef_fast_mine && keyCode > 0) {
                int finalDuration = (serverTime > 0) ? serverTime : (int)(mine_hold_time * 1000.0f);
                std::thread(AutoPressWorker, keyCode, finalDuration, typeVal).detach();
                printf("[BOT] Auto logic started.\n");
            }
        }

        if (modified) {
            if (output->dtor && output->str) output->dtor(output->str);
            char* newBuffer = new char[strData.size() + 1];
            memcpy(newBuffer, strData.c_str(), strData.size() + 1);
            output->str = newBuffer;
            output->length = strData.size();
            output->dtor = custom_cef_string_dtor;
        }
    }
    return result;
}

void InitStringHook() {
    HMODULE hCef = GetModuleHandleA("libcef.dll");
    if (hCef) {
        void* pFunc = (void*)GetProcAddress(hCef, "cef_string_utf16_to_utf8");
        if (pFunc) {
            MH_CreateHook(pFunc, &hk_cef_string_utf16_to_utf8, (void**)&o_cef_string_utf16_to_utf8);
            MH_EnableHook(pFunc);
            printf("[SYSTEM] String Sniffer activated. Waiting for events...\n");
        }
        else {
            printf("[ERROR] Failed to find string export.\n");
        }
    }
}

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void ToggleOpenDoors(bool state);
void ToggleAutoC(bool state);
void ToggleAllSkills(bool state);

bool init = false;
bool show_menu = false;
int active_tab = 0;

char g_Username[64] = "User";
char g_Expiry[64] = "Unknown";

Keybind* current_bind_edit = nullptr;
bool show_bind_window = false;

bool play_intro = true;
double intro_start_time = 0.0;

bool is_teleporting = false;
CVector tp_target_pos = { 0, 0, 0 };
float tp_speed_step = 30.0f;
bool enable_map_teleport = false;
Keybind bind_map_teleport("Teleport to Waypoint", &enable_map_teleport);
bool is_metka_found = false;

bool enable_tp_exploit = false;
Keybind bind_tp_exploit("TP to 0.0.0 (Passenger)", &enable_tp_exploit);

bool g_GhostIsAttached = false;
int g_GhostAttachedID = -1;
DWORD g_GhostLastUpdate = 0;

bool enable_esp_vis_check = false;
float col_esp_vis[3] = { 0.0f, 1.0f, 0.0f };
float col_esp_occ[3] = { 1.0f, 0.0f, 0.0f };

bool enable_obj_surf = false;
Keybind bind_obj_surf("Ghost Object Surf", &enable_obj_surf);

int LastObj = -1;
float LastObjDistance = 100.0f;
CVector LastObjPos = { 0, 0, 0 };
bool HasActiveAnchor = false;

bool enable_tracers = false;
Keybind bind_tracers("Bullet Tracers", &enable_tracers);
float col_tracer[3] = { 0.0f, 1.0f, 1.0f };
float tracer_duration = 2.0f;

struct BulletTraceInfo {
    CVector start;
    CVector end;
    float timeCreated;
};
std::vector<BulletTraceInfo> g_BulletTracers;

void AddBulletTrace(CVector start, CVector end) {
    BulletTraceInfo trace;
    trace.start = start;
    trace.end = end;
    trace.timeCreated = (float)ImGui::GetTime();

    g_BulletTracers.push_back(trace);
}

bool enable_arrows = false;
Keybind bind_arrows("Player Arrows", &enable_arrows);
float arrows_radius = 150.0f;
float arrows_size = 15.0f;
float col_arrows[3] = { 1.0f, 0.0f, 0.0f };

bool enable_auto_handbrake = false;
Keybind bind_auto_handbrake("Auto Handbrake", &enable_auto_handbrake);

bool enable_nocol_buildings = false;
Keybind bind_nocol_buildings("No Building Col", &enable_nocol_buildings);

bool enable_esp_glow = false;
Keybind bind_esp_glow("ESP Glow", &enable_esp_glow);
float glow_intensity = 0.7f;

bool enable_box = false;
Keybind bind_box("ESP Box", &enable_box);

bool enable_lines = false;
Keybind bind_lines("ESP Lines", &enable_lines);

bool enable_name = false;
Keybind bind_name("ESP Name", &enable_name);

bool enable_trail = false;
Keybind bind_trail("Movement Trail", &enable_trail);
float trail_max_points = 40.0f;
float col_trail[3] = { 0.0f, 0.5f, 1.0f };
std::vector<CVector> trail_history;

float GetDistance3D(CVector a, CVector b) {
    return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2) + pow(a.z - b.z, 2));
}

bool enable_skeleton = false;
Keybind bind_skeleton("Skeleton", &enable_skeleton);
float col_skeleton[3] = { 1.0f, 1.0f, 1.0f };
float skeleton_thickness = 1.0f;

bool enable_id_tags = false;
Keybind bind_id_tags("ESP ID Tags", &enable_id_tags);

bool enable_health = false;
Keybind bind_health("Health Bar", &enable_health);

bool enable_armor = false;
Keybind bind_armor("Armor Bar", &enable_armor);

bool enable_text_hp_arm = true;
Keybind bind_text_hp_arm("HP/Arm Text", &enable_text_hp_arm);

bool enable_weapon = false;
Keybind bind_weapon("ESP Weapon", &enable_weapon);

bool enable_miner_wh = false;
Keybind bind_miner_wh("Miner WH", &enable_miner_wh);

bool enable_event_mode = false;
Keybind bind_event_mode("ESP Magic Battle", &enable_event_mode);

bool enable_frozen_lands = false;
Keybind bind_frozen_lands("ESP Frozen Lands", &enable_frozen_lands);

bool enable_chams = false;
Keybind bind_chams("Chams", &enable_chams);

bool chams_wireframe = false;
Keybind bind_chams_wireframe("Chams Wireframe", &chams_wireframe);

bool enable_voice_listen = false;
Keybind bind_voice_listen("Voice Spy", &enable_voice_listen);
int voice_target_id = -1;

int iCustomSkinID = 0;

struct AccSlotInfo {
    bool enabled;
    int model;
    int bone;
    float pos[3];
    float rot[3];
    float scale[3];
    int color1;
    int color2;
    const char* name;
};

AccSlotInfo g_AccSlots[5] = {
    { false, 18911, 2,  {0,0,0}, {0,0,0}, {1,1,1}, 0, 0, "HEAD (Голова)" },
    { false, 19036, 2,  {0.06f,0.01f,0.0f}, {0,90,90}, {1,1,1}, 0, 0, "FACE (Лицо)" },
    { false, 19472, 1,  {0,0,0}, {0,0,0}, {1,1,1}, 0, 0, "TORSO (Торс)" },
    { false, 19046, 6,  {0,0,0}, {0,0,0}, {1,1,1}, 0, 0, "HANDS (Руки)" },
    { false, 11745, 1,  {0,-0.15f,0}, {0,0,0}, {1,1,1}, 0, 0, "BACK (Спина)" }
};

int g_SelectedAccSlot = 0;
typedef void(__thiscall* UpdateAttachedObject_t)(void* pLocalPlayer, int index);

void ApplyAccessoryLocal(int slotID) {
    if (slotID < 0 || slotID >= 10) return;

    DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
    if (!sampDll) return;

    DWORD* pSampInfo = (DWORD*)(sampDll + 0x26E8DC);
    if (!pSampInfo || !*pSampInfo) return;

    DWORD* pPools = (DWORD*)(*pSampInfo + 0x3DE);
    if (!pPools || !*pPools) return;

    DWORD* pPlayerPool = (DWORD*)(*pPools + 0x08);
    if (!pPlayerPool || !*pPlayerPool) return;

    void* pLocalPlayer = *(void**)(*pPlayerPool + 0x18);
    if (!pLocalPlayer) return;

    struct StAttachedObject {
        int iModel;
        int iBone;
        float vOffset[3];
        float vRot[3];
        float vScale[3];
        DWORD dwMaterialColor1;
        DWORD dwMaterialColor2;
    };

    AccSlotInfo& slot = g_AccSlots[slotID];

    DWORD dataOffset = 0x14 + (slotID * sizeof(StAttachedObject));
    StAttachedObject* pObj = (StAttachedObject*)((DWORD)pLocalPlayer + dataOffset);

    int* pSlotEnabled = (int*)((DWORD)pLocalPlayer + 0x21C + (slotID * 4));

    if (slot.enabled) {
        pObj->iModel = slot.model;
        pObj->iBone = slot.bone;

        pObj->vOffset[0] = slot.pos[0];
        pObj->vOffset[1] = slot.pos[1];
        pObj->vOffset[2] = slot.pos[2];

        pObj->vRot[0] = slot.rot[0];
        pObj->vRot[1] = slot.rot[1];
        pObj->vRot[2] = slot.rot[2];

        pObj->vScale[0] = slot.scale[0];
        pObj->vScale[1] = slot.scale[1];
        pObj->vScale[2] = slot.scale[2];

        pObj->dwMaterialColor1 = slot.color1;
        pObj->dwMaterialColor2 = slot.color2;

        *pSlotEnabled = 1;
    }
    else {
        pObj->iModel = -1;
        *pSlotEnabled = 0;
    }

    UpdateAttachedObject_t UpdateFunc = (UpdateAttachedObject_t)(sampDll + 0x3A80);
    UpdateFunc(pLocalPlayer, slotID);
}

bool enable_mouse_aim = false;
Keybind bind_mouse_aim("Wand Aim", &enable_mouse_aim);

bool real_crosshair_fov = false;

float mouse_fov = 50.0f;
float mouse_smooth = 1.0f;
bool mouse_check_vis = true;

float mouse_offset_x = 60.0f;
float mouse_offset_y = -115.0f;
float mouse_predict = 1.1f;
int mouse_aim_bone = 0;

float col_mouse_fov[3] = { 1.0f, 0.0f, 0.0f };
bool draw_mouse_fov = false;
Keybind bind_draw_mouse_fov("Draw Wand FOV", &draw_mouse_fov);

float mouse_residue_x = 0.0f;
float mouse_residue_y = 0.0f;

int mouse_locked_target_id = -1;

bool silent_enabled = false;
Keybind bind_silent("Silent Aim", &silent_enabled);

bool silent_magic = false;
Keybind bind_silent_magic("Magic Bullet", &silent_magic);

bool draw_fov = true;
Keybind bind_draw_fov("Draw FOV", &draw_fov);

bool silent_ignore_walls = false;
Keybind bind_silent_walls("Ignore Walls", &silent_ignore_walls);

bool silent_ignore_team = false;
Keybind bind_silent_team("Ignore Team", &silent_ignore_team);

bool silent_ignore_skin = false;
Keybind bind_silent_skin("Ignore Skin", &silent_ignore_skin);

bool silent_random_shot = false;
Keybind bind_silent_random("Random Shot", &silent_random_shot);

float silent_fov = 150.0f;
float silent_max_dist = 300.0f;
float silent_random_spread = 0.0f;

bool enable_faction_bypass = false;
Keybind bind_faction_bypass("Faction Drive Bypass", &enable_faction_bypass);

bool enable_drift = false;
Keybind bind_drift("Drift Mod", &enable_drift);
float drift_amount = 0.35f;

bool enable_nitro = false;
Keybind bind_nitro("Nitro", &enable_nitro);

bool enable_car_godmode = false;
Keybind bind_car_godmode("Car Godmode", &enable_car_godmode);

bool enable_speedhack = false;
Keybind bind_speedhack("Speedhack", &enable_speedhack);
float speedhack_val = 1.0f;
int speedhack_act_key = VK_LMENU;
bool speedhack_waiting = false;

bool enable_car_jump = false;
Keybind bind_car_jump("Car Jump", &enable_car_jump);
float car_jump_force = 0.3f;
int car_jump_act_key = VK_LSHIFT;
bool car_jump_waiting = false;

bool enable_open_doors = false;
Keybind bind_open_doors("Open Doors", &enable_open_doors, ToggleOpenDoors);

bool enable_autopc = false;
Keybind bind_autopc("Auto +C", &enable_autopc, ToggleAutoC);

bool enable_all_skills = false;
Keybind bind_all_skills("All Skills", &enable_all_skills, ToggleAllSkills);

bool enable_airbreak = false;
Keybind bind_airbreak("Air Break", &enable_airbreak);
float airbreak_speed = 0.5f;

bool enable_invis = false;
Keybind bind_invis("Invisible", &enable_invis);
float invis_z = 50.0f;

bool enable_rvanka = false;
Keybind bind_rvanka("Rvanka Hack", &enable_rvanka);

bool enable_seat_tp = false;
Keybind bind_seat_tp("Seat Teleport", &enable_seat_tp);

float seat_tp_fov = 100.0f;
int seat_tp_key = VK_LMENU;
bool draw_seat_tp_fov = true;
float col_seat_tp_fov[3] = { 0.0f, 1.0f, 0.0f };

struct RwV3d {
    float x, y, z;
};

struct RwMatrix {
    RwV3d right;
    uint32_t flags;
    RwV3d up;
    uint32_t pad1;
    RwV3d at;
    uint32_t pad2;
    RwV3d pos;
    uint32_t pad3;
};

float ab_speed_val = 0.5f;
float ab_accel = 0.0f;
bool ab_moving = false;

void ForceLocalPosition(float x, float y, float z) {
    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (!pPed || !*pPed) return;

    float* speed = (float*)((*pPed) + 0x44);
    speed[0] = 0.0f; speed[1] = 0.0f; speed[2] = 0.0f;

    DWORD ptrMatrix = *(DWORD*)((*pPed) + 0x14);
    if (ptrMatrix) {
        *(float*)(ptrMatrix + 0x30) = x;
        *(float*)(ptrMatrix + 0x34) = y;
        *(float*)(ptrMatrix + 0x38) = z;
    }
}

typedef void(__thiscall* ApplyMoveSpeed_t)(void* pEntity);
ApplyMoveSpeed_t oApplyMoveSpeed = NULL;

void __fastcall hkApplyMoveSpeed(void* pEntity, void* edx) {
    if (enable_airbreak) {
        void* pLocalPed = *(void**)0xB6F5F0;
        void* pLocalVeh = *(void**)0xBA18FC;
        if (pEntity == pLocalPed || (pLocalVeh && pEntity == pLocalVeh)) {
            return;
        }
    }
    oApplyMoveSpeed(pEntity);
}

int rvanka_mode = 0;
float rvanka_speed = 50.0f;
float rvanka_fov = 100.0f;
int rvanka_key = VK_LMENU;
bool rvanka_draw_fov = true;
float col_rvanka_fov[3] = { 1.0f, 0.0f, 0.0f };

bool IsPointInFOV(float screenX, float screenY, float fovRadius) {
    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2);
    float dx = screenX - center.x;
    float dy = screenY - center.y;
    return (dx * dx + dy * dy) <= (fovRadius * fovRadius);
}

bool enable_anim_breaker = true;
Keybind bind_anim_breaker("Anim Breaker", &enable_anim_breaker);
int anim_break_key_default = 0x58;

bool enable_auto_peek = false;
Keybind bind_auto_peek("Auto Peek", &enable_auto_peek);
bool is_peeking = false;
CVector peek_start_pos = { 0, 0, 0 };
float col_auto_peek[3] = { 0.0f, 1.0f, 1.0f };

bool enable_nocol = false;
Keybind bind_nocol("No Collision", &enable_nocol);

typedef void(__thiscall* ProcessCollision_t)(void* pThis);
ProcessCollision_t oProcessCollision = NULL;

void __fastcall hkProcessCollision(void* pThis, void* edx) {
    void* pLocalPed = *(void**)0xB6F5F0;
    void* pLocalVeh = *(void**)0xBA18FC;

    if (enable_nocol) {
        if (pThis == pLocalPed || (pLocalVeh && pThis == pLocalVeh)) {
            return;
        }
    }
    oProcessCollision(pThis);
}

typedef bool(__thiscall* RakPeerSend_t)(void* pThis, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel);
RakPeerSend_t oRakPeerSend = NULL;

int style_box = 0;
int style_health = 1;
int style_lines = 0;
float col_esp_box[3] = { 1.0f, 1.0f, 1.0f };
float col_esp_lines[3] = { 1.0f, 1.0f, 1.0f };
float col_esp_names[3] = { 1.0f, 1.0f, 1.0f };
float col_aim_fov[3] = { 1.0f, 1.0f, 1.0f };
float col_esp_fill_top[3] = { 1.0f, 1.0f, 1.0f };
float col_esp_fill_bot[3] = { 0.0f, 0.0f, 0.0f };
float col_esp_armor[3] = { 0.0f, 0.5f, 1.0f };
float menu_size[2] = { 838.0f, 768.0f };

int wm_corner = 1;
float wm_padding_x = 20.0f;
float wm_padding_y = 20.0f;

float col_hp_grad_top[3] = { 0.8f, 0.0f, 1.0f };
float col_hp_grad_bot[3] = { 0.0f, 0.4f, 1.0f };

float sidebar_anim_y = 0.0f;
float sidebar_target_y = 0.0f;
float sidebar_anim_x = 0.0f;
bool sidebar_init = false;

float tab_text_anim[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

int objectId[4] = { 7234, 7235, 7236, 7237 };
const char* objectName[4] = { "Coal", "Iron", "Gold", "Diamond" };
float col_miner[4][3] = {
    { 0.62f, 0.20f, 0.17f },
    { 0.78f, 0.78f, 0.78f },
    { 1.00f, 0.93f, 0.00f },
    { 0.00f, 1.00f, 1.00f }
};

float col_chams_vis[3] = { 0.0f, 1.0f, 0.0f };
float col_chams_inv[3] = { 1.0f, 0.0f, 0.0f };

float chams_width = 1.0f;

bool wm_settings_open = false;
float wm_settings_anim = 0.0f;
ImVec2 wm_pos_stored = ImVec2(20, 20);
bool wm_first_launch = true;

bool wm_show_time = true;
bool wm_show_fps = true;
bool wm_show_link = true;
ImVec2 wm_pos = ImVec2(50, 50);

IDirect3DTexture9* tex_vis = nullptr;
IDirect3DTexture9* tex_inv = nullptr;

typedef HRESULT(__stdcall* DrawIndexedPrimitive_t)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
DrawIndexedPrimitive_t oDrawIndexedPrimitive = NULL;

WNDPROC oWndProc;
HWND window = NULL;

ImFont* font_main = nullptr;
ImFont* font_logo = nullptr;
ImFont* font_icons = nullptr;

typedef HRESULT(__stdcall* Reset_t)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
Reset_t oReset = NULL;

typedef long(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
EndScene_t oEndScene = NULL;

typedef BOOL(WINAPI* SetCursorPos_t)(int, int);
SetCursorPos_t oSetCursorPos = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Vector2 { float x, y; };

Vector2 CalcScreenCoords(float x, float y, float z) {
    const float* m = reinterpret_cast<float*>(0xB6FA2C);
    unsigned long dwLenX = *reinterpret_cast<unsigned long*>(0xC17044);
    unsigned long dwLenY = *reinterpret_cast<unsigned long*>(0xC17048);

    float rw_x = (z * m[8]) + (y * m[4]) + (x * m[0]) + m[12];
    float rw_y = (z * m[9]) + (y * m[5]) + (x * m[1]) + m[13];
    float rw_z = (z * m[10]) + (y * m[6]) + (x * m[2]) + m[14];

    if (rw_z <= 0.01f) return { -1, -1 };

    float fRecip = 1.0f / rw_z;
    float res_x = rw_x * fRecip * dwLenX;
    float res_y = rw_y * fRecip * dwLenY;

    return { res_x, res_y };
}

float GetDistance2D(Vector2 a, Vector2 b) {
    return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2));
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float GetAnim(const char* label, bool condition, float speed) {
    ImGuiID id = ImGui::GetID(label);
    float current = anims[id];
    float target = condition ? 1.0f : 0.0f;
    float delta = ImGui::GetIO().DeltaTime * speed;

    if (current < target) {
        current += delta;
        if (current > target) current = target;
    }
    else if (current > target) {
        current -= delta;
        if (current < target) current = target;
    }
    anims[id] = current;
    return current;
}

ImU32 ColorAlpha(ImVec4 col, float alpha_mult) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, col.w * alpha_mult));
}

void PatchBytes(void* address, const void* data, size_t size) {
    DWORD oldProtect;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, data, size);
    VirtualProtect(address, size, oldProtect, &oldProtect);
}

template<typename T>
void WriteMem(DWORD address, T value) {
    DWORD oldProtect;
    VirtualProtect((LPVOID)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
    *(T*)address = value;
    VirtualProtect((LPVOID)address, sizeof(T), oldProtect, &oldProtect);
}

void InstallJmpHook(DWORD address, void* hookFunc, size_t size) {
    DWORD oldProtect;
    VirtualProtect((void*)address, size, PAGE_EXECUTE_READWRITE, &oldProtect);

    memset((void*)address, 0x90, size);

    *(BYTE*)address = 0xE9;

    DWORD relativeAddr = (DWORD)hookFunc - address - 5;
    *(DWORD*)(address + 1) = relativeAddr;

    VirtualProtect((void*)address, size, oldProtect, &oldProtect);
}

bool GetScreenPosForArrow(CVector worldPos, ImVec2& outPos, ImVec2 center) {
    const float* m = reinterpret_cast<float*>(0xB6FA2C);
    unsigned long dwLenX = *reinterpret_cast<unsigned long*>(0xC17044);
    unsigned long dwLenY = *reinterpret_cast<unsigned long*>(0xC17048);

    float z = (worldPos.z * m[10]) + (worldPos.y * m[6]) + (worldPos.x * m[2]) + m[14];
    float x = (worldPos.z * m[8]) + (worldPos.y * m[4]) + (worldPos.x * m[0]) + m[12];
    float y = (worldPos.z * m[9]) + (worldPos.y * m[5]) + (worldPos.x * m[1]) + m[13];

    if (z <= 0.01f) {
        x *= -100000.0f;
        y *= -100000.0f;
    }
    else {
        float fRecip = 1.0f / z;
        x *= fRecip * dwLenX;
        y *= fRecip * dwLenY;
    }

    outPos.x = x;
    outPos.y = y;
    return z > 0.01f;
}

void ToggleOpenDoors(bool state) {
    static DWORD samp_dll = 0;
    if (!samp_dll) samp_dll = (DWORD)GetModuleHandleA("samp.dll");
    if (!samp_dll) return;

    if (state) {
        PatchBytes((void*)0x63E40F, "\xEB\xBA", 2);
    }
    else {
        PatchBytes((void*)0x63E40F, "\x6A\x1C", 2);
    }

    if (state) {
        PatchBytes((void*)(samp_dll + 0xA2FE8), "\x90\x90\x90\x90\x90\x90", 6);
        PatchBytes((void*)(samp_dll + 0xA2FF2), "\x90\x90\x90\x90\x90\x90", 6);
        PatchBytes((void*)(samp_dll + 0xA300A), "\x90\x90\x90\x90\x90\x90", 6);
        PatchBytes((void*)(samp_dll + 0xA3021), "\x90\x90", 2);
        PatchBytes((void*)(samp_dll + 0xA302C), "\x90\x90", 2);
        PatchBytes((void*)(samp_dll + 0xA3035), "\xEB", 1);
        PatchBytes((void*)(samp_dll + 0xA306F), "\xEB", 1);
    }
    else {
        PatchBytes((void*)(samp_dll + 0xA2FE8), "\x0F\x84\xAF\x00\x00\x00", 6);
        PatchBytes((void*)(samp_dll + 0xA2FF2), "\x0F\x83\xA5\x00\x00\x00", 6);
        PatchBytes((void*)(samp_dll + 0xA300A), "\x0F\x84\x8D\x00\x00\x00", 6);
        PatchBytes((void*)(samp_dll + 0xA3021), "\x75\x7A", 2);
        PatchBytes((void*)(samp_dll + 0xA302C), "\x74\x6F", 2);
        PatchBytes((void*)(samp_dll + 0xA3035), "\x74", 1);
        PatchBytes((void*)(samp_dll + 0xA306F), "\x74", 1);
    }
}

void ToggleGymBot(bool state) {
    if (state) {
        std::thread(GymBotWorker).detach();
        printf("[BOT] Gym Bot Started.\n");
    }
    else {
        printf("[BOT] Gym Bot Stopped.\n");
    }
}

void ChangeSkin(int modelID) {
    if (modelID < 0 || modelID > 30000) return;

    ((void(__cdecl*)(int, int))0x4087E0)(modelID, 2);
    ((void(__cdecl*)(bool))0x40EA10)(false);

    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (pPed && *pPed) {
        ((void(__thiscall*)(void*, int))0x5E4880)((void*)*pPed, modelID);
    }
}

void ToggleAutoC(bool state) {
    if (state) {
        PatchBytes((void*)0x62AC47, "\x90\x90", 2);
        PatchBytes((void*)0x62508D, "\x90\x90", 2);
        PatchBytes((void*)0x62509B, "\x90\x90", 2);
    }
    else {
        PatchBytes((void*)0x62AC47, "\x75\x50", 2);
        PatchBytes((void*)0x62508D, "\x75\x35", 2);
        PatchBytes((void*)0x62509B, "\x74\x27", 2);
    }
}

void ToggleAllSkills(bool state) {
    if (state) {
        PatchBytes((void*)0x5E3BE0, "\xEB\xD2\x90", 3);
        PatchBytes((void*)0x61E4B6, "\x90\x90", 2);
    }
    else {
        PatchBytes((void*)0x5E3BE0, "\x5F\x32\xC0", 3);
        PatchBytes((void*)0x61E4B6, "\x74\x2C", 2);
    }
}

void airbreak_teleport(float x, float y, float z) {
    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (!pPed || !*pPed) return;

    DWORD pVehicle = *(DWORD*)0xBA18FC;

    DWORD* pEntity = pVehicle ? (DWORD*)pVehicle : pPed;

    DWORD* pMatrix = (DWORD*)((*pEntity) + 0x14);
    if (!pMatrix || !*pMatrix) return;

    *(float*)((*pMatrix) + 0x30) = x;
    *(float*)((*pMatrix) + 0x34) = y;
    *(float*)((*pMatrix) + 0x38) = z;

    if (pVehicle) {
        float* speed = (float*)(pVehicle + 68);
        speed[0] = 0.0f; speed[1] = 0.0f; speed[2] = 0.0f;
    }
}

void CalculateAimAngles(CVector* src, CVector* dst, float* yaw, float* pitch) {
    float dx = dst->x - src->x;
    float dy = dst->y - src->y;
    float dz = dst->z - src->z;
    float dist = sqrt(dx * dx + dy * dy);

    *yaw = -atan2(dx, dy);
    *pitch = atan2(dz, dist);
}

float NormalizeAngle(float angle) {
    while (angle < -M_PI) angle += 2 * M_PI;
    while (angle > M_PI) angle -= 2 * M_PI;
    return angle;
}


void RunTrollLogic(int targetId) {
    DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
    if (!sampDll) return;

    DWORD* pSampInfo = (DWORD*)(sampDll + 0x26E8DC);
    if (!pSampInfo || !*pSampInfo) return;

    void* pRakClient = *(void**)(*pSampInfo + 0x2C);
    if (!pRakClient) return;

    void** vTable = *(void***)pRakClient;
    RakClient_Send_t pSend = (RakClient_Send_t)vTable[6];
    RakClient_RPC_t pRPC = (RakClient_RPC_t)vTable[25];

    if (!RefNetGame() || !RefNetGame()->GetPlayerPool()) return;
    if (!RefNetGame()->GetPlayerPool()->IsConnected(targetId)) return;

    CRemotePlayer* pRemote = RefNetGame()->GetPlayerPool()->GetPlayer(targetId);
    if (!pRemote || !pRemote->m_pPed || !pRemote->m_pPed->m_pGamePed) return;

    uintptr_t pTargetPed = (uintptr_t)pRemote->m_pPed->m_pGamePed;
    if (IsBadReadPtr((void*)(pTargetPed + 0x58C), 4)) return;
    uintptr_t pVeh = *(uintptr_t*)(pTargetPed + 0x58C);
    if (!pVeh || IsBadReadPtr((void*)pVeh, 0x100)) return;

    int targetVehID = -1;
    auto vehPool = RefNetGame()->GetVehiclePool();
    if (vehPool) {
        for (int i = 0; i < 2000; i++) {
            if (vehPool->Get(i) && vehPool->Get(i)->m_pGameVehicle == (void*)pVeh) {
                targetVehID = i;
                break;
            }
        }
    }
    if (targetVehID == -1) return;

    if (IsBadReadPtr((void*)(pVeh + 0x14), 4)) return;
    uintptr_t pMatrix = *(uintptr_t*)(pVeh + 0x14);
    if (!pMatrix || IsBadReadPtr((void*)pMatrix, 64)) return;

    float targetPos[3];
    targetPos[0] = *(float*)(pMatrix + 0x30);
    targetPos[1] = *(float*)(pMatrix + 0x34);
    targetPos[2] = *(float*)(pMatrix + 0x38);

    DWORD pLocalPed = *(DWORD*)0xB6F5F0;
    if (!pLocalPed) return;
    DWORD pLocalMatrix = *(DWORD*)(pLocalPed + 0x14);
    if (!pLocalMatrix || IsBadReadPtr((void*)pLocalMatrix, 64)) return;

    float savedPos[3];
    savedPos[0] = *(float*)(pLocalMatrix + 0x30);
    savedPos[1] = *(float*)(pLocalMatrix + 0x34);
    savedPos[2] = *(float*)(pLocalMatrix + 0x38);

    *(float*)(pLocalMatrix + 0x30) = targetPos[0];
    *(float*)(pLocalMatrix + 0x34) = targetPos[1];
    *(float*)(pLocalMatrix + 0x38) = targetPos[2] + 1.0f;

    for (int k = 0; k < 3; k++)
    {
        BitStream bs;
        bs.Write((uint16_t)targetVehID);
        bs.Write((uint8_t)0);
        int rpcID = RPC_EnterVehicle;
        pRPC(pRakClient, nullptr, &rpcID, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
    }

    for (int k = 0; k < 5; k++)
    {
        stUnoccupiedData unocData = { 0 };
        unocData.sVehicleID = targetVehID;
        unocData.byteSeatID = 0;
        unocData.fPosition[0] = targetPos[0];
        unocData.fPosition[1] = targetPos[1];
        unocData.fPosition[2] = targetPos[2] - 5.0f;
        unocData.fRoll[0] = *(float*)(pMatrix + 0x00); unocData.fRoll[1] = *(float*)(pMatrix + 0x04); unocData.fRoll[2] = *(float*)(pMatrix + 0x08);
        unocData.fDirection[0] = *(float*)(pMatrix + 0x10); unocData.fDirection[1] = *(float*)(pMatrix + 0x14); unocData.fDirection[2] = *(float*)(pMatrix + 0x18);
        unocData.fMoveSpeed[0] = 0.0f;
        unocData.fMoveSpeed[1] = 0.0f;
        unocData.fMoveSpeed[2] = -2.0f;

        unocData.fHealth = 1000.0f;

        BitStream bs;
        bs.Write((uint8_t)209);
        bs.Write((char*)&unocData, sizeof(stUnoccupiedData));

        pSend(pRakClient, nullptr, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
    }

    {
        BitStream bs;
        bs.Write((uint16_t)targetVehID);
        int rpcID = RPC_ExitVehicle;
        pRPC(pRakClient, nullptr, &rpcID, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
    }

    *(float*)(pLocalMatrix + 0x30) = savedPos[0];
    *(float*)(pLocalMatrix + 0x34) = savedPos[1];
    *(float*)(pLocalMatrix + 0x38) = savedPos[2];
}

const DWORD TIME_SPEED_ADRESS = 0xB7CB64;

void RunAirBreak() {
    if (!enable_airbreak) {
        ab_accel = 0.0f;
        ab_moving = false;
        return;
    }

    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (!pPed || !*pPed) return;

    DWORD pVehicle = *(DWORD*)0xBA18FC;
    bool inVehicle = (pVehicle != 0);
    DWORD pEntity = inVehicle ? pVehicle : *pPed;

    DWORD ptrMatrix = *(DWORD*)(pEntity + 0x14);
    if (!ptrMatrix) return;
    RwMatrix* pMatrix = (RwMatrix*)ptrMatrix;

    float camAngle = *(float*)0xB6F258;

    float s = sinf(camAngle);
    float c = cosf(camAngle);

    RwV3d vecForward = { -c, -s, 0.0f };
    RwV3d vecRight = { -s, c, 0.0f };

    if (inVehicle) {
        pMatrix->right.x = vecRight.x;
        pMatrix->right.y = vecRight.y;
        pMatrix->right.z = 0.0f;

        pMatrix->up.x = vecForward.x;
        pMatrix->up.y = vecForward.y;
        pMatrix->up.z = 0.0f;

        pMatrix->at.x = 0.0f;
        pMatrix->at.y = 0.0f;
        pMatrix->at.z = 1.0f;
    }
    else {
        float correctAngle = camAngle + 1.57079632f;

        *(float*)(*pPed + 0x558) = correctAngle;
        *(float*)(*pPed + 0x55C) = correctAngle;
    }

    RwV3d moveDir = { 0.0f, 0.0f, 0.0f };
    bool keyPressed = false;

    if (GetAsyncKeyState(0x57)) { moveDir.x += vecForward.x; moveDir.y += vecForward.y; keyPressed = true; }
    if (GetAsyncKeyState(0x53)) { moveDir.x -= vecForward.x; moveDir.y -= vecForward.y; keyPressed = true; }
    if (GetAsyncKeyState(0x44)) { moveDir.x += vecRight.x; moveDir.y += vecRight.y; keyPressed = true; }
    if (GetAsyncKeyState(0x41)) { moveDir.x -= vecRight.x; moveDir.y -= vecRight.y; keyPressed = true; }
    if (GetAsyncKeyState(VK_LSHIFT)) { moveDir.z += 1.0f; keyPressed = true; }
    if (GetAsyncKeyState(VK_LCONTROL)) { moveDir.z -= 1.0f; keyPressed = true; }

    float timeStep = *(float*)0xB7CB50;
    if (timeStep < 0.01f) timeStep = 1.0f;

    if (!keyPressed) {
        ab_moving = false;
        ab_accel = 0.0f;

        memset((void*)(pEntity + 0x44), 0, 12);
        memset((void*)(pEntity + 0x50), 0, 12);
    }
    else {
        ab_moving = true;

        if (ab_accel < airbreak_speed)
            ab_accel += 0.005f * (60.0f / timeStep);
        else
            ab_accel = airbreak_speed;

        float len = sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y + moveDir.z * moveDir.z);
        if (len > 0.0f) {
            float invLen = 1.0f / len;
            moveDir.x *= invLen; moveDir.y *= invLen; moveDir.z *= invLen;
        }

        RwV3d finalVel;
        finalVel.x = moveDir.x * ab_accel;
        finalVel.y = moveDir.y * ab_accel;
        finalVel.z = moveDir.z * ab_accel;

        *(float*)(pEntity + 0x44) = finalVel.x;
        *(float*)(pEntity + 0x48) = finalVel.y;
        *(float*)(pEntity + 0x4C) = finalVel.z;

        memset((void*)(pEntity + 0x50), 0, 12);

        float posMult = ab_accel * (60.0f / timeStep);

        pMatrix->pos.x += moveDir.x * posMult;
        pMatrix->pos.y += moveDir.y * posMult;
        pMatrix->pos.z += moveDir.z * posMult;
    }
}

typedef void(__thiscall* SetUsesCollision_t)(void* pEntity, bool bEnable);
SetUsesCollision_t SetUsesCollision = (SetUsesCollision_t)0x54DFB0;

void RunVoiceMagnet() {
    if (!enable_voice_listen) return;
    if (!RefNetGame() || !RefNetGame()->GetPlayerPool()) return;

    auto pLocalPlayer = RefNetGame()->GetPlayerPool()->GetLocalPlayer();
    if (!pLocalPlayer || !pLocalPlayer->m_pPed) return;

    sampapi::v037r3::CPed* pLocalSampPed = pLocalPlayer->m_pPed;
    if (!pLocalSampPed->m_pGamePed) return;

    CMatrix myMatrix;
    pLocalSampPed->GetMatrix(&myMatrix);

    if (!RefNetGame()->GetPlayerPool()->IsConnected(voice_target_id)) return;

    sampapi::v037r3::CRemotePlayer* pRemote = RefNetGame()->GetPlayerPool()->GetPlayer(voice_target_id);
    if (!pRemote || !pRemote->m_pPed || pRemote->m_pPed->IsDead()) return;
    if (!pRemote->m_pPed->m_pGamePed) return;

    uintptr_t pTargetPed = (uintptr_t)pRemote->m_pPed->m_pGamePed;
    uintptr_t pEntityToMove = pTargetPed;

    if (!IsBadReadPtr((void*)(pTargetPed + 0x58C), 4)) {
        uintptr_t vehPtr = *(uintptr_t*)(pTargetPed + 0x58C);
        if (vehPtr != 0) {
            pEntityToMove = vehPtr;
        }
    }

    if (IsBadReadPtr((void*)(pEntityToMove + 0x14), 4)) return;
    uintptr_t pMatrixPtr = *(uintptr_t*)(pEntityToMove + 0x14);

    if (pMatrixPtr && !IsBadWritePtr((void*)pMatrixPtr, 0x40)) {
        float distFront = 2.0f;
        float distUp = 0.5f;

        CVector newPos;
        newPos.x = myMatrix.pos.x + (myMatrix.at.x * distFront) + (myMatrix.up.x * distUp);
        newPos.y = myMatrix.pos.y + (myMatrix.at.y * distFront) + (myMatrix.up.y * distUp);
        newPos.z = myMatrix.pos.z + (myMatrix.at.z * distFront) + (myMatrix.up.y * distUp);

        *(float*)(pMatrixPtr + 0x30) = newPos.x;
        *(float*)(pMatrixPtr + 0x34) = newPos.y;
        *(float*)(pMatrixPtr + 0x38) = newPos.z;

        float* targetMat = (float*)pMatrixPtr;

        targetMat[0] = -myMatrix.right.x; targetMat[1] = -myMatrix.right.y; targetMat[2] = -myMatrix.right.z;
        targetMat[4] = -myMatrix.at.x;    targetMat[5] = -myMatrix.at.y;    targetMat[6] = -myMatrix.at.z;
        targetMat[8] = myMatrix.up.x;     targetMat[9] = myMatrix.up.y;     targetMat[10] = myMatrix.up.z;

        *(float*)(pEntityToMove + 0x44) = 0.0f;
        *(float*)(pEntityToMove + 0x48) = 0.0f;
        *(float*)(pEntityToMove + 0x4C) = 0.0f;
    }
}

void RunAutoHandbrake() {
    if (!enable_auto_handbrake) return;

    DWORD pVehicle = *(DWORD*)0xBA18FC;

    if (pVehicle) {
        float vX = *(float*)(pVehicle + 0x44);
        float vY = *(float*)(pVehicle + 0x48);
        float vZ = *(float*)(pVehicle + 0x4C);
        float speed = sqrt(vX * vX + vY * vY + vZ * vZ) * 180.0f;

        static bool isHolding = false;

        if (speed > 20.0f) {
            if (!isHolding) {
                keybd_event(VK_SPACE, 0, 0, 0);
                isHolding = true;
            }
        }
        else if (speed < 17.0f) {
            if (isHolding) {
                keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
                isHolding = false;
            }
        }
    }
}

bool GetWaypointPos(CVector& outPos) {
    float targetX = *(float*)0xC7F168;
    float targetY = *(float*)0xC7F16C;
    bool isSet = (*(uint8_t*)0xBA6774 > 0) || (targetX != 0.0f && targetY != 0.0f);

    if (isSet && (targetX != 0.0f || targetY != 0.0f)) {
        outPos.x = targetX;
        outPos.y = targetY;
        outPos.z = 0.0f;

        is_metka_found = true;
        return true;
    }

    is_metka_found = false;
    return false;
}

void RunTeleportLogic() {
    if (!enable_map_teleport) return;

    CVector dest;
    if (!GetWaypointPos(dest)) {
        enable_map_teleport = false;
        is_teleporting = false;
        return;
    }

    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (!pPed || !*pPed) return;

    DWORD pVeh = *(DWORD*)0xBA18FC;
    DWORD pEntity = pVeh ? pVeh : *pPed;
    DWORD ptrMatrix = *(DWORD*)(pEntity + 0x14);

    if (!ptrMatrix) return;

    is_teleporting = true;

    float cx = *(float*)(ptrMatrix + 0x30);
    float cy = *(float*)(ptrMatrix + 0x34);

    float dist = sqrt(pow(dest.x - cx, 2) + pow(dest.y - cy, 2));
    float step = 75.0f;

    if (dist > step) {
        float angle = atan2(dest.y - cy, dest.x - cx);

        *(float*)(ptrMatrix + 0x30) += cos(angle) * step;
        *(float*)(ptrMatrix + 0x34) += sin(angle) * step;
        *(float*)(ptrMatrix + 0x38) = 250.0f;

        float* speed = (float*)(pEntity + 0x44);
        speed[0] = 0; speed[1] = 0; speed[2] = 0;
    }
    else {
        *(float*)(ptrMatrix + 0x30) = dest.x;
        *(float*)(ptrMatrix + 0x34) = dest.y;
        *(float*)(ptrMatrix + 0x38) = 250.0f;

        float* speed = (float*)(pEntity + 0x44);
        speed[0] = 0; speed[1] = 0; speed[2] = 0;

        is_teleporting = false;
        enable_map_teleport = false;
    }
}

void RunMiscFeatures() {
    DWORD pClosestVehicle = *(DWORD*)0xB6F980;
    static bool last_nitro = false;
    if (enable_nitro != last_nitro) {
        DWORD oldProt;
        VirtualProtect((void*)0x969165, 1, PAGE_EXECUTE_READWRITE, &oldProt);
        *(BYTE*)0x969165 = enable_nitro ? 1 : 0;
        VirtualProtect((void*)0x969165, 1, oldProt, &oldProt);
        last_nitro = enable_nitro;
    }

    DWORD pCurrentVehicle = *(DWORD*)0xBA18FC;

    if (pCurrentVehicle && pCurrentVehicle > 0x1000) {
        if (enable_speedhack) {
            if (speedhack_act_key != 0 && (GetAsyncKeyState(speedhack_act_key) & 0x8000)) {
                float camAngle = *(float*)0xB6F178;
                if (!IsBadWritePtr((void*)(pCurrentVehicle + 0x44), 8)) {
                    float* speedX = (float*)(pCurrentVehicle + 0x44);
                    float* speedY = (float*)(pCurrentVehicle + 0x48);
                    *speedX = sinf(camAngle) * speedhack_val;
                    *speedY = cosf(camAngle) * speedhack_val;
                }
            }
        }

        if (!IsBadReadPtr((void*)(pCurrentVehicle + 0x384), 4)) {
            DWORD pHandling = *(DWORD*)(pCurrentVehicle + 0x384);

            if (pHandling && pHandling > 0x1000) {
                float* pTractionBias = (float*)(pHandling + 0xA4);

                static float original_val = -1.0f;
                static DWORD last_veh_ptr = 0;

                if (last_veh_ptr != pCurrentVehicle) {
                    original_val = -1.0f;
                    last_veh_ptr = pCurrentVehicle;
                }

                bool isShiftPressed = (GetAsyncKeyState(VK_LSHIFT) & 0x8000);

                if (enable_drift && isShiftPressed) {
                    DWORD oldProt;
                    if (VirtualProtect(pTractionBias, 4, PAGE_EXECUTE_READWRITE, &oldProt)) {

                        if (original_val == -1.0f) {
                            original_val = *pTractionBias;
                        }

                        if (original_val != -1.0f) {
                            *pTractionBias = drift_amount;
                        }

                        VirtualProtect(pTractionBias, 4, oldProt, &oldProt);
                    }
                }
                else {
                    if (original_val != -1.0f) {
                        DWORD oldProt;
                        if (VirtualProtect(pTractionBias, 4, PAGE_EXECUTE_READWRITE, &oldProt)) {
                            *pTractionBias = original_val;
                            VirtualProtect(pTractionBias, 4, oldProt, &oldProt);

                            original_val = -1.0f;
                        }
                    }
                }
            }
        }

        if (enable_car_jump) {
            static bool jump_pressed = false;
            if (car_jump_act_key != 0 && (GetAsyncKeyState(car_jump_act_key) & 0x8000)) {
                if (!jump_pressed && !IsBadWritePtr((void*)(pCurrentVehicle + 0x4C), 4)) {
                    float* speedZ = (float*)(pCurrentVehicle + 0x4C);
                    *speedZ += car_jump_force;
                    jump_pressed = true;
                }
            }
            else {
                jump_pressed = false;
            }
        }
    }

    if (enable_car_godmode && pClosestVehicle && pClosestVehicle > 0x1000) {
        if (!IsBadWritePtr((void*)(pClosestVehicle + 0x4C0), 4)) {
            float* vehHealth = (float*)(pClosestVehicle + 0x4C0);
            *vehHealth = 1000.0f;
        }

        DWORD ptrState = pClosestVehicle + 1064;
        DWORD ptrDoor = pClosestVehicle + 1272;

        if (!IsBadWritePtr((void*)ptrState, 4)) *(DWORD*)ptrState = 16;
        if (!IsBadWritePtr((void*)ptrDoor, 1)) *(BYTE*)ptrDoor = 1;
    }

    if (enable_nocol) {
        if (pCurrentVehicle && pCurrentVehicle > 0x1000) {
            if (!IsBadWritePtr((void*)(pCurrentVehicle + 0x44), 16)) {

                float* speedZ = (float*)(pCurrentVehicle + 0x4C);

                if (!enable_airbreak && !(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                    *speedZ = 0.0f;
                }

                float* speedX = (float*)(pCurrentVehicle + 0x44);
                float* speedY = (float*)(pCurrentVehicle + 0x48);

                DWORD ptrMatrix = *(DWORD*)(pCurrentVehicle + 0x14);
                if (ptrMatrix) {
                    float vecForwardX = *(float*)(ptrMatrix + 0x10);
                    float vecForwardY = *(float*)(ptrMatrix + 0x14);

                    float flySpeed = 0.8f;
                    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) flySpeed = 1.5f;

                    if (GetAsyncKeyState(0x57) & 0x8000) {
                        *speedX = vecForwardX * flySpeed;
                        *speedY = vecForwardY * flySpeed;
                    }
                    else if (GetAsyncKeyState(0x53) & 0x8000) {
                        *speedX = -(vecForwardX * flySpeed);
                        *speedY = -(vecForwardY * flySpeed);
                    }
                    else {
                        *speedX = 0.0f;
                        *speedY = 0.0f;
                    }

                    *(float*)(pCurrentVehicle + 0x50) = 0.0f;
                    *(float*)(pCurrentVehicle + 0x54) = 0.0f;
                }
            }
        }
        else {
            DWORD pPed = *(DWORD*)0xB6F5F0;
            if (pPed) {
                float* speedZ = (float*)(pPed + 0x4C);
                if (!enable_airbreak) *speedZ = 0.0f;
            }
        }
    }
}

typedef bool(__cdecl* GetIsLineOfSightClear_t)(CVector* start, CVector* end, bool bBuildings, bool bVehicles, bool bPeds, bool bObjects, bool bDummies, bool bSeeThrough, bool bDoCameraIgnoreCheck);
auto GetIsLineOfSightClear = (GetIsLineOfSightClear_t)0x56A490;

typedef void(__thiscall* GetBonePosition_t)(void* pPed, CVector* pResult, unsigned int uiBoneID, bool bDynamic);
auto GetBonePosition = (GetBonePosition_t)0x5E4280;

typedef bool(__thiscall* FireInstantHit_t)(void* pThis, void* pFiringEntity, CVector* pOrigin, CVector* pMuzzlePosn, void* pTargetEntity, CVector* pTarget, CVector* pOriginForDriveBy, bool bUnk, bool bMuzzle);
FireInstantHit_t oFireInstantHit = NULL;

float GetSmoothAnim(const char* label, bool condition, float speed = 8.0f) {
    static std::map<ImGuiID, float> anim_map;
    ImGuiID id = ImGui::GetID(label);
    float target = condition ? 1.0f : 0.0f;
    float delta = ImGui::GetIO().DeltaTime * speed;

    if (anim_map[id] < target) anim_map[id] = (std::min)(target, anim_map[id] + delta);
    else if (anim_map[id] > target) anim_map[id] = (std::max)(target, anim_map[id] - delta);

    return anim_map[id];
}

float GetRandomFloat(float min, float max) {
    return min + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (max - min)));
}

void DrawGearIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float r = size * 0.5f;
    dl->AddCircle(center, r * 0.6f, color, 0, 1.5f);
    for (int i = 0; i < 8; i++) {
        float angle = i * (2 * 3.14159f / 8);
        float ox = cos(angle);
        float oy = sin(angle);
        dl->AddLine(
            ImVec2(center.x + ox * r * 0.6f, center.y + oy * r * 0.6f),
            ImVec2(center.x + ox * r, center.y + oy * r),
            color, 1.5f
        );
    }
}

IDirect3DTexture9* GetSkinTexture(int id, IDirect3DDevice9* pDevice) {
    if (g_SkinTextures[id].tried_loading) {
        return g_SkinTextures[id].texture;
    }

    g_SkinTextures[id].tried_loading = true;

    std::string path = ".\\amazing_cef\\sites\\main\\assets\\images\\inventory\\items\\skins\\" + std::to_string(id) + ".png";

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if (!data) return nullptr;

    IDirect3DTexture9* texture = nullptr;
    if (pDevice->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, NULL) < 0) {
        stbi_image_free(data);
        return nullptr;
    }

    D3DLOCKED_RECT rect;
    if (texture->LockRect(0, &rect, NULL, D3DLOCK_DISCARD) >= 0) {
        for (int y = 0; y < height; y++) {
            unsigned char* src = data + (y * width * 4);
            unsigned char* dst = (unsigned char*)rect.pBits + (y * rect.Pitch);
            for (int x = 0; x < width; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2]; // Blue
                dst[x * 4 + 1] = src[x * 4 + 1]; // Green
                dst[x * 4 + 2] = src[x * 4 + 0]; // Red
                dst[x * 4 + 3] = src[x * 4 + 3]; // Alpha
            }
        }
        texture->UnlockRect(0);
    }

    stbi_image_free(data);
    g_SkinTextures[id].texture = texture;
    return texture;
}

namespace GUI {
    bool Checkbox(const char* label, bool* v) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);
        const ImGuiStyle& style = g.Style;

        const float square_sz = 18.0f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        const float total_width = square_sz + style.ItemSpacing.x + label_size.x;
        const ImRect total_bb(pos, ImVec2(pos.x + total_width, pos.y + square_sz));

        ImGui::ItemSize(total_bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
        if (pressed) {
            *v = !(*v);
            ImGui::MarkItemEdited(id);
        }

        float anim = GetSmoothAnim(label, *v, 12.0f);

        ImDrawList* draw = window->DrawList;
        const ImRect check_bb(pos, ImVec2(pos.x + square_sz, pos.y + square_sz));

        ImU32 bg_col = ImGui::GetColorU32(ImVec4(0.13f, 0.13f, 0.14f, 1.0f));
        ImU32 border_col = ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.28f, 1.0f));

        ImU32 active_col = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);

        draw->AddRectFilled(check_bb.Min, check_bb.Max, bg_col, 4.0f);

        if (anim > 0.0f) {
            ImU32 fill_col = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), (int)(255 * anim));
            draw->AddRectFilled(check_bb.Min, check_bb.Max, fill_col, 4.0f);
        }

        if (anim < 1.0f) {
            draw->AddRect(check_bb.Min, check_bb.Max, border_col, 4.0f);
        }

        if (anim > 0.2f) {
            ImVec2 center = ImVec2(check_bb.Min.x + square_sz / 2, check_bb.Min.y + square_sz / 2);
            float scale = (anim - 0.2f) / 0.8f;

            ImVec2 p1 = ImVec2(center.x - 4.0f, center.y + 0.5f);
            ImVec2 p2 = ImVec2(center.x - 1.5f, center.y + 3.0f);
            ImVec2 p3 = ImVec2(center.x + 4.0f, center.y - 3.5f);

            p1 = ImVec2(center.x + (p1.x - center.x) * scale, center.y + (p1.y - center.y) * scale);
            p2 = ImVec2(center.x + (p2.x - center.x) * scale, center.y + (p2.y - center.y) * scale);
            p3 = ImVec2(center.x + (p3.x - center.x) * scale, center.y + (p3.y - center.y) * scale);

            draw->AddLine(p1, p2, IM_COL32(255, 255, 255, (int)(255 * anim)), 2.0f);
            draw->AddLine(p2, p3, IM_COL32(255, 255, 255, (int)(255 * anim)), 2.0f);
        }

        ImVec2 text_pos = ImVec2(check_bb.Max.x + style.ItemSpacing.x, check_bb.Min.y + style.FramePadding.y - 1);

        ImU32 text_color = (hovered || *v) ? IM_COL32(230, 230, 230, 255) : IM_COL32(160, 160, 170, 255);
        if (*v) text_color = active_col;

        draw->AddText(text_pos, IM_COL32(200, 200, 200, 255), label);

        return pressed;
    }

    void DrawTabIcon(ImDrawList* dl, ImVec2 pos, int icon_id, ImU32 color) {
        float size = 16.0f;
        ImVec2 center = ImVec2(pos.x + size / 2, pos.y + size / 2);
        float t = 1.5f;

        switch (icon_id) {
        case 0:
            dl->AddCircle(center, 6.0f, color, 0, t);
            dl->AddLine(ImVec2(center.x, center.y - 8), ImVec2(center.x, center.y + 8), color, t);
            dl->AddLine(ImVec2(center.x - 8, center.y), ImVec2(center.x + 8, center.y), color, t);
            break;
        case 1:
            dl->PathLineTo(ImVec2(center.x - 7, center.y));
            dl->PathBezierQuadraticCurveTo(ImVec2(center.x, center.y - 5), ImVec2(center.x + 7, center.y), 0);
            dl->PathBezierQuadraticCurveTo(ImVec2(center.x, center.y + 5), ImVec2(center.x - 7, center.y), 0);
            dl->PathStroke(color, 0, t);
            dl->AddCircleFilled(center, 2.0f, color);
            break;
        case 2:
            dl->AddLine(ImVec2(center.x - 6, center.y - 3), ImVec2(center.x + 6, center.y - 3), color, t);
            dl->AddCircleFilled(ImVec2(center.x - 3, center.y - 3), 2.0f, color);
            dl->AddLine(ImVec2(center.x - 6, center.y + 3), ImVec2(center.x + 6, center.y + 3), color, t);
            dl->AddCircleFilled(ImVec2(center.x + 3, center.y + 3), 2.0f, color);
            break;
        case 6:
            dl->AddRect(ImVec2(center.x - 6, center.y - 6), ImVec2(center.x + 6, center.y + 6), color, 1.0f, 0, t);
            dl->AddRectFilled(ImVec2(center.x - 3, center.y - 6), ImVec2(center.x + 3, center.y - 3), color);
            break;
        case 99:
        {
            ImVec2 p1(center.x - 6, center.y - 4);
            ImVec2 p2(center.x + 7, center.y);
            ImVec2 p3(center.x - 6, center.y + 4);
            dl->AddTriangle(p1, p2, p3, color, t);
            dl->AddLine(p1, p2, color, t);
            dl->AddLine(p2, p3, color, t);
            dl->AddLine(p3, ImVec2(center.x - 4, center.y), color, t);
            dl->AddLine(ImVec2(center.x - 4, center.y), p1, color, t);
        }
        break;
        }
    }

    bool TabButton(const char* label, int icon_id, int index, int& active_index) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 45);
        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        ImGui::ItemSize(size);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed) active_index = index;

        bool selected = (active_index == index);

        if (selected) {
            window->DrawList->AddRectFilled(
                ImVec2(pos.x, pos.y + 10),
                ImVec2(pos.x + 3, pos.y + 35),
                IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255)
            );
        }

        ImU32 text_col = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(110, 110, 110, 255);

        DrawTabIcon(window->DrawList, ImVec2(pos.x + 20, pos.y + 14), icon_id, text_col);

        window->DrawList->AddText(ImVec2(pos.x + 55, pos.y + 14), text_col, label);

        return pressed;
    }

    bool GearButton(const char* str_id) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(str_id);

        float size_sz = 18.0f;
        float x = window->DC.CursorPos.x + ImGui::GetContentRegionAvail().x - size_sz;
        float y = window->DC.CursorPos.y - 28.0f;

        const ImRect bb(ImVec2(x, y), ImVec2(x + size_sz, y + size_sz));
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        ImU32 col = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
        ImVec2 center = ImVec2(bb.Min.x + size_sz / 2, bb.Min.y + size_sz / 2);

        window->DrawList->AddCircle(center, 5.0f, col, 0, 1.5f);
        for (int i = 0; i < 4; i++) {
            window->DrawList->AddLine(
                ImVec2(center.x + (i == 0 ? 7 : i == 1 ? 0 : i == 2 ? -7 : 0), center.y + (i == 1 ? 7 : i == 3 ? -7 : 0)),
                ImVec2(center.x + (i == 0 ? -7 : i == 1 ? 0 : i == 2 ? 7 : 0), center.y + (i == 1 ? -7 : i == 3 ? 7 : 0)),
                col, 1.5f);
        }
        return pressed;
    }

    namespace CustomGUI {
        void DrawCheckerboard(ImDrawList* dl, ImVec2 min, ImVec2 max, float size, ImU32 col1, ImU32 col2) {
            dl->AddRectFilled(min, max, col1);
            for (float y = min.y; y < max.y; y += size) {
                for (float x = min.x; x < max.x; x += size) {
                    if (((int)(x / size) + (int)(y / size)) % 2 == 0)
                        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size), col2);
                }
            }
        }

        bool ConfigInput(const char* label, char* buf, size_t buf_size) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems) return false;

            ImVec2 pos = window->DC.CursorPos;
            float w = ImGui::GetContentRegionAvail().x;
            float h = 35.0f;

            ImGui::PushID(label);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

            float textHeight = ImGui::GetTextLineHeight();
            float paddingY = (h - textHeight) / 2.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, paddingY));

            ImU32 bg_col = ImGui::GetColorU32(ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
            window->DrawList->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg_col, 6.0f);

            ImGui::SetNextItemWidth(w);
            bool ret = ImGui::InputText("##input", buf, buf_size);

            bool active = ImGui::IsItemActive();
            bool hovered = ImGui::IsItemHovered();

            ImU32 border_col = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            if (active) {
                border_col = ImGui::GetColorU32(ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f));
            }
            else if (hovered) {
                border_col = ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            }

            window->DrawList->AddRect(pos, ImVec2(pos.x + w, pos.y + h), border_col, 6.0f);

            if (!active && strlen(buf) == 0) {
                ImVec2 text_pos = ImVec2(pos.x + 10, pos.y + paddingY);
                window->DrawList->AddText(text_pos, IM_COL32(100, 100, 100, 255), "Enter config name...");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::PopID();

            return ret;
        }

        bool ColorPickerPopup(const char* label, float* col) {
            bool value_changed = false;
            ImGuiIO& io = ImGui::GetIO();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            const float popup_width = 240.0f;
            const float sv_height = 140.0f;
            const float bars_height = 140.0f;
            const float total_height = sv_height + bars_height;

            ImGui::SetNextWindowSize(ImVec2(popup_width, total_height));
            ImGui::SetNextWindowBgAlpha(1.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));

            if (ImGui::BeginPopup(label)) {
                ImVec2 p = ImGui::GetWindowPos();

                float H, S, V;
                ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], H, S, V);

                {
                    ImVec2 sv_min = p;
                    ImVec2 sv_max = ImVec2(p.x + popup_width, p.y + sv_height);

                    dl->AddRectFilledMultiColor(sv_min, sv_max,
                        IM_COL32(255, 255, 255, 255),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(H, 1, 1))),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(H, 1, 1))),
                        IM_COL32(255, 255, 255, 255));

                    dl->AddRectFilledMultiColor(sv_min, sv_max,
                        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                        IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));

                    ImGui::SetCursorPos(ImVec2(0, 0));
                    ImGui::InvisibleButton("##sv_box", ImVec2(popup_width, sv_height));

                    if (ImGui::IsItemActive()) {
                        S = ImSaturate((io.MousePos.x - sv_min.x) / (sv_max.x - sv_min.x));
                        V = 1.0f - ImSaturate((io.MousePos.y - sv_min.y) / (sv_max.y - sv_min.y));
                        value_changed = true;
                    }

                    ImVec2 cursor_pos = ImVec2(sv_min.x + S * popup_width, sv_min.y + (1.0f - V) * sv_height);
                    dl->AddCircle(cursor_pos, 5.0f, IM_COL32(255, 255, 255, 255), 12, 1.5f);
                }

                ImVec2 controls_pos = ImVec2(p.x + 15.0f, p.y + sv_height + 15.0f);
                float bar_w = popup_width - 30.0f;
                float bar_h = 10.0f;

                {
                    ImVec2 hue_min = controls_pos;
                    ImVec2 hue_max = ImVec2(hue_min.x + bar_w, hue_min.y + bar_h);

                    const int segments = 6;
                    for (int i = 0; i < segments; ++i) {
                        float t1 = (float)i / segments;
                        float t2 = (float)(i + 1) / segments;
                        dl->AddRectFilledMultiColor(
                            ImVec2(hue_min.x + t1 * bar_w, hue_min.y),
                            ImVec2(hue_min.x + t2 * bar_w, hue_max.y),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(t1, 1, 1))),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(t2, 1, 1))),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(t2, 1, 1))),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(ImColor::HSV(t1, 1, 1)))
                        );
                    }
                    dl->AddRect(hue_min, hue_max, IM_COL32(0, 0, 0, 50), 5.0f);

                    ImGui::SetCursorScreenPos(hue_min);
                    ImGui::InvisibleButton("##hue_bar", ImVec2(bar_w, bar_h));
                    if (ImGui::IsItemActive()) {
                        H = ImSaturate((io.MousePos.x - hue_min.x) / bar_w);
                        value_changed = true;
                    }

                    ImVec2 knob_pos = ImVec2(hue_min.x + H * bar_w, hue_min.y + bar_h / 2);
                    dl->AddCircleFilled(knob_pos, 8.0f, IM_COL32(255, 20, 147, 255));
                    dl->AddCircle(knob_pos, 9.0f, IM_COL32(255, 255, 255, 255), 12, 2.0f);
                }

                float preview_y = controls_pos.y + bar_h + 20.0f;
                {
                    float preview_r = 18.0f;
                    ImVec2 preview_center = ImVec2(p.x + 15.0f + preview_r, preview_y + preview_r);
                    dl->AddCircleFilled(preview_center, preview_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 1.0f)));
                    dl->AddCircle(preview_center, preview_r, IM_COL32(255, 255, 255, 50), 12, 1.0f);

                    float hex_x = p.x + 15.0f + (preview_r * 2) + 15.0f;
                    float hex_w = popup_width - (hex_x - p.x) - 15.0f;
                    float hex_h = 36.0f;
                    ImVec2 hex_min = ImVec2(hex_x, preview_y);
                    ImVec2 hex_max = ImVec2(hex_x + hex_w, preview_y + hex_h);

                    dl->AddRectFilled(hex_min, hex_max, IM_COL32(60, 60, 60, 255), 18.0f);

                    char hex_str[16];
                    sprintf_s(hex_str, "#%02X%02X%02X", (int)(col[0] * 255), (int)(col[1] * 255), (int)(col[2] * 255));

                    ImGui::PushFont(ImGui::GetFont());
                    ImVec2 txt_sz = ImGui::CalcTextSize(hex_str);
                    dl->AddText(ImVec2(hex_min.x + (hex_w - txt_sz.x) / 2, hex_min.y + (hex_h - txt_sz.y) / 2), IM_COL32(200, 200, 200, 255), hex_str);
                    ImGui::PopFont();
                }

                float palette_y = preview_y + 36.0f + 15.0f;
                {
                    ImU32 palette[] = {
                        IM_COL32(38, 70, 83, 255), IM_COL32(42, 157, 143, 255), IM_COL32(233, 196, 106, 255),
                        IM_COL32(244, 162, 97, 255), IM_COL32(231, 111, 81, 255), IM_COL32(220, 20, 60, 255),
                        IM_COL32(0, 71, 171, 255), IM_COL32(30, 144, 255, 255), IM_COL32(0, 191, 255, 255),
                        IM_COL32(0, 206, 209, 255), IM_COL32(135, 206, 250, 255)
                    };

                    float circle_radius = 11.0f;
                    float spacing = (popup_width - 30.0f - (6 * circle_radius * 2)) / 5.0f;
                    if (spacing < 5.0f) spacing = 5.0f;

                    ImVec2 start_p = ImVec2(p.x + 15.0f + circle_radius, palette_y + circle_radius);

                    for (int i = 0; i < 10; i++) {
                        int row = i / 5;
                        int col_idx = i % 5;

                        ImVec2 center = ImVec2(
                            start_p.x + col_idx * (circle_radius * 2 + spacing),
                            start_p.y + row * (circle_radius * 2 + 8.0f)
                        );

                        dl->AddCircleFilled(center, circle_radius, palette[i]);

                        ImGui::SetCursorScreenPos(ImVec2(center.x - circle_radius, center.y - circle_radius));
                        char pid[16]; sprintf_s(pid, "##pal%d", i);
                        if (ImGui::InvisibleButton(pid, ImVec2(circle_radius * 2, circle_radius * 2))) {
                            ImVec4 c = ImGui::ColorConvertU32ToFloat4(palette[i]);
                            col[0] = c.x; col[1] = c.y; col[2] = c.z;
                            value_changed = true;
                        }
                    }
                }

                if (value_changed) {
                    ImGui::ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return value_changed;
        }

        void ColorEditButton(const char* label, float* col) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems) return;

            ImGuiContext& g = *GImGui;
            const ImGuiID id = window->GetID(label);

            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", label);
            ImGui::SameLine();

            float h = ImGui::GetFrameHeight();
            float w = h * 1.5f;

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);

            const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + w, window->DC.CursorPos.y + h));
            ImGui::ItemSize(bb);
            if (ImGui::ItemAdd(bb, id)) {
                bool hovered, held;
                bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

                ImU32 col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 1.0f));
                window->DrawList->AddRectFilled(bb.Min, bb.Max, col32, 6.0f);

                if (hovered) {
                    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 100), 6.0f, 0, 2.0f);
                }

                if (pressed) {
                    ImGui::OpenPopup(label);
                }
            }
            ColorPickerPopup(label, col);
        }
    }

    void Slider(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.1f") {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", label);

        float input_w = 40.0f;
        float spacing = 8.0f;
        float total_w = ImGui::GetContentRegionAvail().x;
        float slider_w = total_w - input_w - spacing;
        float height = 18.0f;

        ImVec2 pos = window->DC.CursorPos;
        float center_y = pos.y + height / 2.0f;

        const ImRect bb(pos, ImVec2(pos.x + slider_w, pos.y + height));
        ImGui::ItemSize(bb);

        if (ImGui::ItemAdd(bb, id)) {
            bool hovered, held;
            bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

            if (held || pressed) {
                float mouse_x = g.IO.MousePos.x;
                float normalized = (mouse_x - bb.Min.x) / (bb.Max.x - bb.Min.x);
                if (normalized < 0.0f) normalized = 0.0f;
                if (normalized > 1.0f) normalized = 1.0f;
                *v = v_min + normalized * (v_max - v_min);
            }
        }

        ImDrawList* draw = window->DrawList;
        draw->AddRectFilled(ImVec2(bb.Min.x, center_y - 2), ImVec2(bb.Max.x, center_y + 2), IM_COL32(30, 30, 35, 255), 2.0f);

        float fraction = (*v - v_min) / (v_max - v_min);
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;

        float grab_x = bb.Min.x + (slider_w * fraction);

        ImU32 accent = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
        ImU32 accent_dim = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 150);

        if (fraction > 0.01f) {
            draw->AddRectFilled(ImVec2(bb.Min.x, center_y - 2), ImVec2(grab_x, center_y + 2), accent, 2.0f);
        }

        draw->AddCircleFilled(ImVec2(grab_x, center_y), 6.0f, IM_COL32(255, 255, 255, 255));
        draw->AddCircleFilled(ImVec2(grab_x, center_y), 4.0f, accent);

        bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            draw->AddCircle(ImVec2(grab_x, center_y), 7.0f, accent_dim, 12, 1.5f);
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(input_w);

        char input_id[64];
        sprintf_s(input_id, "##input_%s", label);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        ImGui::InputFloat(input_id, v, 0.0f, 0.0f, fmt);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::PopItemWidth();
    }

    void BindCheckbox(const char* label, Keybind* bind) {
        if (Checkbox(label, bind->value)) {
            if (bind->callback) bind->callback(*bind->value);
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            current_bind_edit = bind;
            show_bind_window = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Right Click to Bind");
            if (bind->key != 0) {
                ImGui::TextColored(ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f), "Current: %s [%s]", keyNames[bind->key], bind->mode == BIND_HOLD ? "Hold" : "Toggle");
            }
            ImGui::EndTooltip();
        }
    }

    void ColorPicker(const char* label, float* col) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), label);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::ColorEdit3(std::string("##").append(label).c_str(), col, ImGuiColorEditFlags_NoInputs);
    }
}

namespace ConfigSystem {
    char configNameBuffer[128] = "default";
    int selectedConfigIndex = -1;
    std::vector<std::string> configFiles;
    const std::string folderPath = "C:\\dcp-hack\\configs\\";

    void SetupFolders() {
        _mkdir("C:\\dcp-hack");
        _mkdir(folderPath.c_str());
    }

    void RefreshConfigs() {
        configFiles.clear();
        std::string search_path = folderPath + "*.cfg";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search_path.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string name = fd.cFileName;
                    size_t lastindex = name.find_last_of(".");
                    if (lastindex != std::string::npos) name = name.substr(0, lastindex);
                    configFiles.push_back(name);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    void WriteFloat(std::ofstream& out, const std::string& name, float val) { out << name << " " << val << std::endl; }
    void WriteInt(std::ofstream& out, const std::string& name, int val) { out << name << " " << val << std::endl; }
    void WriteBool(std::ofstream& out, const std::string& name, bool val) { out << name << " " << (val ? "1" : "0") << std::endl; }
    void WriteColor(std::ofstream& out, const std::string& name, float* col, int size = 3) {
        out << name << " ";
        for (int i = 0; i < size; i++) out << col[i] << " ";
        out << std::endl;
    }

    void SaveConfig(const std::string& cfgName) {
        std::ofstream out(folderPath + cfgName + ".cfg");
        if (!out.is_open()) return;

        WriteBool(out, "enable_seat_tp", enable_seat_tp);
        WriteBool(out, "enable_gym_bot", enable_gym_bot);
        WriteInt(out, "gym_delay", gym_delay);
        WriteBool(out, "enable_esp_vis_check", enable_esp_vis_check);
        WriteColor(out, "col_esp_vis", col_esp_vis);
        WriteColor(out, "col_esp_occ", col_esp_occ);
        WriteFloat(out, "seat_tp_fov", seat_tp_fov);
        WriteInt(out, "seat_tp_key", seat_tp_key);
        WriteBool(out, "draw_seat_tp_fov", draw_seat_tp_fov);
        WriteColor(out, "col_seat_tp_fov", col_seat_tp_fov);
        WriteBool(out, "enable_cef_fast_mine", enable_cef_fast_mine);
        WriteBool(out, "enable_tracers", enable_tracers);
        WriteBool(out, "real_crosshair_fov", real_crosshair_fov);
        WriteColor(out, "col_tracer", col_tracer);
        WriteFloat(out, "tracer_duration", tracer_duration);
        WriteBool(out, "silent_magic", silent_magic);
        WriteBool(out, "enable_auto_handbrake", enable_auto_handbrake);
        WriteBool(out, "enable_frozen_lands", enable_frozen_lands);
        WriteBool(out, "enable_esp_glow", enable_esp_glow);
        WriteFloat(out, "glow_intensity", glow_intensity);
        WriteBool(out, "enable_box", enable_box);
        WriteInt(out, "style_box", style_box);
        WriteBool(out, "enable_lines", enable_lines);
        WriteInt(out, "style_lines", style_lines);
        WriteBool(out, "enable_name", enable_name);
        WriteBool(out, "enable_id_tags", enable_id_tags);
        WriteBool(out, "enable_health", enable_health);
        WriteInt(out, "style_health", style_health);
        WriteBool(out, "enable_armor", enable_armor);
        WriteBool(out, "enable_text_hp_arm", enable_text_hp_arm);
        WriteBool(out, "enable_weapon", enable_weapon);
        WriteBool(out, "enable_skeleton", enable_skeleton);
        WriteFloat(out, "skeleton_thickness", skeleton_thickness);
        WriteBool(out, "enable_chams", enable_chams);
        WriteBool(out, "chams_wireframe", chams_wireframe);
        WriteBool(out, "enable_trail", enable_trail);
        WriteFloat(out, "trail_max_points", trail_max_points);
        WriteBool(out, "enable_arrows", enable_arrows);
        WriteFloat(out, "arrows_radius", arrows_radius);
        WriteFloat(out, "arrows_size", arrows_size);
        WriteBool(out, "enable_miner_wh", enable_miner_wh);

        WriteColor(out, "col_esp_box", col_esp_box);
        WriteColor(out, "col_esp_fill_top", col_esp_fill_top);
        WriteColor(out, "col_esp_fill_bot", col_esp_fill_bot);
        WriteColor(out, "col_esp_lines", col_esp_lines);
        WriteColor(out, "col_esp_names", col_esp_names);
        WriteColor(out, "col_esp_armor", col_esp_armor);
        WriteColor(out, "col_skeleton", col_skeleton);
        WriteColor(out, "col_chams_vis", col_chams_vis);
        WriteColor(out, "col_chams_inv", col_chams_inv);
        WriteColor(out, "col_trail", col_trail);
        WriteColor(out, "col_arrows", col_arrows);
        WriteColor(out, "col_hp_grad_top", col_hp_grad_top);
        WriteColor(out, "col_hp_grad_bot", col_hp_grad_bot);
        WriteColor(out, "col_auto_peek", col_auto_peek);
        WriteColor(out, "col_mouse_fov", col_mouse_fov);
        WriteColor(out, "col_aim_fov", col_aim_fov);
        WriteColor(out, "menu_color", menu_color);

        WriteBool(out, "silent_enabled", silent_enabled);
        WriteBool(out, "draw_fov", draw_fov);
        WriteBool(out, "silent_ignore_walls", silent_ignore_walls);
        WriteBool(out, "silent_ignore_team", silent_ignore_team);
        WriteBool(out, "silent_ignore_skin", silent_ignore_skin);
        WriteBool(out, "silent_random_shot", silent_random_shot);
        WriteFloat(out, "silent_fov", silent_fov);
        WriteFloat(out, "silent_max_dist", silent_max_dist);
        WriteFloat(out, "silent_random_spread", silent_random_spread);

        WriteBool(out, "enable_mouse_aim", enable_mouse_aim);
        WriteFloat(out, "mouse_fov", mouse_fov);
        WriteFloat(out, "mouse_smooth", mouse_smooth);
        WriteBool(out, "mouse_check_vis", mouse_check_vis);
        WriteFloat(out, "mouse_offset_x", mouse_offset_x);
        WriteFloat(out, "mouse_offset_y", mouse_offset_y);
        WriteFloat(out, "mouse_predict", mouse_predict);
        WriteInt(out, "mouse_aim_bone", mouse_aim_bone);
        WriteBool(out, "draw_mouse_fov", draw_mouse_fov);
        WriteBool(out, "enable_event_mode", enable_event_mode);

        WriteBool(out, "enable_drift", enable_drift);
        WriteFloat(out, "drift_amount", drift_amount);
        WriteBool(out, "enable_nitro", enable_nitro);
        WriteBool(out, "enable_car_godmode", enable_car_godmode);
        WriteBool(out, "enable_speedhack", enable_speedhack);
        WriteFloat(out, "speedhack_val", speedhack_val);
        WriteInt(out, "speedhack_act_key", speedhack_act_key);
        WriteBool(out, "enable_car_jump", enable_car_jump);
        WriteFloat(out, "car_jump_force", car_jump_force);
        WriteInt(out, "car_jump_act_key", car_jump_act_key);
        WriteBool(out, "enable_open_doors", enable_open_doors);
        WriteBool(out, "enable_nocol", enable_nocol);
        WriteBool(out, "enable_autopc", enable_autopc);
        WriteBool(out, "enable_all_skills", enable_all_skills);
        WriteBool(out, "enable_airbreak", enable_airbreak);
        WriteFloat(out, "airbreak_speed", airbreak_speed);
        WriteBool(out, "enable_invis", enable_invis);
        WriteFloat(out, "invis_z", invis_z);
        WriteBool(out, "enable_anim_breaker", enable_anim_breaker);
        WriteBool(out, "enable_voice_listen", enable_voice_listen);
        WriteBool(out, "enable_auto_peek", enable_auto_peek);

        WriteBool(out, "enable_nocol_buildings", enable_nocol_buildings);
        WriteBool(out, "enable_faction_bypass", enable_faction_bypass);
        WriteBool(out, "enable_rvanka", enable_rvanka);

        WriteInt(out, "rvanka_mode", rvanka_mode);
        WriteFloat(out, "rvanka_speed", rvanka_speed);
        WriteFloat(out, "rvanka_fov", rvanka_fov);
        WriteInt(out, "rvanka_key", rvanka_key);
        WriteBool(out, "rvanka_draw_fov", rvanka_draw_fov);
        WriteColor(out, "col_rvanka_fov", col_rvanka_fov);

        WriteBool(out, "wm_show_time", wm_show_time);
        WriteBool(out, "wm_show_fps", wm_show_fps);
        WriteBool(out, "wm_show_link", wm_show_link);

        for (int i = 0; i < 4; i++) {
            std::string key = "col_miner_" + std::to_string(i);
            WriteColor(out, key, col_miner[i]);
        }

        for (auto& bind : g_Keybinds) {
            out << "bind \"" << bind->name << "\" " << bind->key << " " << bind->mode << std::endl;
        }

        out.close();
        RefreshConfigs();
    }

    void LoadConfig(const std::string& cfgName) {
        std::ifstream in(folderPath + cfgName + ".cfg");
        if (!in.is_open()) return;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string key;
            ss >> key;

            auto ReadColor = [&](float* col, int size = 3) {
                for (int i = 0; i < size; i++) ss >> col[i];
                };

            if (key == "bind") {
                std::string bindNameStr;
                char ch;
                ss >> ch;
                if (ch == '"') {
                    std::getline(ss, bindNameStr, '"');
                }
                else {
                    ss.putback(ch);
                    ss >> bindNameStr;
                }

                int keyVal = 0, modeVal = 0;
                ss >> keyVal >> modeVal;

                for (auto& bind : g_Keybinds) {
                    if (std::string(bind->name) == bindNameStr) {
                        bind->key = keyVal;
                        bind->mode = modeVal;
                        break;
                    }
                }
                continue;
            }

            if (key == "enable_box") ss >> enable_box;
            else if (key == "enable_seat_tp") ss >> enable_seat_tp;
            else if (key == "enable_gym_bot") ss >> enable_gym_bot;
            else if (key == "gym_delay") ss >> gym_delay;
            else if (key == "enable_esp_vis_check") ss >> enable_esp_vis_check;
            else if (key == "col_esp_vis") ReadColor(col_esp_vis);
            else if (key == "col_esp_occ") ReadColor(col_esp_occ);
            else if (key == "seat_tp_fov") ss >> seat_tp_fov;
            else if (key == "seat_tp_key") ss >> seat_tp_key;
            else if (key == "draw_seat_tp_fov") ss >> draw_seat_tp_fov;
            else if (key == "col_seat_tp_fov") ReadColor(col_seat_tp_fov);
            else if (key == "enable_cef_fast_mine") ss >> enable_cef_fast_mine;
            else if (key == "enable_tracers") ss >> enable_tracers;
            else if (key == "real_crosshair_fov") ss >> real_crosshair_fov;
            else if (key == "col_tracer") ReadColor(col_tracer);
            else if (key == "tracer_duration") ss >> tracer_duration;
            else if (key == "silent_magic") ss >> silent_magic;
            else if (key == "enable_auto_handbrake") ss >> enable_auto_handbrake;
            else if (key == "enable_frozen_lands") ss >> enable_frozen_lands;
            else if (key == "col_esp_fill_top") ReadColor(col_esp_fill_top);
            else if (key == "col_esp_fill_bot") ReadColor(col_esp_fill_bot);
            else if (key == "enable_esp_glow") ss >> enable_esp_glow;
            else if (key == "glow_intensity") ss >> glow_intensity;
            else if (key == "style_box") ss >> style_box;
            else if (key == "enable_lines") ss >> enable_lines;
            else if (key == "style_lines") ss >> style_lines;
            else if (key == "enable_name") ss >> enable_name;
            else if (key == "enable_id_tags") ss >> enable_id_tags;
            else if (key == "enable_health") ss >> enable_health;
            else if (key == "style_health") ss >> style_health;
            else if (key == "enable_armor") ss >> enable_armor;
            else if (key == "enable_text_hp_arm") ss >> enable_text_hp_arm;
            else if (key == "enable_weapon") ss >> enable_weapon;
            else if (key == "enable_skeleton") ss >> enable_skeleton;
            else if (key == "skeleton_thickness") ss >> skeleton_thickness;
            else if (key == "enable_chams") ss >> enable_chams;
            else if (key == "chams_wireframe") ss >> chams_wireframe;
            else if (key == "enable_trail") ss >> enable_trail;
            else if (key == "trail_max_points") ss >> trail_max_points;
            else if (key == "enable_arrows") ss >> enable_arrows;
            else if (key == "arrows_radius") ss >> arrows_radius;
            else if (key == "arrows_size") ss >> arrows_size;
            else if (key == "enable_miner_wh") ss >> enable_miner_wh;

            else if (key == "col_esp_box") ReadColor(col_esp_box);
            else if (key == "col_esp_lines") ReadColor(col_esp_lines);
            else if (key == "col_esp_names") ReadColor(col_esp_names);
            else if (key == "col_esp_armor") ReadColor(col_esp_armor);
            else if (key == "col_skeleton") ReadColor(col_skeleton);
            else if (key == "col_chams_vis") ReadColor(col_chams_vis);
            else if (key == "col_chams_inv") ReadColor(col_chams_inv);
            else if (key == "col_trail") ReadColor(col_trail);
            else if (key == "col_arrows") ReadColor(col_arrows);
            else if (key == "col_hp_grad_top") ReadColor(col_hp_grad_top);
            else if (key == "col_hp_grad_bot") ReadColor(col_hp_grad_bot);
            else if (key == "col_auto_peek") ReadColor(col_auto_peek);
            else if (key == "col_mouse_fov") ReadColor(col_mouse_fov);
            else if (key == "col_aim_fov") ReadColor(col_aim_fov);
            else if (key == "menu_color") ReadColor(menu_color);

            else if (key == "silent_enabled") ss >> silent_enabled;
            else if (key == "draw_fov") ss >> draw_fov;
            else if (key == "silent_ignore_walls") ss >> silent_ignore_walls;
            else if (key == "silent_ignore_team") ss >> silent_ignore_team;
            else if (key == "silent_ignore_skin") ss >> silent_ignore_skin;
            else if (key == "silent_random_shot") ss >> silent_random_shot;
            else if (key == "silent_fov") ss >> silent_fov;
            else if (key == "silent_max_dist") ss >> silent_max_dist;
            else if (key == "silent_random_spread") ss >> silent_random_spread;

            else if (key == "enable_mouse_aim") ss >> enable_mouse_aim;
            else if (key == "mouse_fov") ss >> mouse_fov;
            else if (key == "mouse_smooth") ss >> mouse_smooth;
            else if (key == "mouse_check_vis") ss >> mouse_check_vis;
            else if (key == "mouse_offset_x") ss >> mouse_offset_x;
            else if (key == "mouse_offset_y") ss >> mouse_offset_y;
            else if (key == "mouse_predict") ss >> mouse_predict;
            else if (key == "mouse_aim_bone") ss >> mouse_aim_bone;
            else if (key == "draw_mouse_fov") ss >> draw_mouse_fov;
            else if (key == "enable_event_mode") ss >> enable_event_mode;

            else if (key == "enable_drift") ss >> enable_drift;
            else if (key == "drift_amount") ss >> drift_amount;
            else if (key == "enable_nitro") ss >> enable_nitro;
            else if (key == "enable_car_godmode") ss >> enable_car_godmode;
            else if (key == "enable_speedhack") ss >> enable_speedhack;
            else if (key == "speedhack_val") ss >> speedhack_val;
            else if (key == "speedhack_act_key") ss >> speedhack_act_key;
            else if (key == "enable_car_jump") ss >> enable_car_jump;
            else if (key == "car_jump_force") ss >> car_jump_force;
            else if (key == "car_jump_act_key") ss >> car_jump_act_key;
            else if (key == "enable_open_doors") ss >> enable_open_doors;
            else if (key == "enable_nocol") ss >> enable_nocol;
            else if (key == "enable_autopc") ss >> enable_autopc;
            else if (key == "enable_all_skills") ss >> enable_all_skills;
            else if (key == "enable_airbreak") ss >> enable_airbreak;
            else if (key == "airbreak_speed") ss >> airbreak_speed;
            else if (key == "enable_invis") ss >> enable_invis;
            else if (key == "invis_z") ss >> invis_z;
            else if (key == "enable_anim_breaker") ss >> enable_anim_breaker;
            else if (key == "enable_voice_listen") ss >> enable_voice_listen;
            else if (key == "enable_auto_peek") ss >> enable_auto_peek;

            else if (key == "enable_nocol_buildings") ss >> enable_nocol_buildings;
            else if (key == "enable_faction_bypass") ss >> enable_faction_bypass;
            else if (key == "enable_rvanka") ss >> enable_rvanka;

            else if (key == "rvanka_mode") ss >> rvanka_mode;
            else if (key == "rvanka_speed") ss >> rvanka_speed;
            else if (key == "rvanka_fov") ss >> rvanka_fov;
            else if (key == "rvanka_key") ss >> rvanka_key;
            else if (key == "rvanka_draw_fov") ss >> rvanka_draw_fov;
            else if (key == "col_rvanka_fov") ReadColor(col_rvanka_fov);

            else if (key == "wm_show_time") ss >> wm_show_time;
            else if (key == "wm_show_fps") ss >> wm_show_fps;
            else if (key == "wm_show_link") ss >> wm_show_link;

            else if (key == "col_miner_0") ReadColor(col_miner[0]);
            else if (key == "col_miner_1") ReadColor(col_miner[1]);
            else if (key == "col_miner_2") ReadColor(col_miner[2]);
            else if (key == "col_miner_3") ReadColor(col_miner[3]);
        }

        ToggleOpenDoors(enable_open_doors);
        ToggleAutoC(enable_autopc);
        ToggleAllSkills(enable_all_skills);
        if (enable_gym_bot) ToggleGymBot(true);
    }

    void DeleteConfig(const std::string& cfgName) {
        std::string path = folderPath + cfgName + ".cfg";
        remove(path.c_str());
        RefreshConfigs();
    }
}

bool IconButton(const char* icon, bool active, ImVec2 size) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiID id = window->GetID(icon);
    ImVec2 pos = window->DC.CursorPos;

    bool pressed = ImGui::InvisibleButton(icon, size);
    bool hovered = ImGui::IsItemHovered();

    float anim = GetSmoothAnim(icon, hovered || active, 12.0f);

    ImDrawList* draw = window->DrawList;

    ImColor bg_col = ImColor(40, 38, 45, (int)(255 * (active ? 0.3f : (hovered ? 0.2f : 0.0f))));

    if (active) {
        bg_col = ImColor(menu_color[0], menu_color[1], menu_color[2], 0.2f);
    }

    ImU32 icon_col = active ?
        IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255) :
        IM_COL32(140, 140, 150, 255);

    if (hovered && !active) {
        icon_col = IM_COL32(200, 200, 210, 255);
    }

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg_col, 8.0f);

    if (active) {
        ImU32 bar_col = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
        draw->AddRectFilled(ImVec2(pos.x, pos.y + size.y * 0.2f), ImVec2(pos.x + 3.0f, pos.y + size.y * 0.8f), bar_col, 2.0f);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 100), 8.0f);
    }

    if (font_icons) ImGui::PushFont(font_icons);

    ImVec2 text_size = ImGui::CalcTextSize(icon);
    ImVec2 text_pos = ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f);

    draw->AddText(text_pos, icon_col, icon);

    if (font_icons) ImGui::PopFont();

    return pressed;
}

void UpdateDynamicColors() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 accent = ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f);

    ImVec4 accent_hover = ImVec4(menu_color[0] + 0.1f, menu_color[1] + 0.1f, menu_color[2] + 0.1f, 1.0f);
    ImVec4 accent_active = ImVec4(menu_color[0] - 0.1f, menu_color[1] - 0.1f, menu_color[2] - 0.1f, 1.0f);
    ImVec4 accent_transparent = ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.25f);
    ImVec4 accent_transparent_hover = ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.45f);

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_active;
    colors[ImGuiCol_Button] = accent_transparent;
    colors[ImGuiCol_ButtonHovered] = accent_transparent_hover;
    colors[ImGuiCol_ButtonActive] = accent_active;
    colors[ImGuiCol_Header] = accent_transparent;
    colors[ImGuiCol_HeaderHovered] = accent_transparent_hover;
    colors[ImGuiCol_HeaderActive] = accent_active;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent_active;
    colors[ImGuiCol_ResizeGrip] = accent_transparent;
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accent_active;
    colors[ImGuiCol_Tab] = accent_transparent;
    colors[ImGuiCol_TabHovered] = accent_transparent_hover;
    colors[ImGuiCol_TabActive] = accent;
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.10f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = accent_transparent;
    colors[ImGuiCol_NavHighlight] = accent;
}

void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(0, 0);
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.ItemSpacing = ImVec2(12, 12);
    style.ScrollbarSize = 4.0f;
    style.ScrollbarRounding = 4.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 0.50f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
}

bool SkinButton(const char* label, const ImVec2& size_arg = ImVec2(0, 0)) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImGui::CalcItemSize(size_arg, 100.0f, 30.0f);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    ImGui::ItemSize(size, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImU32 col_bg = IM_COL32(20, 20, 20, 255);
    ImU32 col_border = IM_COL32(40, 40, 40, 255);
    ImU32 col_text = IM_COL32(180, 180, 180, 255);

    if (hovered) {
        col_bg = IM_COL32(30, 30, 30, 255);
        col_text = IM_COL32(255, 255, 255, 255);
    }
    if (held) {
        col_bg = IM_COL32(15, 15, 15, 255);
    }

    window->DrawList->AddRectFilled(bb.Min, bb.Max, col_bg, 4.0f);
    window->DrawList->AddRect(bb.Min, bb.Max, col_border, 4.0f, 0, 1.0f);

    ImVec2 textSize = ImGui::CalcTextSize(label);
    window->DrawList->AddText(ImVec2(bb.Min.x + (size.x - textSize.x) / 2, bb.Min.y + (size.y - textSize.y) / 2), col_text, label);

    return pressed;
}

bool CustomToggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const ImGuiStyle& style = g.Style;

    float height = 20.0f;
    float width = 36.0f;
    float radius = height * 0.5f;

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size_total = ImVec2(width + style.ItemSpacing.x + ImGui::CalcTextSize(label).x, height);

    const ImRect bb(pos, ImVec2(pos.x + size_total.x, pos.y + size_total.y));
    ImGui::ItemSize(size_total, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) *v = !*v;

    float anim = GetAnim(label, *v, 15.0f);

    ImVec4 bg_inactive = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    ImVec4 bg_active = ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f);

    ImVec4 curr_bg = ImVec4(
        Lerp(bg_inactive.x, bg_active.x, anim),
        Lerp(bg_inactive.y, bg_active.y, anim),
        Lerp(bg_inactive.z, bg_active.z, anim),
        1.0f
    );

    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::ColorConvertFloat4ToU32(curr_bg), radius);

    float circle_offset = radius + (width - radius * 2.0f) * anim;
    window->DrawList->AddCircleFilled(ImVec2(pos.x + circle_offset, pos.y + radius), radius - 2.0f, IM_COL32(255, 255, 255, 255));

    ImVec2 text_pos = ImVec2(pos.x + width + 15.0f, pos.y + (height / 2.0f) - (ImGui::CalcTextSize(label).y / 2.0f));
    ImU32 text_col = *v ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);
    window->DrawList->AddText(text_pos, text_col, label);

    return pressed;
}

void ToggleButton(const char* str_id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight();
    float width = height * 1.6f;
    float radius = height * 0.50f;

    if (ImGui::InvisibleButton(str_id, ImVec2(width, height))) *v = !*v;
    ImU32 accentCol = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
    ImU32 col_bg = *v ? accentCol : IM_COL32(40, 40, 40, 255);
    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    float circle_x = *v ? (p.x + width - radius) : (p.x + radius);
    draw_list->AddCircleFilled(ImVec2(circle_x, p.y + radius), radius - 3.0f, IM_COL32(255, 255, 255, 255));
}

HRESULT GenerateTexture(IDirect3DDevice9* pDevice, IDirect3DTexture9** ppD3Dtex, DWORD dwColour) {
    if (FAILED(pDevice->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, ppD3Dtex, NULL)))
        return E_FAIL;

    D3DLOCKED_RECT d3dlr;
    if (SUCCEEDED((*ppD3Dtex)->LockRect(0, &d3dlr, 0, 0))) {
        PBYTE pRect = (PBYTE)d3dlr.pBits;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                ((DWORD*)pRect)[x] = dwColour;
            }
            pRect += d3dlr.Pitch;
        }
        (*ppD3Dtex)->UnlockRect(0);
    }
    return S_OK;
}

void ToggleGameInput(bool state) {
    if (state) {
        BYTE patch[] = { 0x33, 0xC0, 0x0F, 0x84 };
        PatchBytes((void*)0x53F41F, patch, 4);
    }
    else {
        BYTE orig[] = { 0x85, 0xC0, 0x0F, 0x8C };
        PatchBytes((void*)0x53F41F, orig, 4);
    }
}

const char* GetWeaponName(int id) {
    switch (id) {
    case 0: return "Fist"; case 1: return "Knuckles"; case 2: return "Golf";
    case 3: return "Nightstick"; case 4: return "Knife"; case 5: return "Bat";
    case 22: return "Pistol"; case 23: return "Silenced"; case 24: return "Deagle";
    case 25: return "Shotgun"; case 26: return "Sawnoff"; case 27: return "Combat SG";
    case 28: return "Uzi"; case 29: return "MP5"; case 30: return "AK-47"; case 31: return "M4";
    case 32: return "Tec-9"; case 33: return "Rifle"; case 34: return "Sniper";
    case 38: return "Minigun";
    default: return "";
    }
}

void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h, ImU32 color, ImU32 outline) {
    float lw = w / 4.0f; float lh = h / 4.0f;
    dl->AddLine(ImVec2(x - 1, y - 1), ImVec2(x + lw, y - 1), outline, 3.0f);
    dl->AddLine(ImVec2(x - 1, y - 1), ImVec2(x - 1, y + lh), outline, 3.0f);
    dl->AddLine(ImVec2(x + w - lw, y - 1), ImVec2(x + w + 1, y - 1), outline, 3.0f);
    dl->AddLine(ImVec2(x + w + 1, y - 1), ImVec2(x + w + 1, y + lh), outline, 3.0f);
    dl->AddLine(ImVec2(x - 1, y + h - lh), ImVec2(x - 1, y + h + 1), outline, 3.0f);
    dl->AddLine(ImVec2(x - 1, y + h + 1), ImVec2(x + lw, y + h + 1), outline, 3.0f);
    dl->AddLine(ImVec2(x + w - lw, y + h + 1), ImVec2(x + w + 1, y + h + 1), outline, 3.0f);
    dl->AddLine(ImVec2(x + w + 1, y + h - lh), ImVec2(x + w + 1, y + h + 1), outline, 3.0f);
    dl->AddLine(ImVec2(x, y), ImVec2(x + lw, y), color);
    dl->AddLine(ImVec2(x, y), ImVec2(x, y + lh), color);
    dl->AddLine(ImVec2(x + w - lw, y), ImVec2(x + w, y), color);
    dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + lh), color);
    dl->AddLine(ImVec2(x, y + h - lh), ImVec2(x, y + h), color);
    dl->AddLine(ImVec2(x, y + h), ImVec2(x + lw, y + h), color);
    dl->AddLine(ImVec2(x + w, y + h - lh), ImVec2(x + w, y + h), color);
    dl->AddLine(ImVec2(x + w - lw, y + h), ImVec2(x + w, y + h), color);
}

void DrawHealthBar(ImDrawList* dl, float x, float y, float h, float hp, int style, bool showText, float refHeight) {
    float curHp = hp;
    if (curHp < 0.0f) curHp = 0.0f;
    float maxHp = 100.0f;
    if (curHp > 100.0f) maxHp = curHp;

    float scale = refHeight / 150.0f;

    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.4f) scale = 0.4f;

    float baseWidth = 3.0f;
    float barW = baseWidth * scale;
    float offset = 4.0f * scale;

    if (barW < 2.0f) barW = 2.0f;

    float pct = curHp / maxHp;

    ImVec2 bgMin = ImVec2(x - offset - barW, y);
    ImVec2 bgMax = ImVec2(x - offset, y + h);

    dl->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 150));
    dl->AddRect(ImVec2(bgMin.x - 1, bgMin.y - 1), ImVec2(bgMax.x + 1, bgMax.y + 1), IM_COL32(0, 0, 0, 255));

    if (curHp > 0.0f) {
        float fillHeight = h * pct;
        ImVec2 fillMin = ImVec2(bgMin.x, bgMax.y - fillHeight);
        ImVec2 fillMax = bgMax;

        if (style == 0) {
            ImU32 col = IM_COL32(0, 255, 0, 255);
            if (pct < 0.5f) col = IM_COL32(255, 200, 0, 255);
            if (pct < 0.25f) col = IM_COL32(255, 0, 0, 255);
            dl->AddRectFilled(fillMin, fillMax, col);
        }
        else {
            ImU32 cTop = ImColor(col_hp_grad_top[0], col_hp_grad_top[1], col_hp_grad_top[2]);
            ImU32 cBot = ImColor(col_hp_grad_bot[0], col_hp_grad_bot[1], col_hp_grad_bot[2]);
            dl->AddRectFilledMultiColor(fillMin, fillMax, cTop, cTop, cBot, cBot);
        }
    }

    if (showText && scale >= 0.5f) {
        char hpBuf[8];
        sprintf_s(hpBuf, "%d", (int)curHp);

        ImFont* font = ImGui::GetFont();
        float oldScale = font->Scale;

        font->Scale = scale * 0.8f;
        ImGui::PushFont(font);

        ImVec2 textSize = ImGui::CalcTextSize(hpBuf);
        ImVec2 centerPoint = ImVec2(bgMin.x + (barW / 2.0f), bgMin.y + (h / 2.0f));
        ImVec2 textPos = ImVec2(centerPoint.x - (textSize.x / 2.0f), centerPoint.y - (textSize.y / 2.0f));

        dl->AddText(ImVec2(textPos.x - 1, textPos.y), IM_COL32(0, 0, 0, 255), hpBuf);
        dl->AddText(ImVec2(textPos.x + 1, textPos.y), IM_COL32(0, 0, 0, 255), hpBuf);
        dl->AddText(ImVec2(textPos.x, textPos.y - 1), IM_COL32(0, 0, 0, 255), hpBuf);
        dl->AddText(ImVec2(textPos.x, textPos.y + 1), IM_COL32(0, 0, 0, 255), hpBuf);

        dl->AddText(textPos, IM_COL32(255, 255, 255, 255), hpBuf);

        ImGui::PopFont();
        font->Scale = oldScale;
    }
}

void DrawArmorBar(ImDrawList* dl, float x, float y, float w, float arm, ImU32 color, bool showText, float refHeight) {
    float curArm = arm;
    if (curArm < 0.0f) curArm = 0.0f;
    float maxArm = 100.0f;
    if (curArm > 100.0f) maxArm = curArm;

    float scale = refHeight / 150.0f;
    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.4f) scale = 0.4f;

    float baseHeight = 3.0f;
    float height = baseHeight * scale;
    float offset = 2.0f * scale;

    if (height < 2.0f) height = 2.0f;

    float pct = curArm / maxArm;

    ImVec2 bgMin = ImVec2(x, y + offset);
    ImVec2 bgMax = ImVec2(x + w, y + offset + height);

    dl->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 150));
    dl->AddRect(ImVec2(bgMin.x - 1, bgMin.y - 1), ImVec2(bgMax.x + 1, bgMax.y + 1), IM_COL32(0, 0, 0, 255));

    if (curArm > 0.0f) {
        float fillW = w * pct;
        if (fillW < 1.0f && curArm > 0.0f) fillW = 1.0f;
        ImVec2 fillMax = ImVec2(bgMin.x + fillW, bgMax.y);
        dl->AddRectFilled(bgMin, fillMax, color);
    }

    if (showText && curArm > 0.0f && scale >= 0.5f) {
        char armBuf[8];
        sprintf_s(armBuf, "%d", (int)curArm);

        ImFont* font = ImGui::GetFont();
        float oldScale = font->Scale;

        font->Scale = scale * 0.8f;
        ImGui::PushFont(font);

        ImVec2 textSize = ImGui::CalcTextSize(armBuf);
        ImVec2 centerPoint = ImVec2(bgMin.x + (w / 2.0f), bgMin.y + (height / 2.0f));
        ImVec2 textPos = ImVec2(centerPoint.x - (textSize.x / 2.0f), centerPoint.y - (textSize.y / 2.0f));

        dl->AddText(ImVec2(textPos.x - 1, textPos.y), IM_COL32(0, 0, 0, 255), armBuf);
        dl->AddText(ImVec2(textPos.x + 1, textPos.y), IM_COL32(0, 0, 0, 255), armBuf);
        dl->AddText(ImVec2(textPos.x, textPos.y - 1), IM_COL32(0, 0, 0, 255), armBuf);
        dl->AddText(ImVec2(textPos.x, textPos.y + 1), IM_COL32(0, 0, 0, 255), armBuf);

        dl->AddText(textPos, IM_COL32(255, 255, 255, 255), armBuf);

        ImGui::PopFont();
        font->Scale = oldScale;
    }
}

class CGameEntity {
public:
    void* vtable;
    char pad_04[20];
    void* m_pRwObject;
    char pad_1C[6];
    unsigned short m_nModelIndex;
};

class CGamePed : public CGameEntity {

};

bool IsEntityVisible(void* entity) {
    if (!entity) return false;
    return ((bool(__thiscall*)(void*))0x536BC0)(entity);
}

void GetPedBonePosition(void* ped, CVector* outPos, int boneId, bool updateSkin = false) {
    ((void(__thiscall*)(void*, CVector*, int, bool))0x5E4280)(ped, outPos, boneId, updateSkin);
}

void DrawGlowLine(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 col, float h) {
    if (h <= 0) return;

    float scale = h / 150.0f;
    if (scale < 0.4f) scale = 0.4f;
    if (scale > 1.0f) scale = 1.0f;

    ImVec4 color = ImGui::ColorConvertU32ToFloat4(col);
    int layers = 6;
    float base_thickness = 1.5f;

    for (int i = layers; i > 0; i--) {
        float layer_thickness = (base_thickness + (i * 3.5f)) * scale;

        float alpha = (glow_intensity / layers) * (1.0f - ((float)i / (float)layers));
        ImU32 layer_col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha * color.w));

        dl->AddLine(p1, p2, layer_col, layer_thickness);
    }
}

void DrawSkeleton(ImDrawList* dl, void* pGamePed, ImU32 color, float h) {
    const int bones[][2] = { {5, 4}, {4, 3}, {3, 1}, {4, 22}, {22, 23}, {23, 24}, {4, 32}, {32, 33}, {33, 34}, {1, 51}, {51, 52}, {52, 53}, {1, 41}, {41, 42}, {42, 43} };

    for (int i = 0; i < 15; i++) {
        CVector bone1Pos, bone2Pos;
        GetPedBonePosition(pGamePed, &bone1Pos, bones[i][0]);
        GetPedBonePosition(pGamePed, &bone2Pos, bones[i][1]);

        Vector2 s1 = CalcScreenCoords(bone1Pos.x, bone1Pos.y, bone1Pos.z);
        Vector2 s2 = CalcScreenCoords(bone2Pos.x, bone2Pos.y, bone2Pos.z);

        if (s1.x != -1 && s2.x != -1) {
            ImVec2 p1 = ImVec2(s1.x, s1.y);
            ImVec2 p2 = ImVec2(s2.x, s2.y);

            if (enable_esp_glow) {
                DrawGlowLine(dl, p1, p2, color, h);
            }

            dl->AddLine(p1, p2, color, skeleton_thickness);
        }
    }
}

bool IsLineOfSightClear(CVector* start, CVector* end, bool bBuildings, bool bVehicles, bool bPeds, bool bObjects, bool bDummies, bool bSeeThrough, bool bCameraIgnore) {
    return ((bool(__cdecl*)(CVector*, CVector*, bool, bool, bool, bool, bool, bool, bool))0x56A490)(
        start, end, bBuildings, bVehicles, bPeds, bObjects, bDummies, bSeeThrough, bCameraIgnore
        );
}

bool IsPosVisible(CVector targetPos) {
    void* pLocalPed = (void*)*(DWORD*)0xB6F5F0;
    if (!pLocalPed) return false;

    CVector startPos;
    GetPedBonePosition(pLocalPed, &startPos, 6);

    float dist = sqrt(pow(targetPos.x - startPos.x, 2) +
        pow(targetPos.y - startPos.y, 2) +
        pow(targetPos.z - startPos.z, 2));

    if (dist > 150.0f) return false;
    if (dist < 1.0f) return true;

    float offset = 0.6f;
    startPos.x += ((targetPos.x - startPos.x) / dist) * offset;
    startPos.y += ((targetPos.y - startPos.y) / dist) * offset;
    startPos.z += ((targetPos.z - startPos.z) / dist) * offset;

    return GetIsLineOfSightClear(&startPos, &targetPos, true, true, false, true, false, false, false);
}

bool Transform3DTo2D(CVector* world, CVector* screen) {
    return ((bool(__cdecl*)(CVector*, CVector*))0x71DAB0)(world, screen);
}

DWORD GetPlayerColorR3(int id) {
    static DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
    if (!sampDll) return 0;
    DWORD* colors = (DWORD*)(sampDll + 0x151578);
    if (IsBadReadPtr(colors, 4)) return 0;
    return colors[id];
}

float GetRandFloat(float min, float max) {
    return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}

bool GetMagicTarget(CVector& outTargetPos, int& outTargetID) {
    if (!RefNetGame() || !RefNetGame()->GetPlayerPool()) return false;

    auto poolApi = RefNetGame()->GetPlayerPool();
    int localId = GetLocalPlayerIdR3();
    DWORD localColor = (localId != -1) ? GetPlayerColorR3(localId) : 0;

    void* pLocalPed = (void*)*(DWORD*)0xB6F5F0;
    if (!pLocalPed) return false;

    uint8_t weaponID = *(uint8_t*)((DWORD)pLocalPed + 0x718);

    CVector startTrace;

    if (weaponID == 34) {
        float* camPos = (float*)0xB6F9CC;
        startTrace.x = camPos[12];
        startTrace.y = camPos[13];
        startTrace.z = camPos[14];
    }
    else {
        GetPedBonePosition(pLocalPed, &startTrace, 6);
        startTrace.z += 0.3f;
    }

    float searchDist = silent_max_dist;
    if (weaponID == 34 && searchDist < 300.0f) searchDist = 600.0f;

    float closestDist = searchDist;
    bool found = false;

    std::vector<int> bones;
    if (silent_random_shot) {
        bones = { 3, 4, 5, 6, 22, 32, 42 };
        for (size_t i = 0; i < bones.size(); i++) {
            std::swap(bones[i], bones[rand() % bones.size()]);
        }
    }
    else {
        bones = { 3 };
    }

    for (int i = 0; i < 1004; i++) {
        if (i == localId) continue;
        if (!poolApi->IsConnected(i)) continue;

        CRemotePlayer* remotePlayer = poolApi->GetPlayer(i);
        if (!remotePlayer || !remotePlayer->m_pPed) continue;
        if (remotePlayer->m_pPed->IsDead()) continue;

        void* pGamePed = remotePlayer->m_pPed->m_pGamePed;
        if (!pGamePed) continue;

        if (silent_ignore_team && localId != -1) {
            if ((localColor & 0xFFFFFF) == (GetPlayerColorR3(i) & 0xFFFFFF)) continue;
        }

        for (int boneID : bones) {
            CVector enemyBonePos;
            GetPedBonePosition(pGamePed, &enemyBonePos, boneID);

            float dist = sqrt(pow(enemyBonePos.x - startTrace.x, 2) +
                pow(enemyBonePos.y - startTrace.y, 2) +
                pow(enemyBonePos.z - startTrace.z, 2));

            if (dist > closestDist) break;

            if (!silent_ignore_walls) {
                if (!IsPosVisible(enemyBonePos)) {
                    continue;
                }
            }

            closestDist = dist;
            outTargetPos = enemyBonePos;
            outTargetID = i;
            found = true;

            break;
        }
    }
    return found;
}

ImVec2 GetFOVCenter() {
    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x / 2.0f;
    float cy = io.DisplaySize.y / 2.0f;

    if (real_crosshair_fov && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
        float gameCrosshairX = *(float*)0xB6EC10;
        float gameCrosshairY = *(float*)0xB6EC14;

        if (gameCrosshairX > 1.0f && gameCrosshairX < io.DisplaySize.x &&
            gameCrosshairY > 1.0f && gameCrosshairY < io.DisplaySize.y) {

            return ImVec2(gameCrosshairX + mouse_offset_x, gameCrosshairY + mouse_offset_y);
        }
    }

    return ImVec2(cx + mouse_offset_x, cy + mouse_offset_y);
}

bool __fastcall hkFireInstantHit(void* pThis, void* edx, void* pFiringEntity, CVector* pOrigin, CVector* pMuzzlePosn, void* pTargetEntity, CVector* pTarget, CVector* pOriginForDriveBy, bool bUnk, bool bMuzzle) {
    void* pLocalPed = (void*)*(DWORD*)0xB6F5F0;

    if (silent_enabled && pFiringEntity == pLocalPed && RefNetGame()) {

        static CVector aimTargetPos = { 0, 0, 0 };
        int aimTargetID = -1;

        if (GetMagicTarget(aimTargetPos, aimTargetID)) {

            if (silent_magic) {
                return oFireInstantHit(pThis, pFiringEntity, pOrigin, pMuzzlePosn, pTargetEntity, &aimTargetPos, pOriginForDriveBy, bUnk, bMuzzle);
            }
            else {
                Vector2 screenPos = CalcScreenCoords(aimTargetPos.x, aimTargetPos.y, aimTargetPos.z);
                if (screenPos.x != -1 && screenPos.y != -1) {
                    ImVec2 center = GetFOVCenter();
                    float dist2D = sqrt(pow(center.x - screenPos.x, 2) + pow(center.y - screenPos.y, 2));
                    if (dist2D <= silent_fov) {
                        return oFireInstantHit(pThis, pFiringEntity, pOrigin, pMuzzlePosn, pTargetEntity, &aimTargetPos, pOriginForDriveBy, bUnk, bMuzzle);
                    }
                }
            }
        }
    }
    return oFireInstantHit(pThis, pFiringEntity, pOrigin, pMuzzlePosn, pTargetEntity, pTarget, pOriginForDriveBy, bUnk, bMuzzle);
}

void RunMouseAimbot() {
    if (!bind_mouse_aim.value || !*bind_mouse_aim.value || show_menu) {
        mouse_locked_target_id = -1;
        mouse_residue_x = 0.0f;
        mouse_residue_y = 0.0f;
        return;
    }

    if (!RefNetGame() || !RefNetGame()->GetPlayerPool()) return;
    auto poolApi = RefNetGame()->GetPlayerPool();
    int localId = GetLocalPlayerIdR3();

    ImVec2 center = GetFOVCenter();
    float centerX = center.x;
    float centerY = center.y;

    const int SKIN_EVENT_1 = 2862;
    const int SKIN_EVENT_2 = 2863;

    int targetBoneId = (mouse_aim_bone == 1) ? 6 : 3;

    if (mouse_locked_target_id != -1) {
        bool shouldReset = true;
        if (poolApi->IsConnected(mouse_locked_target_id)) {
            CRemotePlayer* remote = poolApi->GetPlayer(mouse_locked_target_id);
            if (remote && remote->m_pPed && !remote->m_pPed->IsDead()) {
                void* pGamePed = remote->m_pPed->m_pGamePed;
                if (pGamePed) {
                    if (!mouse_check_vis || IsEntityVisible(pGamePed)) {
                        shouldReset = false;
                    }
                }
            }
        }
        if (shouldReset) mouse_locked_target_id = -1;
    }

    if (mouse_locked_target_id == -1) {
        int bestTarget = -1;
        float bestDist = mouse_fov;

        int mySkin = -1;
        auto localPlayer = poolApi->GetLocalPlayer();
        if (localPlayer && localPlayer->m_pPed && localPlayer->m_pPed->m_pGamePed) {
            mySkin = ((CGamePed*)localPlayer->m_pPed->m_pGamePed)->m_nModelIndex;
        }

        for (int i = 0; i < 1004; i++) {
            if (i == localId || !poolApi->IsConnected(i)) continue;

            CRemotePlayer* remote = poolApi->GetPlayer(i);
            if (!remote || !remote->m_pPed || remote->m_pPed->IsDead()) continue;
            void* pGamePed = remote->m_pPed->m_pGamePed;
            if (!pGamePed) continue;

            if (mouse_check_vis) {
                CVector aimBonePos;
                GetPedBonePosition(pGamePed, &aimBonePos, targetBoneId);

                if (!IsPosVisible(aimBonePos)) continue;
            }

            if (enable_event_mode && mySkin != -1) {
                int targetSkin = ((CGamePed*)pGamePed)->m_nModelIndex;
                bool amISpecial = (mySkin == SKIN_EVENT_1 || mySkin == SKIN_EVENT_2);
                bool isTargetSpecial = (targetSkin == SKIN_EVENT_1 || targetSkin == SKIN_EVENT_2);
                if (amISpecial == isTargetSpecial) continue;
            }

            CVector enemyPos;
            GetPedBonePosition(pGamePed, &enemyPos, targetBoneId);

            Vector2 screenPos = CalcScreenCoords(enemyPos.x, enemyPos.y, enemyPos.z);
            if (screenPos.x == -1 || screenPos.y == -1) continue;

            float dist = sqrt(pow(centerX - screenPos.x, 2) + pow(centerY - screenPos.y, 2));
            if (dist < bestDist) {
                bestDist = dist;
                bestTarget = i;
            }
        }
        mouse_locked_target_id = bestTarget;
    }

    if (mouse_locked_target_id != -1) {
        CRemotePlayer* remote = poolApi->GetPlayer(mouse_locked_target_id);
        if (remote && remote->m_pPed) {
            void* pGamePed = remote->m_pPed->m_pGamePed;
            if (pGamePed) {
                CVector enemyPos;
                GetPedBonePosition(pGamePed, &enemyPos, targetBoneId);

                float velX = *(float*)((DWORD)pGamePed + 0x44);
                float velY = *(float*)((DWORD)pGamePed + 0x48);
                float velZ = *(float*)((DWORD)pGamePed + 0x4C);

                if (mouse_predict > 0.01f) {
                    enemyPos.x += velX * mouse_predict;
                    enemyPos.y += velY * mouse_predict;
                    enemyPos.z += velZ * mouse_predict;
                }

                Vector2 screenPos = CalcScreenCoords(enemyPos.x, enemyPos.y, enemyPos.z);

                if (screenPos.x != -1 && screenPos.y != -1) {
                    float targetX = screenPos.x;
                    float targetY = screenPos.y;

                    float deltaX = targetX - centerX;
                    float deltaY = targetY - centerY;

                    float smooth = (mouse_smooth < 1.0f) ? 1.0f : mouse_smooth;

                    if (abs(deltaX) < 15.0f && abs(deltaY) < 15.0f) {
                        smooth = 1.0f;
                    }

                    float moveX_f = deltaX / smooth;
                    float moveY_f = deltaY / smooth;

                    moveX_f += mouse_residue_x;
                    moveY_f += mouse_residue_y;

                    int moveX = (int)moveX_f;
                    int moveY = (int)moveY_f;

                    mouse_residue_x = moveX_f - moveX;
                    mouse_residue_y = moveY_f - moveY;

                    if (moveX != 0 || moveY != 0) {
                        mouse_event(MOUSEEVENTF_MOVE, moveX, moveY, 0, 0);
                    }
                }
            }
        }
    }
}

void UpdateGhostSurfLogic(float px, float py, float pz) {
    if (!RefNetGame() || !RefNetGame()->GetObjectPool()) return;

    auto pool = RefNetGame()->GetObjectPool();

    int closestObject = -1;
    float closestDistance = 3.402823466e+38F;

    HasActiveAnchor = false;

    for (int i = 0; i < 1000; i++) {
        if (pool->m_pObject[i] == nullptr) continue;

        sampapi::v037r3::CObject* obj = pool->Get(i);
        if (!obj) continue;

        if (obj->m_nModel == 12114) {
            closestObject = i;
            break;
        }
    }

    if (closestObject != -1) {
        LastObj = closestObject + 2000;
        sampapi::v037r3::CObject* obj = pool->Get(closestObject);
        if (obj) {
            LastObjPos = obj->m_position;
            HasActiveAnchor = true;
        }
    }
}

int GetAnyStreamedVehicleID() {
    if (RefNetGame() && RefNetGame()->GetVehiclePool()) {
        auto pool = RefNetGame()->GetVehiclePool();

        for (int i = 0; i < 2000; i++) {
            if (pool->Get(i)) {
                if (pool->Get(i)->m_pGameVehicle != nullptr) {
                    return i;
                }
            }
        }
    }
    return -1;
}

struct GtaPickup {
    float x, y, z;
    float w;
    int type;
};

bool IsPlayerNearPickup(float px, float py, float pz) {
    DWORD pickupPoolAddr = 0x9788C0;

    for (int i = 0; i < 620; i++) {
        DWORD pPickup = pickupPoolAddr + (i * 0x20);
        int type = *(int*)(pPickup + 0x1C);

        if (type == 0) continue;

        float pickX = *(float*)(pPickup + 0x0);
        float pickY = *(float*)(pPickup + 0x4);
        float pickZ = *(float*)(pPickup + 0x8);

        if (abs(px - pickX) < 1.2f && abs(py - pickY) < 1.2f && abs(pz - pickZ) < 2.0f) {
            return true;
        }
    }
    return false;
}

bool __fastcall hkRakPeerSend(void* pThis, void* edx, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel) {
    if (!init || !RefNetGame()) return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);

    uint8_t packetId;
    bitStream->ResetReadPointer();
    if (!bitStream->Read(packetId)) return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);

    if (packetId == ID_BULLET_SYNC) {
        stBulletData bData;
        bitStream->ResetReadPointer();
        bitStream->Read(packetId);
        bitStream->Read((char*)&bData, sizeof(stBulletData));

        bool isModified = false;

        if (silent_enabled && silent_magic) {
            CVector magicPos;
            int magicID = -1;

            if (GetMagicTarget(magicPos, magicID)) {
                bData.byteType = 1;
                bData.sTargetID = (uint16_t)magicID;
                bData.fTarget[0] = magicPos.x; bData.fTarget[1] = magicPos.y; bData.fTarget[2] = magicPos.z;

                float rndX = GetRandomFloat(-0.03f, 0.03f);
                float rndY = GetRandomFloat(-0.03f, 0.03f);
                float rndZ = GetRandomFloat(-0.03f, 0.03f);
                bData.fCenter[0] = magicPos.x + rndX;
                bData.fCenter[1] = magicPos.y + rndY;
                bData.fCenter[2] = magicPos.z + rndZ;

                if (bData.byteWeaponID == 34) {
                    DWORD dwCamMatrix = 0xB6F9CC;
                    if (!IsBadReadPtr((void*)dwCamMatrix, 4)) {
                        float* camPos = (float*)(dwCamMatrix);
                        bData.fOrigin[0] = camPos[12]; bData.fOrigin[1] = camPos[13]; bData.fOrigin[2] = camPos[14];
                    }
                }
                else {
                    void* pLocalPed = (void*)*(DWORD*)0xB6F5F0;
                    if (pLocalPed) {
                        CVector myHead; GetPedBonePosition(pLocalPed, &myHead, 6);
                        float distOrigin = sqrt(pow(bData.fOrigin[0] - myHead.x, 2) + pow(bData.fOrigin[1] - myHead.y, 2));
                        if (distOrigin > 1.5f) {
                            bData.fOrigin[0] = myHead.x; bData.fOrigin[1] = myHead.y; bData.fOrigin[2] = myHead.z;
                        }
                    }
                }
                isModified = true;
            }
        }

        if (enable_tracers) {
            CVector vecStart = { bData.fOrigin[0], bData.fOrigin[1], bData.fOrigin[2] };
            CVector vecEnd = { bData.fTarget[0], bData.fTarget[1], bData.fTarget[2] };

            AddBulletTrace(vecStart, vecEnd);
        }

        bitStream->Reset();
        bitStream->Write((uint8_t)ID_BULLET_SYNC);
        bitStream->Write((char*)&bData, sizeof(stBulletData));

        return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);
    }

    if (enable_faction_bypass && packetId == ID_VEHICLE_SYNC) {
        stInCarData carData;
        bitStream->Read((char*)&carData, sizeof(stInCarData));

        float pos[3] = { 0,0,0 };
        float moveSpeed[3] = { 0,0,0 };
        float turnSpeed[3] = { 0,0,0 };
        float roll[3] = { 1,0,0 };
        float dir[3] = { 0,1,0 };
        float health = 1000.0f;
        bool dataRead = false;

        if (RefNetGame()->GetVehiclePool()) {
            sampapi::v037r3::CVehicle* pVeh = RefNetGame()->GetVehiclePool()->Get(carData.sVehicleID);
            if (pVeh && pVeh->m_pGameVehicle) {
                uintptr_t pGameVeh = (uintptr_t)pVeh->m_pGameVehicle;
                if (!IsBadReadPtr((void*)(pGameVeh + 0x14), 4)) {
                    RwMatrix* pMat = *(RwMatrix**)(pGameVeh + 0x14);
                    if (pMat && !IsBadReadPtr(pMat, sizeof(RwMatrix))) {
                        pos[0] = pMat->pos.x; pos[1] = pMat->pos.y; pos[2] = pMat->pos.z;
                        roll[0] = pMat->right.x; roll[1] = pMat->right.y; roll[2] = pMat->right.z;
                        dir[0] = pMat->at.x;    dir[1] = pMat->at.y;    dir[2] = pMat->at.z;
                        dataRead = true;
                    }
                }
                if (!IsBadReadPtr((void*)(pGameVeh + 0x44), 16)) {
                    float* v = (float*)(pGameVeh + 0x44);
                    float* t = (float*)(pGameVeh + 0x50);
                    moveSpeed[0] = v[0]; moveSpeed[1] = v[1]; moveSpeed[2] = v[2];
                    turnSpeed[0] = t[0]; turnSpeed[1] = t[1]; turnSpeed[2] = t[2];
                }
                if (!IsBadReadPtr((void*)(pGameVeh + 0x4C0), 4)) {
                    health = *(float*)(pGameVeh + 0x4C0);
                }
            }
        }

        if (!dataRead) {
            pos[0] = carData.fPosition[0]; pos[1] = carData.fPosition[1]; pos[2] = carData.fPosition[2];
            moveSpeed[0] = carData.fMoveSpeed[0]; moveSpeed[1] = carData.fMoveSpeed[1]; moveSpeed[2] = carData.fMoveSpeed[2];
        }

        stPassengerData passData;
        passData.sVehicleID = carData.sVehicleID;
        passData.byteSeatID = 1;
        passData.byteCurrentWeapon = carData.byteCurrentWeapon;
        passData.byteHealth = carData.bytePlayerHealth;
        passData.byteArmor = carData.byteArmor;
        passData.sLeftRightKeys = carData.sLeftRightKeys;
        passData.sUpDownKeys = carData.sUpDownKeys;
        passData.sKeys = carData.sKeys;
        passData.fPosition[0] = pos[0];
        passData.fPosition[1] = pos[1];
        passData.fPosition[2] = pos[2];

        BitStream bsPass;
        bsPass.Write((uint8_t)ID_PASSENGER_SYNC);
        bsPass.Write((char*)&passData, sizeof(stPassengerData));
        oRakPeerSend(pThis, &bsPass, priority, reliability, orderingChannel);

        stUnoccupiedData unocData;
        unocData.sVehicleID = carData.sVehicleID;
        unocData.byteSeatID = 0;
        unocData.fHealth = health;
        unocData.fPosition[0] = pos[0];
        unocData.fPosition[1] = pos[1];
        unocData.fPosition[2] = pos[2];
        unocData.fRoll[0] = roll[0]; unocData.fRoll[1] = roll[1]; unocData.fRoll[2] = roll[2];
        unocData.fDirection[0] = dir[0]; unocData.fDirection[1] = dir[1]; unocData.fDirection[2] = dir[2];
        unocData.fMoveSpeed[0] = moveSpeed[0];
        unocData.fMoveSpeed[1] = moveSpeed[1];
        unocData.fMoveSpeed[2] = moveSpeed[2];
        unocData.fTurnSpeed[0] = turnSpeed[0];
        unocData.fTurnSpeed[1] = turnSpeed[1];
        unocData.fTurnSpeed[2] = turnSpeed[2];

        BitStream bsUnoc;
        bsUnoc.Write((uint8_t)ID_UNOCCUPIED_SYNC);
        bsUnoc.Write((char*)&unocData, sizeof(stUnoccupiedData));
        oRakPeerSend(pThis, &bsUnoc, priority, reliability, orderingChannel);

        return true;
    }

    bitStream->ResetReadPointer();
    if (!bitStream->Read(packetId)) return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);

    bool isPacketModified = false;

    auto ApplyFakeSpeed = [&](float* moveSpeed) {
        if (is_teleporting) {
            moveSpeed[0] = 0.5f; moveSpeed[1] = 0.5f; moveSpeed[2] = 0.0f;
            return;
        }
        if (enable_airbreak) {
            float camAngle = *(float*)0xB6F258;
            bool pressing = false;
            float vX = 0, vY = 0, vZ = 0;
            if (GetAsyncKeyState(0x57)) { vX -= sin(camAngle); vY += cos(camAngle); pressing = true; }
            if (GetAsyncKeyState(0x53)) { vX += sin(camAngle); vY -= cos(camAngle); pressing = true; }
            if (GetAsyncKeyState(0x41)) { vX -= cos(camAngle); vY -= sin(camAngle); pressing = true; }
            if (GetAsyncKeyState(0x44)) { vX += cos(camAngle); vY += sin(camAngle); pressing = true; }
            if (GetAsyncKeyState(VK_LSHIFT)) { vZ += 0.8f; pressing = true; }
            if (GetAsyncKeyState(VK_LCONTROL)) { vZ -= 0.8f; pressing = true; }
            if (pressing) {
                float mult = 0.6f;
                moveSpeed[0] = vX * mult; moveSpeed[1] = vY * mult; moveSpeed[2] = vZ * mult;
            }
        }
        };

    if (packetId == ID_PLAYER_SYNC) {
        stOnFootData data;
        if (bitStream->Read((char*)&data, sizeof(stOnFootData))) {

            bool nearPickup = IsPlayerNearPickup(data.fPosition[0], data.fPosition[1], data.fPosition[2]);

            if (enable_tp_exploit || g_PieTpActive) {
                int vehID = GetAnyStreamedVehicleID();

                float tpX = g_PieTpActive ? g_PieTpTarget.x : 0.0f;
                float tpY = g_PieTpActive ? g_PieTpTarget.y : 0.0f;
                float tpZ = g_PieTpActive ? g_PieTpTarget.z : 5.0f;

                if (vehID != -1) {
                    ForceLocalPosition(tpX, tpY, tpZ);

                    stPassengerData passData;
                    memset(&passData, 0, sizeof(stPassengerData));

                    passData.sVehicleID = (uint16_t)vehID;
                    passData.byteSeatID = 1;

                    passData.byteHealth = data.byteHealth;
                    passData.byteArmor = data.byteArmor;
                    passData.byteCurrentWeapon = data.byteCurrentWeapon;
                    passData.sKeys = data.sKeys;
                    passData.sLeftRightKeys = data.sLeftRightKeys;
                    passData.sUpDownKeys = data.sUpDownKeys;

                    passData.fPosition[0] = tpX;
                    passData.fPosition[1] = tpY;
                    passData.fPosition[2] = tpZ;

                    bitStream->Reset();
                    bitStream->Write((uint8_t)ID_PASSENGER_SYNC);
                    bitStream->Write((char*)&passData, sizeof(stPassengerData));

                    bool result = oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);

                    if (g_PieTpActive) {
                        g_PieTpActive = false;
                    }

                    return result;
                }
                else {
                    ForceLocalPosition(tpX, tpY, tpZ);

                    data.fPosition[0] = tpX;
                    data.fPosition[1] = tpY;
                    data.fPosition[2] = tpZ;

                    bitStream->Reset();
                    bitStream->Write((uint8_t)ID_PLAYER_SYNC);
                    bitStream->Write((char*)&data, sizeof(stOnFootData));

                    if (g_PieTpActive) {
                        g_PieTpActive = false;
                    }
                }
            }

            if (enable_obj_surf) {
                if (!nearPickup) {
                    int vehID = GetAnyStreamedVehicleID();
                    if (vehID != -1) {
                        g_GhostIsAttached = true;
                        g_GhostAttachedID = vehID;

                        stPassengerData passData;
                        memset(&passData, 0, sizeof(stPassengerData));
                        passData.sVehicleID = (uint16_t)vehID;
                        passData.byteSeatID = 1;
                        passData.byteHealth = data.byteHealth;
                        passData.byteArmor = data.byteArmor;
                        passData.byteCurrentWeapon = data.byteCurrentWeapon;
                        passData.sKeys = data.sKeys;
                        passData.sLeftRightKeys = data.sLeftRightKeys;
                        passData.sUpDownKeys = data.sUpDownKeys;
                        passData.fPosition[0] = data.fPosition[0];
                        passData.fPosition[1] = data.fPosition[1];
                        passData.fPosition[2] = data.fPosition[2];

                        bitStream->Reset();
                        bitStream->Write((uint8_t)ID_PASSENGER_SYNC);
                        bitStream->Write((char*)&passData, sizeof(stPassengerData));

                        return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);
                    }
                    else {
                        g_GhostIsAttached = false;
                        g_GhostAttachedID = -1;
                    }
                }
            }
            else {
                g_GhostIsAttached = false;
            }

            if (enable_rvanka && rvanka_mode == 1 && (GetAsyncKeyState(rvanka_key) & 0x8000)) {
                auto pool = RefNetGame()->GetPlayerPool();
                if (pool) {
                    for (int i = 0; i < 1004; i++) {
                        if (!pool->IsConnected(i) || i == GetLocalPlayerIdR3()) continue;
                        CRemotePlayer* pRemote = pool->GetPlayer(i);
                        if (!pRemote || !pRemote->m_pPed || pRemote->m_pPed->IsDead()) continue;
                        CMatrix matrix; pRemote->m_pPed->GetMatrix(&matrix);
                        Vector2 sc = CalcScreenCoords(matrix.pos.x, matrix.pos.y, matrix.pos.z);
                        if (IsPointInFOV(sc.x, sc.y, rvanka_fov)) {
                            data.fPosition[0] = matrix.pos.x; data.fPosition[1] = matrix.pos.y; data.fPosition[2] = matrix.pos.z;
                            data.fMoveSpeed[2] = rvanka_speed;
                            isPacketModified = true; break;
                        }
                    }
                }
            }
            else if (enable_invis) {
                int bestVehID = -1;
                float closestDist = 150.0f;

                if (RefNetGame()->GetVehiclePool()) {
                    CVector myPos;
                    myPos.x = data.fPosition[0];
                    myPos.y = data.fPosition[1];
                    myPos.z = data.fPosition[2];

                    for (int i = 0; i < 2000; i++) {
                        auto pVeh = RefNetGame()->GetVehiclePool()->Get(i);

                        if (!pVeh) continue;

                        if (!pVeh->m_pGameVehicle) continue;

                        DWORD pGameVeh = (DWORD)pVeh->m_pGameVehicle;
                        DWORD pMatrix = *(DWORD*)(pGameVeh + 0x14);

                        if (pMatrix) {
                            float vx = *(float*)(pMatrix + 0x30);
                            float vy = *(float*)(pMatrix + 0x34);
                            float vz = *(float*)(pMatrix + 0x38);

                            float dist = sqrt(pow(myPos.x - vx, 2) + pow(myPos.y - vy, 2) + pow(myPos.z - vz, 2));

                            if (dist < closestDist) {
                                closestDist = dist;
                                bestVehID = i;
                            }
                        }
                    }
                }

                if (bestVehID != -1) {
                    data.fPosition[2] -= 15.0f;

                    data.sSurfingVehicleID = bestVehID;

                    data.fSurfingOffsets[0] = 0.0f;
                    data.fSurfingOffsets[1] = 0.0f;
                    data.fSurfingOffsets[2] = -20000.0f;
                }
                else {
                    data.fPosition[2] -= invis_z;
                    data.sSurfingVehicleID = 0;
                    data.fSurfingOffsets[0] = 0.0f; data.fSurfingOffsets[1] = 0.0f; data.fSurfingOffsets[2] = 0.0f;
                }

                data.sCurrentAnimationID = 1130;
                data.sAnimFlags = 0;

                data.fMoveSpeed[0] = 0.0f;
                data.fMoveSpeed[1] = 0.0f;
                data.fMoveSpeed[2] = -0.01f;

                isPacketModified = true;
            }

            if (enable_airbreak || is_teleporting) {
                data.sCurrentAnimationID = 1130;
                data.sAnimFlags = 0;
                ApplyFakeSpeed(data.fMoveSpeed);
                isPacketModified = true;
            }

            if (isPacketModified) {
                bitStream->ResetWritePointer();
                bitStream->Write(packetId);
                bitStream->Write((char*)&data, sizeof(stOnFootData));
            }
        }
    }
    else if (packetId == ID_VEHICLE_SYNC) {
        stInCarData data;
        if (bitStream->Read((char*)&data, sizeof(stInCarData))) {

            if (enable_rvanka && rvanka_mode == 0 && (GetAsyncKeyState(rvanka_key) & 0x8000)) {
                auto pool = RefNetGame()->GetPlayerPool();
                if (pool) {
                    for (int i = 0; i < 1004; i++) {
                        if (!pool->IsConnected(i) || i == GetLocalPlayerIdR3()) continue;
                        CRemotePlayer* pRemote = pool->GetPlayer(i);
                        if (!pRemote || !pRemote->m_pPed || pRemote->m_pPed->IsDead()) continue;
                        CMatrix matrix; pRemote->m_pPed->GetMatrix(&matrix);
                        Vector2 sc = CalcScreenCoords(matrix.pos.x, matrix.pos.y, matrix.pos.z);
                        if (IsPointInFOV(sc.x, sc.y, rvanka_fov)) {
                            data.fPosition[0] = matrix.pos.x; data.fPosition[1] = matrix.pos.y; data.fPosition[2] = matrix.pos.z;
                            data.fMoveSpeed[2] = rvanka_speed;
                            isPacketModified = true; break;
                        }
                    }
                }
            }
            else if (enable_invis) {
                data.fPosition[2] -= 50.0f;

                data.fMoveSpeed[0] = 0.0f;
                data.fMoveSpeed[1] = 0.0f;
                data.fMoveSpeed[2] = -0.05f;

                isPacketModified = true;
            }

            if (enable_airbreak || is_teleporting) {
                ApplyFakeSpeed(data.fMoveSpeed);
                isPacketModified = true;
            }

            if (isPacketModified) {
                bitStream->ResetWritePointer();
                bitStream->Write(packetId);
                bitStream->Write((char*)&data, sizeof(stInCarData));
            }
        }
    }
    else if (packetId == ID_PASSENGER_SYNC) {
        stPassengerData data;
        if (bitStream->Read((char*)&data, sizeof(stPassengerData))) {
            if (enable_invis) {
                data.fPosition[2] -= 50.0f;
                data.sKeys = 0;
                data.sLeftRightKeys = 0;
                data.sUpDownKeys = 0;

                bitStream->ResetWritePointer();
                bitStream->Write(packetId);
                bitStream->Write((char*)&data, sizeof(stPassengerData));
            }
        }
    }

    bitStream->ResetReadPointer();
    return oRakPeerSend(pThis, bitStream, priority, reliability, orderingChannel);
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if ((show_menu || tp_pie_open) && init) {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_KEYUP && wParam == VK_DELETE) {
        show_menu = !show_menu;
        show_bind_window = false;
        current_bind_edit = nullptr;

        if (init) {
            ImGui::GetIO().MouseDrawCursor = (show_menu || tp_pie_open);

            ToggleGameInput(show_menu || tp_pie_open);

            if (!show_menu && !tp_pie_open) {
                memset((void*)0xB73458, 0, 0x132);
                RECT rect; GetWindowRect(window, &rect);
                oSetCursorPos(rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2);
            }
        }
        return 1;
    }

    if (uMsg == WM_KEYDOWN && wParam == VK_DELETE) {
        return 1;
    }

    if ((show_menu || tp_pie_open) && init) {
        ImGuiIO& io = ImGui::GetIO();

        switch (uMsg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEMOVE:
            return 1;
        }

        if ((uMsg == WM_KEYDOWN || uMsg == WM_CHAR) && io.WantCaptureKeyboard) {
            return 1;
        }
    }

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

HRESULT __stdcall hkDrawIndexedPrimitive(LPDIRECT3DDEVICE9 pDevice, D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {

    if (enable_chams && pDevice) {
        IDirect3DVertexBuffer9* pStreamData = nullptr;
        UINT iOffsetInBytes, iStride;

        if (pDevice->GetStreamSource(0, &pStreamData, &iOffsetInBytes, &iStride) == D3D_OK) {
            pStreamData->Release();

            bool isSkinnedStride = (iStride == 36 || iStride == 40 || iStride == 44 ||
                iStride == 48 || iStride == 52 || iStride == 56 ||
                iStride == 60 || iStride == 64);

            if (isSkinnedStride) {

                IDirect3DVertexShader9* pShader = nullptr;
                pDevice->GetVertexShader(&pShader);
                bool isPlayer = (pShader != nullptr);
                if (pShader) pShader->Release();

                if (!isPlayer && NumVertices > 500 && NumVertices < 4000 && primCount > 300) {
                    isPlayer = true;
                }

                if (isPlayer) {
                    static DWORD lastColVis = 0, lastColInv = 0;
                    DWORD currColVis = D3DCOLOR_ARGB(255, (int)(col_chams_vis[0] * 255), (int)(col_chams_vis[1] * 255), (int)(col_chams_vis[2] * 255));
                    DWORD currColInv = D3DCOLOR_ARGB(255, (int)(col_chams_inv[0] * 255), (int)(col_chams_inv[1] * 255), (int)(col_chams_inv[2] * 255));

                    if (!tex_vis || lastColVis != currColVis) {
                        if (tex_vis) tex_vis->Release();
                        GenerateTexture(pDevice, &tex_vis, currColVis);
                        lastColVis = currColVis;
                    }
                    if (!tex_inv || lastColInv != currColInv) {
                        if (tex_inv) tex_inv->Release();
                        GenerateTexture(pDevice, &tex_inv, currColInv);
                        lastColInv = currColInv;
                    }

                    DWORD oldZEnable, oldZFunc, oldFillMode, oldAlphaBlend, oldAlphaTest, oldSrcBlend, oldDestBlend;
                    DWORD oldLighting, oldFog, oldCull, oldZWrite, oldClip;

                    pDevice->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
                    pDevice->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
                    pDevice->GetRenderState(D3DRS_FILLMODE, &oldFillMode);
                    pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
                    pDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
                    pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
                    pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
                    pDevice->GetRenderState(D3DRS_LIGHTING, &oldLighting);
                    pDevice->GetRenderState(D3DRS_FOGENABLE, &oldFog);
                    pDevice->GetRenderState(D3DRS_CULLMODE, &oldCull);
                    pDevice->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
                    pDevice->GetRenderState(D3DRS_CLIPPING, &oldClip);
                    pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
                    pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
                    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                    pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                    pDevice->SetRenderState(D3DRS_CLIPPING, FALSE);

                    if (chams_wireframe) {
                        pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
                        pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                        pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);

                        pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                        pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                        pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

                        pDevice->SetTexture(0, tex_vis);
                        oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
                    }
                    else {
                        pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                        pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
                        pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

                        pDevice->SetTexture(0, tex_inv);
                        oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);

                        pDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
                        pDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                        pDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

                        pDevice->SetTexture(0, tex_vis);
                        oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
                    }

                    pDevice->SetRenderState(D3DRS_ZENABLE, oldZEnable);
                    pDevice->SetRenderState(D3DRS_ZFUNC, oldZFunc);
                    pDevice->SetRenderState(D3DRS_FILLMODE, oldFillMode);
                    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
                    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
                    pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
                    pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
                    pDevice->SetRenderState(D3DRS_LIGHTING, oldLighting);
                    pDevice->SetRenderState(D3DRS_FOGENABLE, oldFog);
                    pDevice->SetRenderState(D3DRS_CULLMODE, oldCull);
                    pDevice->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
                    pDevice->SetRenderState(D3DRS_CLIPPING, oldClip);

                    return D3D_OK;
                }
            }
        }
    }

    return oDrawIndexedPrimitive(pDevice, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
}

BOOL WINAPI hkSetCursorPos(int X, int Y) {
    if (show_menu || tp_pie_open) return FALSE;
    return oSetCursorPos(X, Y);
}

void RenderWatermark() {
    ImGuiIO& io = ImGui::GetIO();
    float framerate = io.Framerate;

    time_t rawtime;
    struct tm timeinfo;
    char time_buffer[16];
    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    sprintf_s(time_buffer, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    char fps_str[32];
    sprintf_s(fps_str, g_Username, 1000.0f / (framerate + 0.01f));
    const char* text_link = "Telegram: @meltedhack";

    ImFont* font = ImGui::GetFont();
    const float font_size = 14.0f;
    float rounding = 6.0f;

    ImU32 col_bg = IM_COL32(12, 12, 12, 255);
    ImU32 col_border = IM_COL32(40, 40, 40, 255);
    ImU32 col_accent = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
    ImU32 col_text_white = IM_COL32(230, 230, 230, 255);
    ImU32 col_sep = IM_COL32(60, 60, 60, 255);

    float bar_width = 3.0f;
    float side_padding = 12.0f;
    float item_spacing = 8.0f;
    float icon_size = 14.0f;
    float height = 28.0f;

    float total_width = side_padding;

    total_width += icon_size;

    auto AddWidth = [&](float w) {
        total_width += item_spacing;
        total_width += 1.0f;
        total_width += item_spacing;
        total_width += w;
        };

    float w_link = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text_link).x;
    float w_time = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, time_buffer).x;
    float w_fps = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, fps_str).x;

    if (wm_show_link) AddWidth(w_link);
    if (wm_show_time) AddWidth(w_time);
    if (wm_show_fps)  AddWidth(w_fps);

    total_width += side_padding;

    ImGui::SetNextWindowSize(ImVec2(total_width, height));

    if (wm_first_launch) {
        wm_pos = ImVec2(50, 50);
        wm_first_launch = false;
    }
    ImGui::SetNextWindowPos(wm_pos, ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!show_menu) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##ElysiumWatermark", nullptr, flags);

    if (show_menu) {
        ImVec2 current_pos = ImGui::GetWindowPos();
        if (current_pos.x != wm_pos.x || current_pos.y != wm_pos.y) wm_pos = current_pos;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float center_y = p.y + (height / 2.0f);

    dl->AddRectFilled(p, ImVec2(p.x + total_width, p.y + height), col_bg, rounding);
    dl->AddRect(p, ImVec2(p.x + total_width, p.y + height), col_border, rounding);

    float bar_h_offset = 5.0f;
    dl->AddLine(ImVec2(p.x + 4, p.y + bar_h_offset), ImVec2(p.x + 4, p.y + height - bar_h_offset), col_accent, bar_width);

    dl->AddLine(ImVec2(p.x + total_width - 4, p.y + bar_h_offset), ImVec2(p.x + total_width - 4, p.y + height - bar_h_offset), col_accent, bar_width);

    float cur_x = p.x + side_padding;

    {
        dl->AddCircle(ImVec2(cur_x + icon_size / 2, center_y), icon_size / 2, col_accent, 0, 1.5f);
        dl->AddLine(ImVec2(cur_x + 3, center_y), ImVec2(cur_x + icon_size - 3, center_y), col_accent, 1.5f);
        cur_x += icon_size;
    }

    auto DrawItem = [&](const char* text, ImU32 color, float w) {
        dl->AddLine(ImVec2(cur_x + item_spacing, p.y + 6), ImVec2(cur_x + item_spacing, p.y + height - 6), col_sep);
        cur_x += item_spacing * 2 + 1.0f;

        ImVec2 txt_sz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
        dl->AddText(NULL, font_size, ImVec2(cur_x, center_y - txt_sz.y / 2.0f), color, text);
        cur_x += w;
        };

    if (wm_show_link) DrawItem(text_link, col_text_white, w_link);
    if (wm_show_time) DrawItem(time_buffer, col_accent, w_time);
    if (wm_show_fps)  DrawItem(fps_str, col_text_white, w_fps);

    if (show_menu) {
        ImGui::SetCursorScreenPos(p);

        ImGui::InvisibleButton("##wm_hitbox", ImVec2(total_width, height));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##WM_Settings_Popup");
        }

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

        if (ImGui::BeginPopup("##WM_Settings_Popup")) {
            ImGui::TextColored(ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f), "Watermark Settings");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            CustomToggle("Show Name", &wm_show_link);
            CustomToggle("Show Time", &wm_show_time);
            CustomToggle("Show Info", &wm_show_fps);

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void UpdateKeybinds() {
    if (show_menu) return;

    static std::map<Keybind*, bool> wasPressed;

    for (auto& bind : g_Keybinds) {
        if (bind->key == 0) continue;

        bool keyPressed = (GetAsyncKeyState(bind->key) & 0x8000);

        if (bind->mode == BIND_HOLD) {
            bool oldVal = *bind->value;
            *bind->value = keyPressed;

            if (*bind->value != oldVal && bind->callback) {
                bind->callback(*bind->value);
            }
        }
        else if (bind->mode == BIND_TOGGLE) {
            if (keyPressed && !wasPressed[bind]) {
                *bind->value = !(*bind->value);

                if (bind->callback) {
                    bind->callback(*bind->value);
                }
            }
            wasPressed[bind] = keyPressed;
        }
    }
}

void RenderKeybindList() {
    ImGuiIO& io = ImGui::GetIO();

    bool any_active = false;
    for (auto& bind : g_Keybinds) {
        if (*bind->value && bind->key != 0) {
            any_active = true;
            break;
        }
    }

    if (!show_menu && !any_active) return;

    static float alpha = 0.0f;
    float target_alpha = (show_menu || any_active) ? 1.0f : 0.0f;
    alpha = Lerp(alpha, target_alpha, io.DeltaTime * 10.0f);
    if (alpha < 0.01f) return;

    ImGui::SetNextWindowSize(ImVec2(210, 0));

    ImGui::SetNextWindowPos(ImVec2(50, io.DisplaySize.y / 2 - 100), ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!show_menu) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##KeybindList", nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 s = ImGui::GetWindowSize();

    dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(15, 15, 15, 240), 6.0f);

    dl->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(40, 40, 40, 255), 6.0f);

    ImU32 accent = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);

    float header_h = 30.0f;

    float icon_x = p.x + 12.0f;
    float icon_y = p.y + 11.0f;

    dl->AddLine(ImVec2(icon_x, icon_y), ImVec2(icon_x + 12, icon_y), accent, 2.0f);
    dl->AddLine(ImVec2(icon_x, icon_y + 5), ImVec2(icon_x + 8, icon_y + 5), accent, 2.0f);
    dl->AddLine(ImVec2(icon_x, icon_y + 10), ImVec2(icon_x + 12, icon_y + 10), accent, 2.0f);

    ImGui::SetCursorPos(ImVec2(32, 7));
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "Hotkeys");
    ImGui::SetCursorPosY(header_h);

    for (auto& bind : g_Keybinds) {
        bool should_show = (bind->key != 0) && (show_menu || *bind->value);

        float target_row = should_show ? 1.0f : 0.0f;
        bind->anim_val = Lerp(bind->anim_val, target_row, io.DeltaTime * 15.0f);

        if (bind->anim_val > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, bind->anim_val * alpha);

            ImGui::SetCursorPosX(12.0f);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", bind->name);

            ImGui::SameLine();

            const char* modeStr = (bind->mode == BIND_HOLD) ? "Hold" : "Toggle";

            char rightText[64];
            sprintf_s(rightText, "[%s]", modeStr);

            float w = ImGui::GetContentRegionAvail().x;
            float txtSz = ImGui::CalcTextSize(rightText).x;

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - txtSz - 12.0f);

            ImU32 rightCol = (*bind->value) ? accent : IM_COL32(100, 100, 100, 255);

            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(rightCol), rightText);

            ImGui::PopStyleVar();
        }
    }

    ImGui::Spacing();

    ImGui::End();
    ImGui::PopStyleVar();
}

ImVec2 RotatePoint(ImVec2 center, ImVec2 point, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    point.x -= center.x;
    point.y -= center.y;
    float xnew = point.x * c - point.y * s;
    float ynew = point.x * s + point.y * c;
    return ImVec2(xnew + center.x, ynew + center.y);
}

void AddTriangleShadow(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 col, float thickness) {
    for (int i = 0; i < 5; i++) {
        float t = thickness * ((i + 1) / 5.0f);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
        c.w *= (1.0f - (i / 5.0f)) * 0.4f;

        dl->AddTriangle(p1, p2, p3, ImGui::ColorConvertFloat4ToU32(c), t * 3.0f);
    }
}

void DrawPlayerArrows(ImDrawList* drawList, ImVec2 centerScreen) {
    if (!enable_arrows) return;

    if (RefNetGame() && RefNetGame()->GetPlayerPool()) {
        auto poolApi = RefNetGame()->GetPlayerPool();

        for (int i = 0; i < 1004; i++) {
            if (!poolApi->IsConnected(i) || i == GetLocalPlayerIdR3()) continue;

            CRemotePlayer* remotePlayer = poolApi->GetPlayer(i);
            if (!remotePlayer || !remotePlayer->m_pPed || remotePlayer->m_pPed->IsDead()) continue;

            void* pGamePed = remotePlayer->m_pPed->m_pGamePed;
            if (!pGamePed) continue;

            CMatrix matrix;
            remotePlayer->m_pPed->GetMatrix(&matrix);
            CVector targetPos = matrix.pos;

            ImVec2 screenPos;
            bool onScreen = GetScreenPosForArrow(targetPos, screenPos, centerScreen);

            float dx = screenPos.x - centerScreen.x;
            float dy = screenPos.y - centerScreen.y;

            if (abs(dx) < 1.0f && abs(dy) < 1.0f) continue;

            float angle = atan2(dy, dx);
            float c = cos(angle);
            float s = sin(angle);

            ImVec2 arrowPos = ImVec2(
                centerScreen.x + c * arrows_radius,
                centerScreen.y + s * arrows_radius
            );

            float scale = arrows_size;
            float width = scale * 0.75f;
            float indent = -scale * 0.3f;
            float back = -scale * 0.6f;

            ImVec2 pTip(scale, 0);
            ImVec2 pTop(back, -width);
            ImVec2 pIndent(indent, 0);
            ImVec2 pBot(back, width);

            auto Transform = [&](ImVec2 p) -> ImVec2 {
                return ImVec2(
                    arrowPos.x + (p.x * c - p.y * s),
                    arrowPos.y + (p.x * s + p.y * c)
                );
                };

            ImVec2 v1 = Transform(pTip);
            ImVec2 v2 = Transform(pTop);
            ImVec2 v3 = Transform(pIndent);
            ImVec2 v4 = Transform(pBot);
            ImColor baseColor = ImColor(col_arrows[0], col_arrows[1], col_arrows[2], 1.0f);
            ImColor glowColor = baseColor;
            glowColor.Value.w = 0.4f;

            drawList->PathLineTo(v1);
            drawList->PathLineTo(v2);
            drawList->PathLineTo(v3);
            drawList->PathLineTo(v4);
            drawList->PathStroke(glowColor, ImDrawFlags_Closed, 5.0f);
            drawList->PathLineTo(v1);
            drawList->PathLineTo(v2);
            drawList->PathLineTo(v3);
            drawList->PathLineTo(v4);
            drawList->PathStroke(baseColor, ImDrawFlags_Closed, 2.0f);
        }
    }
}

void RenderBindWindow() {
    if (!show_menu) {
        show_bind_window = false;
        current_bind_edit = nullptr;
        return;
    }

    if (!show_bind_window || !current_bind_edit) return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 160));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.5f));

    if (ImGui::Begin("##BindWindow", &show_bind_window, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 s = ImGui::GetWindowSize();

        ImU32 accent = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
        ImU32 accent_trans = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 20);

        dl->AddRectFilled(p, ImVec2(p.x + s.x, p.y + 40), IM_COL32(20, 20, 25, 255), 8.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + 38), ImVec2(p.x + s.x, p.y + 40), accent, accent, accent_trans, accent_trans);

        ImGui::PushFont(font_logo ? font_logo : ImGui::GetFont());
        const char* title = current_bind_edit->name;
        ImVec2 title_size = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(p.x + (s.x - title_size.x) / 2, p.y + (40 - title_size.y) / 2), IM_COL32(255, 255, 255, 255), title);
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(s.x - 30, 10));
        if (ImGui::InvisibleButton("##close", ImVec2(20, 20))) show_bind_window = false;
        if (ImGui::IsItemHovered()) dl->AddText(ImVec2(p.x + s.x - 25, p.y + 10), IM_COL32(255, 100, 100, 255), "X");
        else dl->AddText(ImVec2(p.x + s.x - 25, p.y + 10), IM_COL32(150, 150, 150, 255), "X");

        ImGui::SetCursorPos(ImVec2(20, 60));
        ImGui::BeginGroup();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Key Assignment");
        ImGui::Spacing();

        const char* keyName = current_bind_edit->waiting ? "PRESS..." : (current_bind_edit->key == 0 ? "NONE" : keyNames[current_bind_edit->key]);

        if (current_bind_edit->waiting) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        if (ImGui::Button(std::string(keyName).append("##keybtn").c_str(), ImVec2(120, 30))) {
            current_bind_edit->waiting = !current_bind_edit->waiting;
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        if (current_bind_edit->waiting) {
            for (int i = 0; i < 254; i++) {
                if (GetAsyncKeyState(i) & 0x8000) {
                    if (i == VK_ESCAPE) {
                        current_bind_edit->key = 0;
                    }
                    else if (i != VK_LBUTTON && i != VK_RBUTTON) {
                        current_bind_edit->key = i;
                    }
                    current_bind_edit->waiting = false;
                    break;
                }
            }
        }

        ImGui::EndGroup();

        ImGui::SameLine(160);

        ImGui::BeginGroup();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Trigger Mode");
        ImGui::Spacing();

        bool is_hold = (current_bind_edit->mode == BIND_HOLD);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));

        if (ImGui::BeginCombo("##modesel", is_hold ? "Hold" : "Toggle", ImGuiComboFlags_NoArrowButton)) {
            if (ImGui::Selectable("Toggle##sel1", !is_hold)) current_bind_edit->mode = BIND_TOGGLE;
            if (ImGui::Selectable("Hold##sel2", is_hold)) current_bind_edit->mode = BIND_HOLD;
            ImGui::EndCombo();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextDisabled(is_hold ? "Hold key to use" : "Press to switch");

        ImGui::EndGroup();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 min = ImGui::GetWindowPos();
            ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool is_mouse_inside_window = (mouse.x >= min.x && mouse.y >= min.y && mouse.x <= max.x && mouse.y <= max.y);

            bool is_any_popup_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

            bool is_item_hovered = ImGui::IsAnyItemHovered();

            if (!is_mouse_inside_window && !is_any_popup_open && !is_item_hovered) {
                show_bind_window = false;
                if (current_bind_edit) current_bind_edit->waiting = false;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void DrawUserProfile(float x, float y, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImVec2(x, y);
    float height = 50.0f;
    float avatar_radius = 16.0f;
    float padding = 10.0f;

    ImVec2 center = ImVec2(p.x + padding + avatar_radius + 5, p.y + height / 2);

    dl->AddCircleFilled(center, avatar_radius, IM_COL32(40, 40, 40, 255));

    char letter[2] = { (char)toupper(g_Username[0]), '\0' };

    ImFont* font = font_logo ? font_logo : ImGui::GetFont();
    ImGui::PushFont(font);

    ImVec2 txtSz = ImGui::CalcTextSize(letter);
    dl->AddText(ImVec2(center.x - txtSz.x / 2, center.y - txtSz.y / 2 - 1), IM_COL32(255, 255, 255, 255), letter);

    ImGui::PopFont();

    float gap = 1.0f;
    ImU32 accent = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
    dl->AddCircle(center, avatar_radius + gap, accent, 0, 1.5f);

    float text_x = center.x + avatar_radius + gap + 10.0f;
    float text_y_start = center.y - 14.0f;

    dl->AddText(ImVec2(text_x, text_y_start), IM_COL32(255, 255, 255, 255), g_Username);

    ImGui::PushFont(ImGui::GetFont());
    float old_scale = ImGui::GetFont()->Scale;
    ImGui::GetFont()->Scale = 0.85f;
    ImGui::PushFont(ImGui::GetFont());

    dl->AddText(ImVec2(text_x, text_y_start + 16.0f), IM_COL32(150, 150, 150, 255), g_Expiry);

    ImGui::GetFont()->Scale = old_scale;
    ImGui::PopFont();
    ImGui::PopFont();
}

void RunAutoPeek() {
    if (!enable_auto_peek) {
        is_peeking = false;
        return;
    }

    int key = bind_auto_peek.key;
    if (key == 0) return;

    bool isKeyPressed = (GetAsyncKeyState(key) & 0x8000);

    DWORD* pPed = (DWORD*)0xB6F5F0;
    if (!pPed || !*pPed) return;

    DWORD pVehicle = *(DWORD*)0xBA18FC;
    DWORD* pEntity = pVehicle ? (DWORD*)pVehicle : pPed;
    DWORD* pMatrix = (DWORD*)((*pEntity) + 0x14);
    if (!pMatrix || !*pMatrix) return;

    if (isKeyPressed && !is_peeking) {
        peek_start_pos.x = *(float*)((*pMatrix) + 0x30);
        peek_start_pos.y = *(float*)((*pMatrix) + 0x34);
        peek_start_pos.z = *(float*)((*pMatrix) + 0x38);
        is_peeking = true;
    }
    else if (!isKeyPressed && is_peeking) {
        *(float*)((*pMatrix) + 0x30) = peek_start_pos.x;
        *(float*)((*pMatrix) + 0x34) = peek_start_pos.y;
        *(float*)((*pMatrix) + 0x38) = peek_start_pos.z;

        if (pVehicle) {
            float* speed = (float*)(pVehicle + 68);
            speed[0] = 0.0f; speed[1] = 0.0f; speed[2] = 0.0f;
        }
        else {
            float* speed = (float*)((*pPed) + 0x44);
            speed[0] = 0.0f; speed[1] = 0.0f; speed[2] = 0.0f;
        }

        is_peeking = false;
    }

    if (is_peeking) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImU32 color = ImColor(col_auto_peek[0], col_auto_peek[1], col_auto_peek[2]);

        float groundZ = peek_start_pos.z - 1.0f;
        float radius = 0.6f;
        int segments = 32;

        for (int i = 0; i < segments; i++) {
            float angle1 = (i * (2 * 3.14159f)) / segments;
            float angle2 = ((i + 1) * (2 * 3.14159f)) / segments;

            float x1 = peek_start_pos.x + cos(angle1) * radius;
            float y1 = peek_start_pos.y + sin(angle1) * radius;

            float x2 = peek_start_pos.x + cos(angle2) * radius;
            float y2 = peek_start_pos.y + sin(angle2) * radius;

            Vector2 s1 = CalcScreenCoords(x1, y1, groundZ);
            Vector2 s2 = CalcScreenCoords(x2, y2, groundZ);

            if (s1.x != -1 && s2.x != -1) {
                dl->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), color, 2.0f);
            }
        }

        Vector2 sBase = CalcScreenCoords(peek_start_pos.x, peek_start_pos.y, groundZ);
        Vector2 sTop = CalcScreenCoords(peek_start_pos.x, peek_start_pos.y, groundZ + 1.0f);
        if (sBase.x != -1 && sTop.x != -1) {
            dl->AddLine(ImVec2(sBase.x, sBase.y), ImVec2(sTop.x, sTop.y), color, 1.0f);
        }
    }
}

void RenderMovementTrail(ImDrawList* dl) {
    if (!enable_trail || !RefNetGame() || !RefNetGame()->GetPlayerPool()) {
        trail_history.clear();
        return;
    }

    auto localPlayer = RefNetGame()->GetPlayerPool()->GetLocalPlayer();
    if (!localPlayer || !localPlayer->m_pPed) return;

    CMatrix matrix;
    localPlayer->m_pPed->GetMatrix(&matrix);
    CVector currentPos = matrix.pos;
    currentPos.z -= 1.0f;

    if (trail_history.empty() || GetDistance3D(trail_history.back(), currentPos) > 0.1f) {
        trail_history.push_back(currentPos);
    }

    while (trail_history.size() > (size_t)trail_max_points) {
        trail_history.erase(trail_history.begin());
    }

    if (trail_history.size() < 2) return;

    for (size_t i = 0; i < trail_history.size() - 1; ++i) {
        Vector2 p1 = CalcScreenCoords(trail_history[i].x, trail_history[i].y, trail_history[i].z);
        Vector2 p2 = CalcScreenCoords(trail_history[i + 1].x, trail_history[i + 1].y, trail_history[i + 1].z);

        if (p1.x != -1 && p2.x != -1) {
            float alpha = (float)i / trail_history.size();

            ImU32 color = ImColor(col_trail[0], col_trail[1], col_trail[2], alpha);

            dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), ImColor(col_trail[0], col_trail[1], col_trail[2], alpha * 0.3f), 4.0f);

            dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), color, 1.5f);
        }
    }
}

void RunAnimBreakerListener() {
    if (!enable_anim_breaker || bind_anim_breaker.key == 0) return;

    static bool was_pressed = false;
    bool is_pressed = (GetAsyncKeyState(bind_anim_breaker.key) & 0x8000);

    if (is_pressed && !was_pressed) {
        cPed* pLocal = cSA::getPlayerPed();
        if (pLocal) {
            cSA::disembarkPed(pLocal);
        }
        was_pressed = true;
    }
    else if (!is_pressed) {
        was_pressed = false;
    }
}

void DrawMinecraftChest(ImDrawList* dl, ImVec2 center, float size, int type) {
    float half = size / 2.0f;
    ImVec2 min = ImVec2(center.x - half, center.y - half);
    ImVec2 max = ImVec2(center.x + half, center.y + half);

    ImU32 colMain, colOutline, colLatch;

    switch (type) {
    case 1:
        colMain = IM_COL32(25, 25, 25, 255);
        colOutline = IM_COL32(140, 0, 255, 255);
        colLatch = IM_COL32(0, 255, 150, 255);
        break;
    case 2:
        colMain = IM_COL32(0, 50, 150, 255);
        colOutline = IM_COL32(0, 0, 0, 255);
        colLatch = IM_COL32(200, 200, 200, 255);
        break;
    case 3:
        colMain = IM_COL32(20, 120, 20, 255);
        colOutline = IM_COL32(0, 0, 0, 255);
        colLatch = IM_COL32(200, 200, 200, 255);
        break;
    case 4:
        colMain = IM_COL32(255, 215, 0, 255);
        colOutline = IM_COL32(100, 80, 0, 255);
        colLatch = IM_COL32(255, 255, 255, 255);
        break;
    case 5:
        colMain = IM_COL32(100, 100, 100, 255);
        colOutline = IM_COL32(0, 0, 0, 255);
        colLatch = IM_COL32(255, 50, 50, 255);
        break;
    default:
        colMain = IM_COL32(160, 82, 45, 255);
        colOutline = IM_COL32(60, 30, 0, 255);
        colLatch = IM_COL32(192, 192, 192, 255);
        break;
    }

    dl->AddRectFilled(min, max, colMain);

    dl->AddRect(min, max, colOutline, 0.0f, 0, 2.0f);

    float lidY = min.y + (size * 0.35f);
    dl->AddLine(ImVec2(min.x, lidY), ImVec2(max.x, lidY), colOutline, 2.0f);

    float lockSize = size * 0.2f;
    ImVec2 lockMin = ImVec2(center.x - lockSize / 2, lidY - 2);
    ImVec2 lockMax = ImVec2(center.x + lockSize / 2, lidY + lockSize + 2);

    dl->AddRectFilled(lockMin, lockMax, colLatch);
    dl->AddRect(lockMin, lockMax, colOutline, 0.0f, 0, 1.0f);

    if (type == 1) {
        dl->AddRectFilled(ImVec2(min.x + 4, min.y + 4), ImVec2(min.x + 7, min.y + 7), IM_COL32(200, 0, 255, 200));
        dl->AddRectFilled(ImVec2(max.x - 7, max.y - 7), ImVec2(max.x - 4, max.y - 4), IM_COL32(200, 0, 255, 200));
    }
}

void RenderBulletTracers(ImDrawList* dl) {
    if (!enable_tracers || g_BulletTracers.empty()) return;

    float currentTime = ImGui::GetTime();
    for (size_t i = 0; i < g_BulletTracers.size(); ) {
        BulletTraceInfo& trace = g_BulletTracers[i];

        float lifeTime = currentTime - trace.timeCreated;

        if (lifeTime > tracer_duration) {
            g_BulletTracers.erase(g_BulletTracers.begin() + i);
            continue;
        }
        else {
            i++;
        }

        Vector2 sStart = CalcScreenCoords(trace.start.x, trace.start.y, trace.start.z);
        Vector2 sEnd = CalcScreenCoords(trace.end.x, trace.end.y, trace.end.z);

        if (sStart.x != -1 && sEnd.x != -1) {
            float alpha = 1.0f - (lifeTime / tracer_duration);
            if (alpha < 0.0f) alpha = 0.0f;

            ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(col_tracer[0], col_tracer[1], col_tracer[2], alpha));

            dl->AddLine(ImVec2(sStart.x, sStart.y), ImVec2(sEnd.x, sEnd.y), color, 2.0f);

            dl->AddCircleFilled(ImVec2(sEnd.x, sEnd.y), 3.0f, color);
        }
    }
}

const char* GetSkinNameByID(int id) {
    switch (id) {
    case 0: return "CJ (Carl Johnson)";
    case 1: return "The Truth";
    case 2: return "Maccer";
    case 294: return "Woozie (Wuzimu)";
    case 265: return "Tenpenny (LSPD)";
    case 266: return "Pulaski (LSPD)";
    case 267: return "Hernandez (LSPD)";
    case 270: return "Sweet";
    case 271: return "Ryder";
    case 269: return "Big Smoke";
    case 23: return "BMXer";
    case 46: return "Tribal";
    case 280: return "LSPD Cop";
    case 281: return "SFPD Cop";
    case 282: return "LVPD Cop";
    case 283: return "Army";
    case 285: return "SWAT";
    case 287: return "Army (Desert)";
    case 217: return "Staff (Kenshin)";
    case 292: return "Cesar";
    case 293: return "OG Loc";
    case 297: return "Madd Dogg";
    case 299: return "Claude (GTA3)";
    default:
        if (id < 0 || id > 311) return "Invalid ID";
        if ((id >= 105 && id <= 110) || (id >= 114 && id <= 116)) return "Gang Member (Groove)";
        if (id >= 102 && id <= 104) return "Gang Member (Ballas)";
        if (id >= 108 && id <= 110) return "Gang Member (Vagos)";
        if (id >= 111 && id <= 113) return "Gang Member (Rifa)";
        if (id >= 117 && id <= 119) return "Triad Member";
        if (id >= 120 && id <= 123) return "Mafia Member";
        if (id >= 70 && id <= 80) return "Civilian (Male)";
        if (id >= 190 && id <= 199) return "Civilian (Female)";
        return "Civilian / Other";
    }
}

void RunFovSeatTeleport() {
    if (!enable_seat_tp || seat_tp_key == 0) return;

    static bool was_pressed = false;
    bool is_pressed = (GetAsyncKeyState(seat_tp_key) & 0x8000);

    if (is_pressed && !was_pressed) {
        was_pressed = true;

        if (!RefNetGame() || !RefNetGame()->GetVehiclePool()) return;

        auto vehPool = RefNetGame()->GetVehiclePool();
        ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2);

        int bestVehID = -1;
        float closestScreenDist = seat_tp_fov;

        for (int i = 0; i < 2000; i++) {
            auto pVeh = vehPool->Get(i);
            if (!pVeh || !pVeh->m_pGameVehicle) continue;

            DWORD pGameVeh = (DWORD)pVeh->m_pGameVehicle;
            DWORD pMatrix = *(DWORD*)(pGameVeh + 0x14);
            if (!pMatrix) continue;

            float vx = *(float*)(pMatrix + 0x30);
            float vy = *(float*)(pMatrix + 0x34);
            float vz = *(float*)(pMatrix + 0x38);

            Vector2 screenPos = CalcScreenCoords(vx, vy, vz);
            if (screenPos.x == -1) continue;

            float dist = sqrt(pow(center.x - screenPos.x, 2) + pow(center.y - screenPos.y, 2));

            if (dist < closestScreenDist) {
                closestScreenDist = dist;
                bestVehID = i;
            }
        }

        if (bestVehID != -1) {
            auto pVeh = vehPool->Get(bestVehID);
            DWORD pGameVeh = (DWORD)pVeh->m_pGameVehicle;
            DWORD pMatrix = *(DWORD*)(pGameVeh + 0x14);

            float tx = *(float*)(pMatrix + 0x30);
            float ty = *(float*)(pMatrix + 0x34);
            float tz = *(float*)(pMatrix + 0x38);

            ForceLocalPosition(tx, ty, tz + 1.0f);

            stPassengerData passData;
            memset(&passData, 0, sizeof(stPassengerData));

            passData.sVehicleID = bestVehID;
            passData.byteSeatID = 1;

            DWORD* pPed = (DWORD*)0xB6F5F0;
            if (pPed && *pPed) {
                passData.byteHealth = *(uint8_t*)(*pPed + 0x540);
                passData.byteArmor = *(uint8_t*)(*pPed + 0x544);
                passData.byteCurrentWeapon = *(uint8_t*)(*pPed + 0x718);
            }
            else {
                passData.byteHealth = 100;
            }

            passData.fPosition[0] = tx;
            passData.fPosition[1] = ty;
            passData.fPosition[2] = tz;

            BitStream bs;
            bs.Write((uint8_t)ID_PASSENGER_SYNC);
            bs.Write((char*)&passData, sizeof(stPassengerData));

            if (oRakPeerSend) {
                DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
                void* pRakClient = *(void**)(*(DWORD*)(sampDll + 0x26E8DC) + 0x2C);
                oRakPeerSend(pRakClient, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
            }
        }
    }
    else if (!is_pressed) {
        was_pressed = false;
    }
}

long __stdcall hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    g_pd3dDevice = pDevice;
    if (!pDevice) return 0;

    if (!init) {
        window = FindWindowA("Grand Theft Auto San Andreas", NULL);
        if (window) {
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = NULL;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX9_Init(pDevice);

            ImFontConfig cfg; cfg.PixelSnapH = true;
            if (io.Fonts) {
                static const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();
                font_main = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &cfg, ranges);
                font_logo = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f, &cfg);
                static const ImWchar icons_ranges[] = { 0xE000, 0xF8FF, 0 };
                ImFontConfig icons_config;
                icons_config.MergeMode = false;
                icons_config.PixelSnapH = true;
                icons_config.FontDataOwnedByAtlas = false;
                font_icons = io.Fonts->AddFontFromMemoryTTF((void*)IconFontData, sizeof(IconFontData), 24.0f, &icons_config, icons_ranges);
            }
            SetupStyle();
            init = true;
        }
    }

    if (init) {
        static IDirect3DStateBlock9* pStateBlock = nullptr;
        if (pStateBlock == nullptr) {
            pDevice->CreateStateBlock(D3DSBT_ALL, &pStateBlock);
        }
        if (pStateBlock) pStateBlock->Capture();

        ImGui::GetIO().MouseDrawCursor = show_menu;

        DWORD colorwrite, srgbwrite;
        pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite);
        pDevice->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);
        pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
        pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, false);

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();


        static bool b_key_pressed = false;
        bool b_key_down = (GetAsyncKeyState(0x42) & 0x8000);

        /*if (b_key_down && !b_key_pressed) {
            if (tp_pie_open) {
                tp_pie_open = false;
                if (!show_menu) {
                    ToggleGameInput(false);
                    ImGui::GetIO().MouseDrawCursor = false;
                }
            }
            else {
                tp_pie_open = true;
                tp_pie_step = 0;
                tp_pie_category = -1;

                tp_pie_center = ImGui::GetIO().DisplaySize;
                tp_pie_center.x *= 0.5f;
                tp_pie_center.y *= 0.5f;

                ToggleGameInput(true);
                ImGui::GetIO().MouseDrawCursor = true;
            }
            b_key_pressed = true;
        }*/
        /*else if (!b_key_down) {
            b_key_pressed = false;
        }*/

        bool close_menu = false;

        if (tp_pie_open) {
            int vehID = GetAnyStreamedVehicleID();
            const char* statusText;
            ImU32 statusColor;

            if (vehID != -1) {
                statusText = "TELEPORT READY";
                statusColor = IM_COL32(50, 255, 50, 255);
            }
            else {
                statusText = "NO VEHICLE DETECTED";
                statusColor = IM_COL32(255, 50, 50, 255);
            }

            ImGui::PushFont(font_logo ? font_logo : ImGui::GetFont());
            ImVec2 txtSz = ImGui::CalcTextSize(statusText);

            ImGui::GetBackgroundDrawList()->AddText(ImVec2(tp_pie_center.x - txtSz.x / 2, tp_pie_center.y + 130.0f), IM_COL32(0, 0, 0, 255), statusText);
            ImGui::GetBackgroundDrawList()->AddText(ImVec2(tp_pie_center.x - txtSz.x / 2 - 1, tp_pie_center.y + 129.0f), statusColor, statusText);

            ImGui::PopFont();

            if (tp_pie_step == 0) {
                ImGui::OpenPopup("##TeleportPieMain");

                const char* items_cat[] = { "Organizations", "Transport Hubs" };
                int selected_cat = -1;

                int res = PiePopupSelectMenu(tp_pie_center, "##TeleportPieMain", items_cat, 2, &selected_cat);

                if (res != -1) {
                    tp_pie_category = res;
                    tp_pie_step = 1;
                }
                else if (ImGui::IsMouseReleased(0)) {
                    float dist = GetDistance2D({ ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y }, { tp_pie_center.x, tp_pie_center.y });
                    if (dist > 30.0f) close_menu = true;
                }
            }
            else if (tp_pie_step == 1) {
                ImGui::OpenPopup("##TeleportPieSub");

                int selected_loc = -1;
                int res = -1;

                auto PerformTeleport = [&](float x, float y, float z) {
                    g_PieTpTarget = { x, y, z };
                    g_PieTpActive = true;

                    ForceLocalPosition(x, y, z);

                    close_menu = true;
                    };

                if (tp_pie_category == 0) {
                    std::vector<const char*> names;
                    for (auto& p : locs_orgs) names.push_back(p.name);
                    res = PiePopupSelectMenu(tp_pie_center, "##TeleportPieSub", names.data(), names.size(), &selected_loc);

                    if (res != -1) PerformTeleport(locs_orgs[res].x, locs_orgs[res].y, locs_orgs[res].z);
                }
                else if (tp_pie_category == 1) {
                    std::vector<const char*> names;
                    for (auto& p : locs_transport) names.push_back(p.name);
                    res = PiePopupSelectMenu(tp_pie_center, "##TeleportPieSub", names.data(), names.size(), &selected_loc);

                    if (res != -1) PerformTeleport(locs_transport[res].x, locs_transport[res].y, locs_transport[res].z);
                }

                if (res == -1 && ImGui::IsMouseReleased(0)) {
                    float dist = GetDistance2D({ ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y }, { tp_pie_center.x, tp_pie_center.y });
                    if (dist > 30.0f) close_menu = true;
                }
            }
        }

        if (tp_pie_open && ImGui::IsMouseClicked(1)) close_menu = true;

        if (close_menu) {
            tp_pie_open = false;
            if (!show_menu) {
                ToggleGameInput(false);
                ImGui::GetIO().MouseDrawCursor = false;
            }
        }

        UpdateDynamicColors();

        auto drawlist = ImGui::GetBackgroundDrawList();
        ImGuiIO& io = ImGui::GetIO();

        if (play_intro) {
            if (intro_start_time == 0.0) intro_start_time = ImGui::GetTime();
            double time = ImGui::GetTime() - intro_start_time;

            float bg_alpha = 0.0f;
            float text_alpha = 0.0f;
            float anim_offset_y = 0.0f;

            if (time < 1.0) {
                bg_alpha = (float)time;
            }
            else if (time < 4.0) {
                bg_alpha = 0.95f;
                if (time < 2.0) {
                    text_alpha = (float)(time - 1.0);
                    anim_offset_y = (1.0f - text_alpha) * 20.0f;
                }
                else {
                    text_alpha = 1.0f;
                }
            }
            else if (time < 5.5) {
                float fade_out = 1.0f - (float)((time - 4.0) / 1.5);
                bg_alpha = 0.95f * fade_out;
                text_alpha = fade_out;
            }
            else {
                play_intro = false;
                show_menu = true;

                ToggleGameInput(show_menu);
            }

            drawlist->AddRectFilled(ImVec2(0, 0), io.DisplaySize, ImColor(0.0f, 0.0f, 0.0f, bg_alpha));

            if (text_alpha > 0.01f) {
                if (font_logo) ImGui::PushFont(font_logo);

                const char* txt_title = "Telegram: @meltedhack";
                float old_scale = ImGui::GetFont()->Scale;
                ImGui::GetFont()->Scale = 2.0f;
                ImGui::PushFont(ImGui::GetFont());

                ImVec2 title_size = ImGui::CalcTextSize(txt_title);
                ImVec2 center = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);

                ImVec2 pos_title = ImVec2(center.x - title_size.x / 2, center.y - title_size.y - 10 + anim_offset_y);

                drawlist->AddText(ImVec2(pos_title.x + 2, pos_title.y + 2), ImColor(0.0f, 0.0f, 0.0f, text_alpha), txt_title);
                drawlist->AddText(pos_title, ImColor(0.0f, 1.0f, 0.0f, text_alpha), txt_title);

                ImGui::GetFont()->Scale = old_scale;
                ImGui::PopFont();

                const char* txt_sub = "sliv dcp hack";
                ImVec2 sub_size = ImGui::CalcTextSize(txt_sub);
                ImVec2 pos_sub = ImVec2(center.x - sub_size.x / 2, center.y + 10 + anim_offset_y);

                drawlist->AddText(ImVec2(pos_sub.x + 1, pos_sub.y + 1), ImColor(0.0f, 0.0f, 0.0f, text_alpha), txt_sub);
                drawlist->AddText(pos_sub, ImColor(1.0f, 1.0f, 1.0f, text_alpha), txt_sub);

                if (font_logo) ImGui::PopFont();
            }

            /*if (show_cef_console) {
                g_CefConsole.Draw("CEF Interceptor Console", &show_cef_console);
            }*/

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
            pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);
            return oEndScene(pDevice);
        }

        RenderWatermark();

        if (enable_tp_exploit) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 center = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);

            int vID = GetAnyStreamedVehicleID();
            char debugBuf[64];

            if (vID != -1) {
                sprintf_s(debugBuf, "TP EXPLOIT: READY (VEH %d)", vID);
                drawlist->AddText(ImVec2(center.x, center.y + 60), IM_COL32(0, 255, 0, 255), debugBuf);
            }
            else {
                sprintf_s(debugBuf, "TP EXPLOIT: NO VEHICLES FOUND!");
                drawlist->AddText(ImVec2(center.x, center.y + 60), IM_COL32(255, 0, 0, 255), debugBuf);
            }
        }

        if (enable_obj_surf) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 center = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);
            float yPos = center.y + 40.0f;

            char buf[128];
            ImU32 col;

            if (g_GhostIsAttached) {
                sprintf_s(buf, "INVISIBLE LOOTER: ACTIVE [VEH %d]", g_GhostAttachedID);
                col = IM_COL32(0, 255, 0, 255);
            }
            else {
                sprintf_s(buf, "INVISIBLE LOOTER: NO VEHICLE FOUND!");
                col = IM_COL32(255, 50, 50, 255);
            }

            drawlist->AddText(ImVec2(center.x - ImGui::CalcTextSize(buf).x / 2 + 1, yPos + 1), IM_COL32(0, 0, 0, 255), buf);
            drawlist->AddText(ImVec2(center.x - ImGui::CalcTextSize(buf).x / 2, yPos), col, buf);
        }

        RenderBulletTracers(drawlist);

        CVector debugPos;
        GetWaypointPos(debugPos);

        if (enable_rvanka && rvanka_draw_fov) {
            ImGuiIO& io = ImGui::GetIO();
            float cx = io.DisplaySize.x / 2.0f;
            float cy = io.DisplaySize.y / 2.0f;
            drawlist->AddCircle(ImVec2(cx, cy), rvanka_fov, ImColor(col_rvanka_fov[0], col_rvanka_fov[1], col_rvanka_fov[2]), 60, 2.0f);
        }

        RenderMovementTrail(drawlist);

        if (show_menu && enable_mouse_aim) {
            ImVec2 center = GetFOVCenter();
            auto dl = ImGui::GetBackgroundDrawList();
            dl->AddLine(ImVec2(center.x - 10, center.y), ImVec2(center.x + 10, center.y), IM_COL32(255, 0, 0, 255), 2.0f);
            dl->AddLine(ImVec2(center.x, center.y - 10), ImVec2(center.x, center.y + 10), IM_COL32(255, 0, 0, 255), 2.0f);
        }

        if (show_menu) {
            memset((void*)0xB73458, 0, 0x132);

            *(BYTE*)0xB73418 = 0;
            *(BYTE*)0xB73404 = 0;

            *(float*)0xB73424 = 0.0f;
            *(float*)0xB73428 = 0.0f;
        }

        if (enable_seat_tp && draw_seat_tp_fov) {
            ImGuiIO& io = ImGui::GetIO();
            float cx = io.DisplaySize.x / 2.0f;
            float cy = io.DisplaySize.y / 2.0f;
            drawlist->AddCircle(ImVec2(cx, cy), seat_tp_fov, ImColor(col_seat_tp_fov[0], col_seat_tp_fov[1], col_seat_tp_fov[2]), 60, 2.0f);
        }

        RunFovSeatTeleport();
        RunTeleportLogic();
        RunAirBreak();
        RunVoiceMagnet();
        RunMouseAimbot();
        RunAutoPeek();
        RunAnimBreakerListener();
        RunMiscFeatures();
        RunAutoHandbrake();

        UpdateKeybinds();
        RenderKeybindList();

        if (silent_enabled && draw_fov) {
            ImVec2 center = GetFOVCenter();
            drawlist->AddCircle(center, silent_fov, ImColor(col_aim_fov[0], col_aim_fov[1], col_aim_fov[2]), 64, 1.0f);
        }

        if (draw_mouse_fov) {
            ImVec2 center = GetFOVCenter();
            drawlist->AddCircle(center, mouse_fov, ImColor(col_mouse_fov[0], col_mouse_fov[1], col_mouse_fov[2]), 64, 1.5f);
        }

        if (enable_box || enable_lines || enable_name || enable_health || enable_armor || enable_weapon || enable_skeleton) {

            DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");

            if (sampDll) {
                DWORD* pSampInfo = (DWORD*)(sampDll + 0x26E8DC);

                if (pSampInfo && *pSampInfo) {
                    DWORD* pPools = (DWORD*)(*pSampInfo + 0x3DE);

                    if (pPools && *pPools) {
                        DWORD* pPlayerPool = (DWORD*)(*pPools + 0x8);

                        if (pPlayerPool && *pPlayerPool) {
                            DWORD* pRemotePlayerArray = (DWORD*)(*pPlayerPool + 0x4);

                            int* pIsListed = (int*)(*pPlayerPool + 0xFB4);

                            if (pRemotePlayerArray && pIsListed) {
                                auto poolApi = RefNetGame() ? RefNetGame()->GetPlayerPool() : nullptr;

                                ImU32 cBox = ImColor(col_esp_box[0], col_esp_box[1], col_esp_box[2]);
                                ImU32 cLine = ImColor(col_esp_lines[0], col_esp_lines[1], col_esp_lines[2]);
                                ImU32 cName = ImColor(col_esp_names[0], col_esp_names[1], col_esp_names[2]);
                                ImU32 cBlack = IM_COL32(0, 0, 0, 255);

                                for (int i = 0; i < 1004; i++) {
                                    if (pIsListed[i] == 0) continue;

                                    DWORD pRemotePlayer = pRemotePlayerArray[i];
                                    if (!pRemotePlayer) continue;

                                    float hp = 0.0f;
                                    float arm = 0.0f;
                                    DWORD pRemotePlayerData = *(DWORD*)(pRemotePlayer + 0x0);

                                    if (pRemotePlayerData) {
                                        try {
                                            float* ptrHp = (float*)(pRemotePlayerData + 0x1B0);
                                            float* ptrArm = (float*)(pRemotePlayerData + 0x1AC);
                                            if (ptrHp) hp = *ptrHp;
                                            if (ptrArm) arm = *ptrArm;
                                        }
                                        catch (...) { hp = 0.0f; arm = 0.0f; }
                                    }

                                    if (hp <= 0.0f || hp > 200.0f) continue;

                                    ImU32 curBoxColor = cBox;
                                    ImU32 curLineColor = cLine;
                                    ImU32 curNameColor = cName;
                                    ImU32 curSkelColor = ImColor(col_skeleton[0], col_skeleton[1], col_skeleton[2]);

                                    if (poolApi) {
                                        CRemotePlayer* pCheckPlayer = poolApi->GetPlayer(i);
                                        if (pCheckPlayer && pCheckPlayer->m_pPed && pCheckPlayer->m_pPed->m_pGamePed) {

                                            if (enable_esp_vis_check) {
                                                CVector chestPos;
                                                GetPedBonePosition(pCheckPlayer->m_pPed->m_pGamePed, &chestPos, 3);

                                                bool isVis = IsPosVisible(chestPos);

                                                ImU32 finalColor = isVis ?
                                                    ImColor(col_esp_vis[0], col_esp_vis[1], col_esp_vis[2]) :
                                                    ImColor(col_esp_occ[0], col_esp_occ[1], col_esp_occ[2]);

                                                curBoxColor = finalColor;
                                                curLineColor = finalColor;
                                                curNameColor = finalColor;
                                                curSkelColor = finalColor;
                                            }
                                        }
                                    }

                                    if (enable_event_mode && poolApi) {
                                        ImU32 colTeam = IM_COL32(0, 255, 0, 255);
                                        ImU32 colEnemy = IM_COL32(255, 0, 0, 255);

                                        const int SKIN_EVENT_1 = 2862;
                                        const int SKIN_EVENT_2 = 2863;

                                        int mySkin = -1;
                                        auto localPlayer = poolApi->GetLocalPlayer();
                                        if (localPlayer && localPlayer->m_pPed && localPlayer->m_pPed->m_pGamePed) {
                                            mySkin = ((CGamePed*)localPlayer->m_pPed->m_pGamePed)->m_nModelIndex;
                                        }

                                        int targetSkin = -2;
                                        auto remotePlayerApi = poolApi->GetPlayer(i);
                                        if (remotePlayerApi && remotePlayerApi->m_pPed && remotePlayerApi->m_pPed->m_pGamePed) {
                                            targetSkin = ((CGamePed*)remotePlayerApi->m_pPed->m_pGamePed)->m_nModelIndex;
                                        }

                                        DWORD targetColor = GetPlayerColorR3(i) & 0x00FFFFFF;

                                        bool isEnemy = false;

                                        if (targetColor == 0xFFFFFF) {
                                            isEnemy = true;
                                        }
                                        else if (targetSkin != -2 && mySkin != -1) {
                                            bool amISpecial = (mySkin == SKIN_EVENT_1 || mySkin == SKIN_EVENT_2);
                                            bool isTargetSpecial = (targetSkin == SKIN_EVENT_1 || targetSkin == SKIN_EVENT_2);

                                            if (amISpecial) {
                                                if (!isTargetSpecial) isEnemy = true;
                                            }
                                            else {
                                                if (isTargetSpecial) isEnemy = true;
                                            }
                                        }
                                        else {
                                            isEnemy = true;
                                        }

                                        if (isEnemy) {
                                            curBoxColor = curLineColor = curNameColor = curSkelColor = colEnemy;
                                        }
                                        else {
                                            curBoxColor = curLineColor = curNameColor = curSkelColor = colTeam;
                                        }
                                    }
                                    if (poolApi) {
                                        CRemotePlayer* playerApi = poolApi->GetPlayer(i);
                                        if (playerApi && playerApi->m_pPed && !playerApi->m_pPed->IsDead() && playerApi->m_pPed->m_pGamePed) {

                                            CMatrix matrix;
                                            playerApi->m_pPed->GetMatrix(&matrix);
                                            CVector pos = matrix.pos;

                                            Vector2 footScreen = CalcScreenCoords(pos.x, pos.y, pos.z - 1.1f);
                                            Vector2 headScreen = CalcScreenCoords(pos.x, pos.y, pos.z + 0.9f);

                                            if (footScreen.x != -1 && headScreen.x != -1) {
                                                float h = footScreen.y - headScreen.y;
                                                float w = h / 2.0f;
                                                float x = footScreen.x - (w / 2.0f);
                                                float y = headScreen.y;

                                                if (enable_lines) {
                                                    ImVec2 src;
                                                    if (style_lines == 0) src = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y);
                                                    else if (style_lines == 1) src = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);
                                                    else src = ImVec2(io.DisplaySize.x / 2, 0);

                                                    if (enable_lines) {
                                                        ImVec2 pStart = src;
                                                        ImVec2 pEnd = ImVec2(footScreen.x, footScreen.y);
                                                        if (enable_esp_glow) DrawGlowLine(drawlist, pStart, pEnd, curLineColor, h);
                                                        drawlist->AddLine(pStart, pEnd, curLineColor);
                                                    }
                                                }

                                                if (enable_box) {
                                                    if (style_box == 0) { // 2D Box
                                                        if (enable_esp_glow) {
                                                            DrawGlowLine(drawlist, ImVec2(x, y), ImVec2(x + w, y), curBoxColor, h);
                                                            DrawGlowLine(drawlist, ImVec2(x, y + h), ImVec2(x + w, y + h), curBoxColor, h);
                                                            DrawGlowLine(drawlist, ImVec2(x, y), ImVec2(x, y + h), curBoxColor, h);
                                                            DrawGlowLine(drawlist, ImVec2(x + w, y), ImVec2(x + w, y + h), curBoxColor, h);
                                                        }
                                                        drawlist->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1), cBlack);
                                                        drawlist->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), curBoxColor);
                                                    }
                                                    else if (style_box == 1) { // Corner Box
                                                        DrawCornerBox(drawlist, x, y, w, h, curBoxColor, cBlack);
                                                    }
                                                    else if (style_box == 2) { // Filled Box
                                                        float static_alpha = 0.4f;

                                                        ImU32 fillColTop, fillColBot;

                                                        if (enable_event_mode) {
                                                            ImVec4 teamCol = ImGui::ColorConvertU32ToFloat4(curBoxColor);
                                                            fillColTop = ImGui::ColorConvertFloat4ToU32(ImVec4(teamCol.x, teamCol.y, teamCol.z, static_alpha));
                                                            fillColBot = ImGui::ColorConvertFloat4ToU32(ImVec4(teamCol.x * 0.3f, teamCol.y * 0.3f, teamCol.z * 0.3f, static_alpha));
                                                        }
                                                        else {
                                                            fillColTop = ImGui::ColorConvertFloat4ToU32(ImVec4(col_esp_fill_top[0], col_esp_fill_top[1], col_esp_fill_top[2], static_alpha));
                                                            fillColBot = ImGui::ColorConvertFloat4ToU32(ImVec4(col_esp_fill_bot[0], col_esp_fill_bot[1], col_esp_fill_bot[2], static_alpha));
                                                        }

                                                        drawlist->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h), fillColTop, fillColTop, fillColBot, fillColBot);

                                                        drawlist->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), curBoxColor);
                                                    }
                                                }

                                                if (enable_skeleton) {
                                                    DrawSkeleton(drawlist, playerApi->m_pPed->m_pGamePed, curSkelColor, h);
                                                }

                                                if (enable_health) {
                                                    DrawHealthBar(drawlist, x, y, h, hp, style_health, enable_text_hp_arm, h);
                                                }

                                                float bottomOffset = 0.0f;
                                                if (enable_armor) {
                                                    if (arm > 0.0f) {
                                                        ImU32 cArmor = ImColor(col_esp_armor[0], col_esp_armor[1], col_esp_armor[2]);
                                                        DrawArmorBar(drawlist, x, y + h + bottomOffset, w, arm, cArmor, enable_text_hp_arm, h);
                                                        float dynOffset = 6.0f * (h / 150.0f);
                                                        if (dynOffset < 3.0f) dynOffset = 3.0f;
                                                        if (dynOffset > 6.0f) dynOffset = 6.0f;
                                                        bottomOffset += dynOffset;
                                                    }
                                                }

                                                if (enable_name) {
                                                    const char* name = poolApi->GetName(i);
                                                    if (name) {
                                                        char idBuf[16];
                                                        sprintf_s(idBuf, "%d", i);
                                                        ImVec2 nameSize = ImGui::CalcTextSize(name);
                                                        ImVec2 idSize = ImGui::CalcTextSize(idBuf);
                                                        float paddingX = 4.0f;
                                                        float spacing = 4.0f;
                                                        float badgeHeight = idSize.y + 2.0f;
                                                        float badgeWidth = idSize.x + (paddingX * 2.0f);
                                                        float totalWidth = nameSize.x;
                                                        if (enable_id_tags) totalWidth += spacing + badgeWidth;
                                                        float startX = headScreen.x - (totalWidth / 2.0f);
                                                        float startY = headScreen.y - nameSize.y - 4.0f;

                                                        drawlist->AddText(ImVec2(startX + 1, startY + 1), cBlack, name);
                                                        drawlist->AddText(ImVec2(startX, startY), curNameColor, name);

                                                        startX += nameSize.x + spacing;

                                                        if (enable_id_tags) {
                                                            float badgeY = startY + (nameSize.y / 2.0f) - (badgeHeight / 2.0f);
                                                            ImVec2 badgeMin = ImVec2(startX, badgeY);
                                                            ImVec2 badgeMax = ImVec2(startX + badgeWidth, badgeY + badgeHeight);
                                                            drawlist->AddRectFilled(badgeMin, badgeMax, IM_COL32(0, 115, 230, 255), 4.0f);
                                                            ImVec2 textPos = ImVec2(badgeMin.x + paddingX, badgeMin.y + 1.0f);
                                                            drawlist->AddText(textPos, IM_COL32(255, 255, 255, 255), idBuf);
                                                        }
                                                    }
                                                }

                                                if (enable_weapon) {
                                                    int wID = playerApi->m_pPed->GetCurrentWeapon();
                                                    const char* wName = GetWeaponName(wID);
                                                    if (wName && wName[0]) {
                                                        ImVec2 ts = ImGui::CalcTextSize(wName);
                                                        float dynOffsetWeapon = 2.0f;
                                                        float textY = footScreen.y + bottomOffset + dynOffsetWeapon;
                                                        drawlist->AddText(ImVec2(footScreen.x - ts.x / 2 + 1, textY + 1), cBlack, wName);
                                                        drawlist->AddText(ImVec2(footScreen.x - ts.x / 2, textY), IM_COL32(255, 255, 255, 255), wName);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (enable_arrows) {
            ImVec2 center = ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);
            DrawPlayerArrows(drawlist, center);
        }

        if (enable_miner_wh && RefNetGame()) {
            auto pool = RefNetGame()->GetObjectPool();
            if (pool) {
                for (int i = 0; i < 1000; i++) {
                    if (pool->m_pObject[i] == nullptr) continue;
                    auto obj = pool->Get(i);
                    if (!obj) continue;
                    int model = obj->m_nModel;
                    for (int b = 0; b < 4; b++) {
                        if (model == objectId[b]) {
                            Vector2 screen = CalcScreenCoords(obj->m_position.x, obj->m_position.y, obj->m_position.z);
                            if (screen.x != -1 && screen.y != -1) {
                                ImColor color = ImColor(col_miner[b][0], col_miner[b][1], col_miner[b][2]);
                                drawlist->AddText(ImVec2(screen.x, screen.y), color, objectName[b]);
                            }
                        }
                    }
                }
            }
        }

        if (enable_frozen_lands) {
            DWORD* pObjectPool = (DWORD*)0xB7449C;

            if (pObjectPool && *pObjectPool) {
                DWORD poolData = *(DWORD*)(*pObjectPool);
                DWORD poolFlags = *(DWORD*)(*pObjectPool + 4);
                int poolSize = *(int*)(*pObjectPool + 8);

                CVector localPos = { 0,0,0 };
                void* pLocalPed = (void*)*(DWORD*)0xB6F5F0;
                if (pLocalPed) {
                    DWORD pMat = *(DWORD*)((DWORD)pLocalPed + 0x14);
                    if (pMat) {
                        localPos.x = *(float*)(pMat + 0x30);
                        localPos.y = *(float*)(pMat + 0x34);
                        localPos.z = *(float*)(pMat + 0x38);
                    }
                }

                for (int i = 0; i < poolSize; i++) {
                    if (*(BYTE*)(poolFlags + i) & 0x80) continue;

                    DWORD pObject = poolData + (i * 412);
                    if (IsBadReadPtr((void*)pObject, 412)) continue;

                    unsigned short modelID = *(unsigned short*)(pObject + 0x22);

                    int chestType = -1;
                    const char* label = nullptr;
                    ImU32 textColor = IM_COL32(255, 255, 255, 255);

                    switch (modelID) {
                    case 12103:
                    case 12130:
                    case 9700:
                    case 12080:
                        chestType = 0;
                        label = "Chest";
                        break;

                    case 9022:
                        chestType = 1;
                        label = "BOSS LOOT";
                        break;

                    case 9702:
                        chestType = 2;
                        label = "Blue Chest";
                        break;

                    case 9701:
                        chestType = 3;
                        label = "Green Chest";
                        break;

                    case 12730:
                        chestType = 4;
                        label = "Star Chest";
                        break;

                    case 14989:
                        chestType = 5;
                        label = "Keycard Chest";
                        break;

                    case 1410:
                        chestType = 99;
                        label = "Backpack";
                        textColor = IM_COL32(255, 100, 100, 255);
                        break;
                    }

                    if (chestType == -1) continue;

                    DWORD pMatrix = *(DWORD*)(pObject + 0x14);
                    if (!pMatrix || IsBadReadPtr((void*)pMatrix, 64)) continue;

                    float px = *(float*)(pMatrix + 0x30);
                    float py = *(float*)(pMatrix + 0x34);
                    float pz = *(float*)(pMatrix + 0x38);

                    float dist = sqrt(pow(localPos.x - px, 2) + pow(localPos.y - py, 2) + pow(localPos.z - pz, 2));
                    if (dist > 100.0f) continue;

                    Vector2 screen = CalcScreenCoords(px, py, pz + 0.5f);

                    if (screen.x != -1 && screen.y != -1) {

                        if (chestType == 99) {
                            float size = 30.0f;
                            float half = size / 2;
                            drawlist->AddRect(ImVec2(screen.x - half, screen.y - size), ImVec2(screen.x + half, screen.y + half), textColor, 0.0f, 0, 2.0f);
                            drawlist->AddText(ImVec2(screen.x - 20, screen.y - size - 15), textColor, label);
                        }
                        else {
                            float iconSize = 40.0f - (dist * 0.2f);
                            if (iconSize < 15.0f) iconSize = 15.0f;

                            DrawMinecraftChest(drawlist, ImVec2(screen.x, screen.y), iconSize, chestType);

                            if (dist < 20.0f) {
                                ImVec2 txtSz = ImGui::CalcTextSize(label);
                                drawlist->AddText(ImVec2(screen.x - txtSz.x / 2, screen.y + iconSize / 2 + 2), IM_COL32(255, 255, 255, 200), label);
                            }
                        }
                    }
                }
            }
        }

        if (show_menu) {
            ImGuiIO& io = ImGui::GetIO();
            memset((void*)0xB73458, 0, 0x132);
            *(BYTE*)0xB73418 = 0;
            *(BYTE*)0xB73404 = 0;
            *(float*)0xB73424 = 0.0f;
            *(float*)0xB73428 = 0.0f;

            ImGui::SetNextWindowSize(ImVec2(720, 480));

            if (ImGui::Begin("PurpleIcons", &show_menu, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
            {
                ImVec2 p = ImGui::GetWindowPos();
                ImVec2 s = ImGui::GetWindowSize();
                ImDrawList* draw = ImGui::GetWindowDrawList();

                float sidebar_width = 85.0f;
                draw->AddRectFilled(p, ImVec2(p.x + sidebar_width, p.y + s.y), ImColor(37, 37, 41, 255), 12.0f, ImDrawFlags_RoundCornersLeft);

                draw->AddRectFilled(ImVec2(p.x + sidebar_width, p.y), ImVec2(p.x + s.x, p.y + s.y), ImColor(18, 18, 20, 255), 12.0f, ImDrawFlags_RoundCornersRight);

                draw->AddLine(ImVec2(p.x + sidebar_width, p.y), ImVec2(p.x + sidebar_width, p.y + s.y), ImColor(50, 50, 55, 255));

                float icon_size = 45.0f;
                float padding_x = (sidebar_width - icon_size) * 0.5f;

                ImGui::SetCursorPos(ImVec2(padding_x, 35));
                ImGui::BeginGroup();

                const char* ICON_TARGET = (const char*)u8"\uE906";
                const char* ICON_VISUAL = (const char*)u8"\uE900";
                const char* ICON_MISC = (const char*)u8"\uE903";
                const char* ICON_CFG = (const char*)u8"\uE901";
                const char* ICON_SKIN = (const char*)u8"\uE904";

                ImGui::PushFont(font_icons);
                float icon_spacing = 15.0f;

                ImGui::PushID(0);
                if (IconButton(ICON_TARGET, active_tab == 0, ImVec2(icon_size, icon_size))) active_tab = 0;
                ImGui::PopID(); ImGui::Dummy(ImVec2(0, icon_spacing));

                ImGui::PushID(1);
                if (IconButton(ICON_VISUAL, active_tab == 1, ImVec2(icon_size, icon_size))) active_tab = 1;
                ImGui::PopID(); ImGui::Dummy(ImVec2(0, icon_spacing));

                ImGui::PushID(2);
                if (IconButton(ICON_MISC, active_tab == 2, ImVec2(icon_size, icon_size))) active_tab = 2;
                ImGui::PopID(); ImGui::Dummy(ImVec2(0, icon_spacing));

                ImGui::PushID(4);
                if (IconButton(ICON_SKIN, active_tab == 4, ImVec2(icon_size, icon_size))) active_tab = 4;
                ImGui::PopID(); ImGui::Dummy(ImVec2(0, icon_spacing));

                ImGui::PushID(3);
                if (IconButton(ICON_CFG, active_tab == 3, ImVec2(icon_size, icon_size))) active_tab = 3;
                ImGui::PopID();

                ImGui::PopFont();
                ImGui::EndGroup();

                const char* tab_title = "";
                if (active_tab == 0) tab_title = "AIMBOT";
                else if (active_tab == 1) tab_title = "VISUALS";
                else if (active_tab == 2) tab_title = "MISC";
                else if (active_tab == 3) tab_title = "SETTINGS";
                else if (active_tab == 4) tab_title = "SKINCHANGER";

                ImGui::SetCursorPos(ImVec2(sidebar_width + 30, 30));
                ImGui::PushFont(font_logo);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), tab_title);
                ImGui::PopFont();

                ImGui::SetCursorPos(ImVec2(sidebar_width + 30, 75));

                float content_w = s.x - sidebar_width - 60;
                float content_h = s.y - 100;
                float col_w = (content_w - 20) / 2;

                if (ImGui::BeginChild("##MainContent", ImVec2(content_w + 25, content_h), false, ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.095f, 0.095f, 0.10f, 1.0f));

                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.28f, 0.6f));

                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.2f, 0.23f, 1.0f));

                    float cp_pos = col_w - 45.0f;

                    ImGuiWindowFlags groupFlags = ImGuiWindowFlags_None;

                    if (active_tab == 0) {
                        ImGui::BeginGroup();
                        ImGui::BeginChild("##AimCol1", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "SILENT AIM");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Active", &bind_silent);

                            GUI::BindCheckbox("Magic Bullet (Rage)", &bind_silent_magic);

                            GUI::BindCheckbox("Draw FOV", &bind_draw_fov);
                            ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##fovcol", col_aim_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                            ImGui::Spacing();
                            GUI::Slider("FOV Radius", &silent_fov, 0.0f, 360.0f);
                            GUI::Slider("Max Dist", &silent_max_dist, 0.0f, 500.0f);
                            GUI::Slider("Spread", &silent_random_spread, 0.0f, 2.0f);

                            ImGui::Spacing(); ImGui::TextDisabled("Filters");
                            GUI::Checkbox("Real Crosshair FOV", &real_crosshair_fov);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves FOV circle to the actual game crosshair when aiming");
                            GUI::BindCheckbox("Ignore Walls", &bind_silent_walls);
                            GUI::BindCheckbox("Ignore Team", &bind_silent_team);
                            GUI::BindCheckbox("Ignore Skin", &bind_silent_skin);
                            GUI::BindCheckbox("Random Bone", &bind_silent_random);
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();

                        ImGui::SameLine(0, 20);

                        ImGui::BeginGroup();

                        ImGui::BeginChild("##AimCol2", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "LEGIT / MOUSE");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Mouse Aim", &bind_mouse_aim);
                            ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##mfovcol", col_mouse_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                            GUI::BindCheckbox("Draw FOV", &bind_draw_mouse_fov);
                            GUI::Checkbox("Vis Check", &mouse_check_vis);
                            ImGui::Spacing();
                            GUI::Slider("Smooth", &mouse_smooth, 1.0f, 50.0f);
                            GUI::Slider("FOV", &mouse_fov, 0.0f, 300.0f);

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "EXTRAS"); ImGui::Separator(); ImGui::Spacing();
                            GUI::BindCheckbox("Auto +C", &bind_autopc);
                            GUI::BindCheckbox("All Skills", &bind_all_skills);
                            GUI::BindCheckbox("Auto Peek", &bind_auto_peek);
                            if (enable_auto_peek) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##peekcol", col_auto_peek, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();
                    }
                    else if (active_tab == 1) {
                        ImGui::BeginGroup();
                        ImGui::BeginChild("##VisCol1", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "PLAYER ESP");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Box", &bind_box);
                            if (enable_box) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cbox", col_esp_box, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                const char* box_styles[] = { "2D", "Corner", "Filled" };
                                ImGui::SetNextItemWidth(90); ImGui::Combo("Style##Box", &style_box, box_styles, 3);
                            }

                            GUI::BindCheckbox("Snaplines", &bind_lines);
                            if (enable_lines) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cline", col_esp_lines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                const char* line_styles[] = { "Bottom", "Center", "Top" };
                                ImGui::SetNextItemWidth(90); ImGui::Combo("Style##Line", &style_lines, line_styles, 3);
                            }

                            GUI::BindCheckbox("Skeleton", &bind_skeleton);
                            if (enable_skeleton) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cskel", col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                GUI::Slider("Thick", &skeleton_thickness, 1.0f, 3.0f);
                            }

                            GUI::BindCheckbox("Name", &bind_name);
                            if (enable_name) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cname", col_esp_names, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }
                            GUI::BindCheckbox("Weapon", &bind_weapon);
                            GUI::BindCheckbox("ID Tags", &bind_id_tags);

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "WALL CHECK");
                            ImGui::Separator();
                            GUI::Checkbox("Enable Vis Check", &enable_esp_vis_check);
                            if (enable_esp_vis_check) {
                                ImGui::Text("Visible Color");
                                ImGui::SameLine(col_w - 45.0f);
                                ImGui::ColorEdit3("##vcol", col_esp_vis, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                                ImGui::Text("Occluded Color");
                                ImGui::SameLine(col_w - 45.0f);
                                ImGui::ColorEdit3("##ocol", col_esp_occ, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }

                            GUI::BindCheckbox("Bullet Tracers", &bind_tracers);
                            ImGui::SameLine();
                            ImGui::ColorEdit3("##tracercol", col_tracer, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            if (enable_tracers) GUI::Slider("Duration", &tracer_duration, 0.5f, 5.0f);
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();

                        ImGui::SameLine(0, 20);

                        ImGui::BeginGroup();
                        ImGui::BeginChild("##VisCol2", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "WORLD");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Health Bar", &bind_health);
                            if (enable_health) {
                                const char* hp_styles[] = { "Solid", "Gradient" };
                                ImGui::SetNextItemWidth(90); ImGui::Combo("Style##HP", &style_health, hp_styles, 2);
                            }
                            GUI::BindCheckbox("Armor Bar", &bind_armor);
                            if (enable_armor) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##carm", col_esp_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }
                            GUI::BindCheckbox("Glow Effect", &bind_esp_glow);
                            if (enable_esp_glow) GUI::Slider("Intensity", &glow_intensity, 0.1f, 1.0f);

                            ImGui::Spacing(); ImGui::TextDisabled("Chams");
                            GUI::BindCheckbox("Enable", &bind_chams);
                            if (enable_chams) {
                                GUI::BindCheckbox("Wireframe", &bind_chams_wireframe);
                                ImGui::Text("Vis"); ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cchamv", col_chams_vis, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                ImGui::Text("Inv"); ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##cchami", col_chams_inv, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }

                            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                            GUI::BindCheckbox("Miner WH", &bind_miner_wh);
                            if (enable_miner_wh) {
                                ImGui::Indent();
                                ImGui::ColorEdit3("Coal", col_miner[0], ImGuiColorEditFlags_NoInputs);
                                ImGui::ColorEdit3("Iron", col_miner[1], ImGuiColorEditFlags_NoInputs);
                                ImGui::ColorEdit3("Gold", col_miner[2], ImGuiColorEditFlags_NoInputs);
                                ImGui::ColorEdit3("Diamond", col_miner[3], ImGuiColorEditFlags_NoInputs);
                                ImGui::Unindent();
                            }
                            GUI::BindCheckbox("Frozen Lands", &bind_frozen_lands);
                            GUI::BindCheckbox("Arrows", &bind_arrows);
                            if (enable_arrows) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##carr", col_arrows, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                GUI::Slider("Radius", &arrows_radius, 50.f, 400.f);
                            }
                            GUI::BindCheckbox("Movement Trail", &bind_trail);
                            if (enable_trail) {
                                ImGui::SameLine(cp_pos); ImGui::ColorEdit3("##ctrail", col_trail, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();
                    }
                    else if (active_tab == 2) {
                        ImGui::BeginGroup();
                        ImGui::BeginChild("##MiscCol1", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "PLAYER");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Exploit TP (0,0,0)", &bind_tp_exploit);

                            GUI::BindCheckbox("AirBreak", &bind_airbreak);
                            if (enable_airbreak) GUI::Slider("Speed", &airbreak_speed, 0.0f, 4.0f);

                            GUI::BindCheckbox("Invisible", &bind_obj_surf);
                            if (enable_obj_surf) {
                                GUI::Slider("Catch Dist", &LastObjDistance, 10.0f, 300.0f);
                                ImGui::TextDisabled("Status: %s", HasActiveAnchor ? "ATTACHED" : "SEARCHING...");
                            }

                            GUI::BindCheckbox("Map Teleport", &bind_map_teleport);

                            GUI::BindCheckbox("No Clip", &bind_nocol);
                            GUI::BindCheckbox("No Buildings", &bind_nocol_buildings);
                            if (GUI::Checkbox("Gym Bot Active", &enable_gym_bot)) {
                                ToggleGymBot(enable_gym_bot);
                            }
                            GUI::Slider("Tap Delay (ms)", (float*)&gym_delay, 10.0f, 200.0f, "%.0f");
                            GUI::BindCheckbox("Anim Breaker", &bind_anim_breaker);
                            GUI::BindCheckbox("Voice Spy", &bind_voice_listen);
                            if (enable_voice_listen) {
                                ImGui::Indent(20.0f);

                                ImGui::Text("Target ID:");
                                ImGui::SameLine();

                                ImGui::PushItemWidth(80.0f);
                                if (ImGui::InputInt("##voice_target_id", &voice_target_id, 0, 0)) {
                                    if (voice_target_id < 0) voice_target_id = 0;
                                    if (voice_target_id > 1000) voice_target_id = 1000;
                                }
                                ImGui::PopItemWidth();

                                ImGui::Unindent(20.0f);
                            }

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.f), "EXPLOITS (RISKY)"); ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("CEF Auto Mine", &bind_cef_fast_mine);
                            if (enable_cef_fast_mine) {
                                GUI::Slider("Hold Time (s)", &mine_hold_time, 1.0f, 6.0f);
                            }

                            GUI::Checkbox("Fast Connect ESC", &enable_cef_spoof_admin);
                            if (enable_cef_spoof_admin) {
                                ImGui::Indent(10.0f);
                                ImGui::InputInt("Adm Level##adm", &cef_admin_level);
                                ImGui::Unindent(10.0f);
                            }

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "TAB VISUAL SPOOFER");
                            ImGui::Separator();
                            ImGui::Spacing();

                            GUI::Checkbox("Active Spoofer", &enable_cef_spoofing);

                            if (enable_cef_spoofing) {
                                ImGui::Indent();

                                ImGui::Text("Character");
                                ImGui::InputText("Nickname", spoof_nickname, 32);
                                ImGui::InputInt("Fake ID", &spoof_id);
                                ImGui::InputInt("Level", &spoof_lvl);
                                ImGui::InputInt("Admin Level", &spoof_admin_lvl);

                                ImGui::Text("Currency");
                                ImGui::InputInt("Cash", &spoof_money, 1000, 1000000);
                                ImGui::InputText("Bank ($)", spoof_bank, 32);

                                ImGui::Text("Organization");
                                ImGui::InputInt("Fraction ID", &spoof_fraction);
                                ImGui::InputInt("Rank ID", &spoof_rank);

                                ImGui::Text("Flags");
                                GUI::Checkbox("All Licenses", &spoof_all_licenses);
                                GUI::Checkbox("Max Skills", &spoof_max_skills);
                                GUI::Checkbox("Spoof in TAB", &spoof_tab_admin);

                                ImGui::Unindent();
                            }

                            GUI::BindCheckbox("Rvanka Hack", &bind_rvanka);
                            if (enable_rvanka) {
                                ImGui::Indent(10.0f);
                                const char* r_modes[] = { "In Car (Spin)", "On Foot" };
                                ImGui::SetNextItemWidth(100);
                                ImGui::Combo("Mode", &rvanka_mode, r_modes, 2);

                                GUI::Slider("Speed", &rvanka_speed, 10.f, 200.f);
                                GUI::Slider("FOV", &rvanka_fov, 10.f, 400.f);

                                char keyBuf[32];
                                sprintf_s(keyBuf, "Key: %s", keyNames[rvanka_key]);
                                if (ImGui::Button(keyBuf, ImVec2(100, 20))) {
                                    ImGui::OpenPopup("select_rvk_key");
                                }
                                if (ImGui::BeginPopup("select_rvk_key")) {
                                    if (ImGui::Selectable("L-ALT")) rvanka_key = VK_LMENU;
                                    if (ImGui::Selectable("L-SHIFT")) rvanka_key = VK_LSHIFT;
                                    if (ImGui::Selectable("L-CTRL")) rvanka_key = VK_LCONTROL;
                                    if (ImGui::Selectable("L-MOUSE")) rvanka_key = VK_LBUTTON;
                                    if (ImGui::Selectable("R-MOUSE")) rvanka_key = VK_RBUTTON;
                                    if (ImGui::Selectable("X")) rvanka_key = 'X';
                                    if (ImGui::Selectable("Z")) rvanka_key = 'Z';
                                    ImGui::EndPopup();
                                }

                                GUI::Checkbox("Draw FOV", &rvanka_draw_fov);
                                ImGui::SameLine(); ImGui::ColorEdit3("##rvkcol", col_rvanka_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                                ImGui::Unindent(10.0f);
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();

                        ImGui::SameLine(0, 20);

                        ImGui::BeginGroup();
                        ImGui::BeginChild("##MiscCol2", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.f), "VEHICLE");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::BindCheckbox("Car GodMode", &bind_car_godmode);
                            GUI::BindCheckbox("Nitro Control", &bind_nitro);
                            GUI::BindCheckbox("Auto Handbrake", &bind_auto_handbrake);
                            GUI::BindCheckbox("Faction Bypass", &bind_faction_bypass);
                            GUI::BindCheckbox("Drift Mode", &bind_drift);
                            if (enable_drift) GUI::Slider("Amount", &drift_amount, 0.1f, 2.0f);

                            GUI::BindCheckbox("Speedhack", &bind_speedhack);
                            if (enable_speedhack) {
                                GUI::Slider("Val", &speedhack_val, 1.0f, 5.0f);
                                ImGui::TextDisabled("Hold ALT to speedup");
                            }

                            GUI::BindCheckbox("Car Jump", &bind_car_jump);
                            if (enable_car_jump) {
                                GUI::Slider("Force", &car_jump_force, 0.1f, 2.0f);
                                ImGui::TextDisabled("Press SHIFT to jump");
                            }

                            GUI::BindCheckbox("Unlock Doors", &bind_open_doors);


                            GUI::BindCheckbox("Passenger kick", &bind_seat_tp);

                            if (enable_seat_tp) {
                                ImGui::Indent(10.0f);

                                GUI::Slider("FOV", &seat_tp_fov, 10.f, 500.f);

                                char keyBuf[32];
                                sprintf_s(keyBuf, "Key: %s", keyNames[seat_tp_key]);
                                if (ImGui::Button(keyBuf, ImVec2(100, 20))) {
                                    ImGui::OpenPopup("select_seat_tp_key");
                                }

                                if (ImGui::BeginPopup("select_seat_tp_key")) {
                                    if (ImGui::Selectable("L-ALT")) seat_tp_key = VK_LMENU;
                                    if (ImGui::Selectable("R-ALT")) seat_tp_key = VK_RMENU;
                                    if (ImGui::Selectable("L-SHIFT")) seat_tp_key = VK_LSHIFT;
                                    if (ImGui::Selectable("L-CTRL")) seat_tp_key = VK_LCONTROL;
                                    if (ImGui::Selectable("L-MOUSE")) seat_tp_key = VK_LBUTTON;
                                    if (ImGui::Selectable("R-MOUSE")) seat_tp_key = VK_RBUTTON;
                                    if (ImGui::Selectable("Z")) seat_tp_key = 'Z';
                                    if (ImGui::Selectable("X")) seat_tp_key = 'X';
                                    if (ImGui::Selectable("C")) seat_tp_key = 'C';
                                    ImGui::EndPopup();
                                }

                                GUI::Checkbox("Draw FOV", &draw_seat_tp_fov);
                                ImGui::SameLine();
                                ImGui::ColorEdit3("##seattpcol", col_seat_tp_fov, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                                ImGui::Unindent(10.0f);
                            }

                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();
                    }
                    else if (active_tab == 3) {
                        ImGui::BeginGroup();

                        ImGui::BeginChild("##CfgCol1", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.f), "INTERFACE");
                            ImGui::Separator(); ImGui::Spacing();

                            GUI::CustomGUI::ColorEditButton("Menu Accent Color", menu_color);

                            ImGui::Spacing(); ImGui::Spacing();
                            ImGui::TextDisabled("Watermark Settings");
                            ImGui::Spacing();

                            GUI::Checkbox("Show Name", &wm_show_link);
                            GUI::Checkbox("Show Time", &wm_show_time);
                            GUI::Checkbox("Show FPS", &wm_show_fps);

                            float availY = ImGui::GetContentRegionAvail().y;
                            if (availY > 40) ImGui::Dummy(ImVec2(0, availY - 40));

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.1f, 0.1f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.15f, 0.15f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.1f, 0.1f, 1.0f));
                            if (ImGui::Button("Unload Cheat", ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
                                show_menu = false;
                            }
                            ImGui::PopStyleColor(3);
                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();

                        ImGui::SameLine(0, 20);

                        ImGui::BeginGroup();
                        ImGui::BeginChild("##CfgCol2", ImVec2(col_w, 0.0f), true, groupFlags);
                        {
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.f), "CONFIGURATIONS");
                            ImGui::Separator(); ImGui::Spacing();

                            static char buf[64] = "";

                            GUI::CustomGUI::ConfigInput("##newcfg", buf, 64);

                            ImGui::Spacing();

                            ImVec4 accentCol = ImVec4(menu_color[0], menu_color[1], menu_color[2], 1.0f);
                            ImVec4 accentHover = ImVec4(menu_color[0] + 0.1f, menu_color[1] + 0.1f, menu_color[2] + 0.1f, 1.0f);

                            ImGui::PushStyleColor(ImGuiCol_Button, accentCol);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentHover);
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentCol);

                            if (ImGui::Button("Create Config", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                                if (strlen(buf) > 0) {
                                    ConfigSystem::SaveConfig(buf);
                                    buf[0] = '\0';
                                }
                            }
                            ImGui::PopStyleColor(3);

                            ImGui::Spacing();

                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.055f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

                            if (ImGui::BeginChild("##cfglist_render", ImVec2(0, 180), true)) {
                                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));

                                for (size_t i = 0; i < ConfigSystem::configFiles.size(); i++) {
                                    const bool is_selected = (ConfigSystem::selectedConfigIndex == i);

                                    if (is_selected) {
                                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.5f));
                                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(menu_color[0], menu_color[1], menu_color[2], 0.6f));
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                                    }
                                    else {
                                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.05f));
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                                    }

                                    ImGui::SetCursorPosX(8.0f);
                                    if (ImGui::Selectable(ConfigSystem::configFiles[i].c_str(), is_selected, 0, ImVec2(0, 25))) {
                                        ConfigSystem::selectedConfigIndex = i;
                                        sprintf_s(ConfigSystem::configNameBuffer, ConfigSystem::configFiles[i].c_str());
                                    }

                                    if (is_selected) ImGui::SetItemDefaultFocus();

                                    ImGui::PopStyleColor(3);
                                }
                                ImGui::PopStyleVar();
                            }
                            ImGui::EndChild();
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor(2);

                            ImGui::Spacing();

                            float btn_w = (ImGui::GetContentRegionAvail().x - 20) / 3;

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.2f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.25f, 0.4f, 1.0f));
                            if (ImGui::Button("Load", ImVec2(btn_w, 28))) {
                                if (ConfigSystem::selectedConfigIndex >= 0) ConfigSystem::LoadConfig(ConfigSystem::configFiles[ConfigSystem::selectedConfigIndex]);
                            }
                            ImGui::PopStyleColor(2);

                            ImGui::SameLine();

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.2f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.25f, 0.4f, 1.0f));
                            if (ImGui::Button("Save", ImVec2(btn_w, 28))) {
                                if (ConfigSystem::selectedConfigIndex >= 0) ConfigSystem::SaveConfig(ConfigSystem::configFiles[ConfigSystem::selectedConfigIndex]);
                            }
                            ImGui::PopStyleColor(2);

                            ImGui::SameLine();

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
                            if (ImGui::Button("Delete", ImVec2(btn_w, 28))) {
                                if (ConfigSystem::selectedConfigIndex >= 0) {
                                    ConfigSystem::DeleteConfig(ConfigSystem::configFiles[ConfigSystem::selectedConfigIndex]);
                                    ConfigSystem::selectedConfigIndex = -1;
                                }
                            }
                            ImGui::PopStyleColor(2);

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                            if (ImGui::Button("Refresh List", ImVec2(ImGui::GetContentRegionAvail().x, 20))) {
                                ConfigSystem::RefreshConfigs();
                            }
                            ImGui::PopStyleColor();

                        }
                        ImGui::EndChild();
                        ImGui::EndGroup();
                    }
                    else if (active_tab == 4) {
                        float windowWidth = ImGui::GetContentRegionAvail().x;
                        float cardWidth = 145.0f;
                        float cardHeight = 160.0f;
                        float spacingX = 15.0f;
                        float spacingY = 15.0f;

                        int columns = (int)(windowWidth / (cardWidth + spacingX));
                        if (columns < 1) columns = 1;

                        ImU32 accentColor = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 255);
                        ImU32 accentColorLow = IM_COL32((int)(menu_color[0] * 255), (int)(menu_color[1] * 255), (int)(menu_color[2] * 255), 40);
                        ImU32 textColSelected = IM_COL32(255, 255, 255, 255);
                        ImU32 textColNormal = IM_COL32(180, 180, 180, 255);

                        static char skinSearchBuf[64] = "";
                        ImGui::BeginGroup();
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##SkinSearch", "Search Skin ID...", skinSearchBuf, 64);
                        ImGui::PopStyleColor();
                        ImGui::PopStyleVar();
                        ImGui::EndGroup();

                        ImGui::Spacing(); ImGui::Spacing();

                        struct SkinInfo { int id; std::string name; };
                        static std::vector<SkinInfo> skinList;

                        if (skinList.empty()) {
                            skinList.push_back({ 0, "CJ (Carl)" });
                            skinList.push_back({ 2634, "Stalker Coat" });
                            skinList.push_back({ 2635, "Stalker Suit" });
                            skinList.push_back({ 2658, "Girl Hoodie" });
                            skinList.push_back({ 2773, "Jogger White" });
                            skinList.push_back({ 2774, "Suit Blue" });
                            skinList.push_back({ 2775, "Jogger Black" });

                            for (int i = 2687; i <= 2761; i++) {
                                std::string n = "Skin " + std::to_string(i);
                                if (i == 2696) n = "Darth Vader";
                                else if (i == 2697) n = "Stormtrooper";
                                else if (i == 2695) n = "Queen";
                                else if (i == 2693) n = "Towel Girl";
                                skinList.push_back({ i, n });
                            }

                            std::string path = ".\\amazing_cef\\sites\\main\\assets\\images\\inventory\\items\\skins\\";

                            if (fs::exists(path)) {
                                for (const auto& entry : fs::directory_iterator(path)) {
                                    if (entry.path().extension() == ".png") {
                                        std::string filename = entry.path().stem().string();

                                        try {
                                            int id = std::stoi(filename);

                                            bool exists = false;
                                            for (const auto& existing : skinList) {
                                                if (existing.id == id) {
                                                    exists = true;
                                                    break;
                                                }
                                            }

                                            if (!exists) {
                                                std::string name = "Skin ID: " + std::to_string(id);

                                                if (id >= 0 && id <= 299) {
                                                    name = GetSkinNameByID(id);
                                                }
                                                else {
                                                    name = "CEF Skin " + std::to_string(id);
                                                }

                                                skinList.push_back({ id, name });
                                            }
                                        }
                                        catch (...) {
                                            continue;
                                        }
                                    }
                                }
                            }

                            for (int i = 1; i <= 299; i++) {
                                bool exists = false;
                                for (const auto& s : skinList) { if (s.id == i) { exists = true; break; } }
                                if (!exists) {
                                    skinList.push_back({ i, GetSkinNameByID(i) });
                                }
                            }

                            std::sort(skinList.begin(), skinList.end(), [](const SkinInfo& a, const SkinInfo& b) {
                                return a.id < b.id;
                                });
                        }

                        ImGui::BeginChild("##SkinGrid", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
                        {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            int currentColumn = 0;

                            for (int i = 0; i < skinList.size(); i++) {
                                SkinInfo& skin = skinList[i];

                                std::string idStr = std::to_string(skin.id);
                                if (skinSearchBuf[0] != '\0') {
                                    std::string nLow = skin.name;
                                    bool match = (nLow.find(skinSearchBuf) != std::string::npos) || (idStr.find(skinSearchBuf) != std::string::npos);
                                    if (!match) continue;
                                }

                                if (currentColumn > 0) {
                                    ImGui::SameLine(0, spacingX);
                                }

                                ImVec2 p = ImGui::GetCursorScreenPos();
                                ImVec2 pMax = ImVec2(p.x + cardWidth, p.y + cardHeight);

                                ImGui::PushID(i);
                                bool clicked = ImGui::InvisibleButton("##skinbtn", ImVec2(cardWidth, cardHeight));
                                bool hovered = ImGui::IsItemHovered();
                                ImGui::PopID();

                                if (clicked) {
                                    iCustomSkinID = skin.id;
                                    ChangeSkin(skin.id);
                                }

                                bool isSelected = (iCustomSkinID == skin.id);

                                ImU32 bgCol = IM_COL32(25, 25, 30, 255);
                                if (hovered) bgCol = IM_COL32(35, 35, 40, 255);
                                if (isSelected) bgCol = IM_COL32(32, 32, 38, 255);
                                dl->AddRectFilled(p, pMax, bgCol, 8.0f);

                                ImU32 borderCol = IM_COL32(50, 50, 55, 255);
                                if (isSelected) borderCol = accentColor;
                                else if (hovered) borderCol = IM_COL32(80, 80, 90, 255);
                                dl->AddRect(p, pMax, borderCol, 8.0f, 0, isSelected ? 2.0f : 1.0f);

                                if (isSelected) {
                                    dl->AddRectFilledMultiColor(ImVec2(p.x, pMax.y - 80), pMax,
                                        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                        accentColorLow, accentColorLow);
                                }

                                IDirect3DDevice9* pDevice = nullptr;
                                extern IDirect3DDevice9* g_pd3dDevice;
                                IDirect3DTexture9* skinTex = nullptr;
                                if (g_pd3dDevice) skinTex = GetSkinTexture(skin.id, g_pd3dDevice);

                                if (skinTex) {
                                    ImVec2 imgMin = ImVec2(p.x + 10, p.y + 10);
                                    ImVec2 imgMax = ImVec2(pMax.x - 10, pMax.y - 35);
                                    dl->AddImageRounded((ImTextureID)skinTex, imgMin, imgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
                                }
                                else {
                                    float centerX = p.x + cardWidth / 2;
                                    float centerY = p.y + cardHeight / 2 - 15;
                                    ImU32 iconColor = isSelected ? accentColor : IM_COL32(55, 55, 60, 255);
                                    dl->AddCircleFilled(ImVec2(centerX, centerY - 20), 18.0f, iconColor);
                                    dl->AddRectFilled(ImVec2(centerX - 25, centerY + 5), ImVec2(centerX + 25, centerY + 45), iconColor, 6.0f, ImDrawFlags_RoundCornersTop);
                                }

                                ImGui::PushFont(font_logo);
                                ImVec2 txtSz = ImGui::CalcTextSize(skin.name.c_str());
                                dl->AddText(ImVec2(p.x + (cardWidth - txtSz.x) / 2, pMax.y - 38), isSelected ? textColSelected : textColNormal, skin.name.c_str());
                                ImGui::PopFont();

                                char idBuf[32]; sprintf_s(idBuf, "ID: %d", skin.id);
                                ImVec2 idSz = ImGui::CalcTextSize(idBuf);
                                dl->AddText(ImGui::GetFont(), 13.0f, ImVec2(p.x + (cardWidth - idSz.x) / 2, pMax.y - 18), IM_COL32(100, 100, 110, 255), idBuf);

                                currentColumn++;
                                if (currentColumn >= columns) {
                                    currentColumn = 0;
                                    ImGui::Dummy(ImVec2(0.0f, spacingY));
                                }
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::PopStyleVar(3);
                }
                ImGui::EndChild();
            }
            ImGui::End();

            RenderBindWindow();
        }

        /*if (show_cef_console) {
            g_CefConsole.Draw("CEF Interceptor Console", &show_cef_console);
        }*/

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
        pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);

        if (pStateBlock) pStateBlock->Apply();
    }
    return oEndScene(pDevice);
}

void* GetD3D9Method(uint32_t vTableIndex) {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return NULL;
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DX Dummy", NULL };
    RegisterClassEx(&wc);
    HWND hWnd = CreateWindow("DX Dummy", NULL, WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);
    D3DPRESENT_PARAMETERS d3dpp = {}; d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = hWnd;
    IDirect3DDevice9* pDummyDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);
    void* targetAddress = NULL;
    if (SUCCEEDED(hr) && pDummyDevice) { targetAddress = (*reinterpret_cast<void***>(pDummyDevice))[vTableIndex]; pDummyDevice->Release(); }
    pD3D->Release(); DestroyWindow(hWnd); UnregisterClass("DX Dummy", wc.hInstance);
    return targetAddress;
}

HRESULT __stdcall hkReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (init) {
        ImGui_ImplDX9_InvalidateDeviceObjects();

        if (tex_vis) {
            tex_vis->Release();
            tex_vis = nullptr;
        }
        if (tex_inv) {
            tex_inv->Release();
            tex_inv = nullptr;
        }
    }

    HRESULT hr = oReset(pDevice, pPresentationParameters);

    if (SUCCEEDED(hr) && init) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    return hr;
}

bool __fastcall hkRakClientRPC(void* pThis, void* edx, int* rpcId, BitStream* bitStream, int priority, int reliability, char orderingChannel, bool shiftTimestamp) {
    if (enable_faction_bypass && *rpcId == 26) {
        uint16_t vehId;
        uint8_t seatId;

        bitStream->ResetReadPointer();
        bitStream->Read(vehId);
        bitStream->Read(seatId);

        if (seatId == 0) {
            bitStream->ResetWritePointer();
            bitStream->Write(vehId);
            bitStream->Write((uint8_t)1);
        }
    }
    return oRakClientRPC(pThis, edx, rpcId, bitStream, priority, reliability, orderingChannel, shiftTimestamp);
}

DWORD jmpNoCol = 0x54CEFC;
DWORD jmpNormal = 0x54BCF4;

BYTE g_OriginalCollisionBytes[6] = { 0 };

void __declspec(naked) hkProcessCollisionSectorList() {
    __asm {
        pushad
        pushfd

        mov al, enable_nocol_buildings
        test al, al
        jnz CheatEnabled

        popfd
        popad
        jmp jmpNormal

        CheatEnabled :
        popfd
            popad
            jmp jmpNoCol
    }
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    OpenConsole();

    ConfigSystem::SetupFolders();
    ConfigSystem::RefreshConfigs();

    while (!GetModuleHandleA("d3d9.dll")) Sleep(100);
    while (!GetModuleHandleA("samp.dll")) Sleep(100);
    while (!GetModuleHandleA("libcef.dll")) Sleep(100);

    DWORD sampDll = (DWORD)GetModuleHandleA("samp.dll");
    DWORD* pSampInfo = NULL;
    DWORD* pRakClient = NULL;

    printf("[SYSTEM] Waiting for SAMP initialization...\n");

    while (true) {
        pSampInfo = (DWORD*)(sampDll + 0x26E8DC);
        if (pSampInfo && *pSampInfo) {
            pRakClient = (DWORD*)(*pSampInfo + 0x2C);
            if (pRakClient && *pRakClient) {
                break;
            }
        }
        Sleep(100);
    }
    printf("[SYSTEM] SAMP Structures found! RakClient at: %p\n", (void*)*pRakClient);

    void* pReset = GetD3D9Method(16);
    void* pEndScene = GetD3D9Method(42);
    void* pDrawIndexedPrimitive = GetD3D9Method(82);

    if (MH_Initialize() != MH_OK) {
        printf("[ERROR] MinHook init failed\n");
        return 0;
    }

    InitStringHook();

    void* pFireInstantHit = (void*)0x73FB10;
    MH_CreateHook(pFireInstantHit, &hkFireInstantHit, reinterpret_cast<void**>(&oFireInstantHit));
    MH_CreateHook(pReset, &hkReset, reinterpret_cast<void**>(&oReset));
    MH_CreateHook(pEndScene, &hkEndScene, reinterpret_cast<void**>(&oEndScene));
    MH_CreateHookApi(L"user32.dll", "SetCursorPos", &hkSetCursorPos, reinterpret_cast<void**>(&oSetCursorPos));
    MH_CreateHook((void*)0x542DD0, &hkApplyMoveSpeed, reinterpret_cast<void**>(&oApplyMoveSpeed));
    MH_CreateHook(pDrawIndexedPrimitive, &hkDrawIndexedPrimitive, reinterpret_cast<void**>(&oDrawIndexedPrimitive));

    BYTE origBytes[6];
    memcpy(origBytes, (void*)0x54BCEE, 6);
    InstallJmpHook(0x54BCEE, &hkProcessCollisionSectorList, 6);

    void** rakVTable = *(void***)(*pRakClient);

    if (MH_CreateHook(rakVTable[6], &hkRakPeerSend, (void**)&oRakPeerSend) == MH_OK) {
        printf("[SYSTEM] Hook Send - OK\n");
    }
    else {
        printf("[ERROR] Hook Send - FAILED\n");
    }

    if (MH_CreateHook(rakVTable[25], &hkRakClientRPC, (void**)&oRakClientRPC) == MH_OK) {
        printf("[SYSTEM] Hook RPC - OK\n");
    }
    else {
        printf("[ERROR] Hook RPC - FAILED\n");
    }

    MH_EnableHook(MH_ALL_HOOKS);

    if (enable_autopc) ToggleAutoC(false);
    if (enable_all_skills) ToggleAllSkills(false);
    if (enable_open_doors) ToggleOpenDoors(false);

    DWORD oldProt;
    VirtualProtect((void*)0x969165, 1, PAGE_EXECUTE_READWRITE, &oldProt);
    *(BYTE*)0x969165 = 0;
    VirtualProtect((void*)0x969165, 1, oldProt, &oldProt);

    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            break;
        }
        Sleep(100);
    }
    PatchBytes((void*)0x54BCEE, origBytes, 6);

    if (show_menu) {
        show_menu = false;
        ToggleGameInput(false);
        ShowCursor(FALSE);

        memset((void*)0xB73458, 0, 0x132);
        if (window) {
            RECT rect; GetWindowRect(window, &rect);
            oSetCursorPos(rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2);
        }
    }

    MH_DisableHook(MH_ALL_HOOKS);

    if (window && oWndProc) SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);

    if (tex_vis) { tex_vis->Release(); tex_vis = nullptr; }
    if (tex_inv) { tex_inv->Release(); tex_inv = nullptr; }

    if (init) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    MH_RemoveHook(&hkSetCursorPos);
    MH_Uninitialize();

    FreeConsole();

    Sleep(500);
    FreeLibraryAndExitThread((HMODULE)lpReserved, 0);
    return 0;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr);
    }
    return TRUE;
}