#include <raylib.h>
#include "Renderer.h"
#include "raymath.h"
#include "Game.h"
#include "bootstrap/instances/Script.h"
#include "core/logging/Logging.h"
#include "subsystems/filesystem/FileSystem.h"
#include "instances/InstanceTypes.h"
#include "services/RunService.h"
#include "services/Lighting.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "icon.h"
#include "icon_ico_file.h"
#include "place_file.h"
#include "preloaded_scripts.h"

// avoid Win32 name collisions
#define WIN32_LEAN_AND_MEAN
#define CloseWindow Win32CloseWindow
#define ShowCursor  Win32ShowCursor
#include <windows.h>
#undef DrawText
#undef CloseWindow
#undef ShowCursor

static Camera3D g_camera{};

// yaw/pitch state
static float gYaw = 0.0f;
static float gPitch = 0.0f;
static bool gAnglesInit = false;
static int gTargetFPS = 0;
static std::vector<std::string> gPaths;
static bool gNoPlace = false;
static bool args = false;

static void PhysicsSimulation() {
    // stub
}

static void Cleanup();

static int LoaderMenu(int padding = 38, int gap = 10,
                      int headerHeight = 90, int instrToOptionsGap = 100) {
    int choice = 0;
    int selected = 0;

    const char* options[] = {
        "visual-darkhouse.luau",
        "visual-tempform.luau",
        "visual-sphere.luau",
        "part_example.luau"
    };
    const int optionCount = 4;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_DOWN)) selected = (selected + 1) % optionCount;
        if (IsKeyPressed(KEY_UP))   selected = (selected - 1 + optionCount) % optionCount;

        BeginDrawing();
        ClearBackground(BLACK);

        // header bar
        DrawRectangle(0, 0, GetScreenWidth(), headerHeight, WHITE);
        DrawText("Librebox Engine Demo", padding, headerHeight/2 - 20, 40, BLACK);

        // instructions
        int instrY = headerHeight + 20;
        DrawText("WASD to move, Arrow keys rotate", padding, instrY, 20, GRAY);
        DrawText("Use UP/DOWN to select, ENTER or (1-4) to confirm", padding, instrY + 30, 20, GRAY);
        DrawText("Press F11 to full-screen in-game", padding, instrY + 60, 20, GRAY);

        // options start after instructions + adjustable gap
        int baseY = instrY + 45 + instrToOptionsGap;
        int boxW = 400;
        int boxH = 50;
        for (int i = 0; i < optionCount; i++) {
            int y = baseY + i * (boxH + gap);
            Color bg = (i == selected) ? DARKGRAY : RAYWHITE;
            Color fg = (i == selected) ? YELLOW   : BLACK;

            DrawRectangle(padding, y, boxW, boxH, bg);
            DrawText(TextFormat("(%d) %s", i+1, options[i]),
                     padding + 15, y + (boxH/2 - 10), 28, fg);
        }

        int creditsY = baseY + optionCount * (boxH + gap) + 40;
        DrawText("Made by Librebox Developers", padding, creditsY, 20, RED);

        EndDrawing();

        if (IsKeyPressed(KEY_ONE))   { choice = 1; break; }
        if (IsKeyPressed(KEY_TWO))   { choice = 2; break; }
        if (IsKeyPressed(KEY_THREE)) { choice = 3; break; }
        if (IsKeyPressed(KEY_FOUR))  { choice = 4; break; }
        if (IsKeyPressed(KEY_ENTER)) { choice = selected + 1; break; }
    }
    return choice;
}

static void Stage_ConfigInitialization() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    std::atexit(&Cleanup);
    LOGI("Loaded configuration");
}

static void LoadAndScheduleScript(const std::string& name, const std::string& path) {
    auto script = std::make_shared<Script>(name, fsys::ReadFileToString(path));
    if (g_game && g_game->workspace) {
        script->SetParent(g_game->workspace);
    }
    script->Schedule();
    LOGI("Scheduled script: %s", path.c_str());
}

static bool Preflight_ValidatePaths() {
    if (gPaths.empty()) return true;
    namespace fs = std::filesystem;
    for (const auto& pathStr : gPaths) {
        fs::path p(pathStr);
        if (!fs::exists(p)) {
            if (p.has_extension()) {
                LOGE("Script file not found: %s", pathStr.c_str());
                return false; // exit before window/renderer
            } else {
                LOGE("Path does not exist: %s", pathStr.c_str());
            }
        }
    }
    return true;
}

static void Stage_Initialization() {
    LOGI("Stage: Initialization begin");

    InitWindow(1280, 720, "Librebox Player 1.0.0");

    // setup icons
    Image icon = {
        .data = icon_data,
        .width = icon_data_width,
        .height = icon_data_height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };

    SetWindowIcon(icon);
    HICON hIcon = CreateIconFromResourceEx(
        (PBYTE)icon_ico,
        (DWORD)icon_ico_len,
        TRUE,
        0x00030000,
        0, 0,
        LR_DEFAULTCOLOR
    );

    if (hIcon) {
        HWND hwnd = GetActiveWindow();
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }


    LOGI("Raylib window initialized");

    int selected = 0;
    
    if (!args) {
        selected = LoaderMenu();
    }

    g_game = std::make_shared<Game>();
    g_game->Init();

    // load script if needed
    if (selected > 0) {
        const unsigned char* data = nullptr;
        size_t len = 0;

        switch (selected) {
            case 1: data = preloaded_script_1; len = preloaded_script_1_len; break;
            case 2: data = preloaded_script_2; len = preloaded_script_2_len; break;
            case 3: data = preloaded_script_3; len = preloaded_script_3_len; break;
            case 4: data = preloaded_script_4; len = preloaded_script_4_len; break;
        }

        if (data && len) {
            auto script = std::make_shared<Script>(
                "preloaded",
                std::string(reinterpret_cast<const char*>(data), len)
            );
            if (g_game->workspace) {
                script->SetParent(g_game->workspace);
            }
            script->Schedule();
            LOGI("Scheduled preloaded script %d", selected);
        }
    }

    g_camera = {};
    g_camera.up = {0.0f, 1.0f, 0.0f};
    g_camera.fovy = 70.0f;
    g_camera.projection = CAMERA_PERSPECTIVE;
    if (g_game->workspace && g_game->workspace->camera) {
        g_camera.position = g_game->workspace->camera->Position;
        g_camera.target   = g_game->workspace->camera->Target;
    } else {
        g_camera.position = {0, 2, -5};
        g_camera.target   = {0, 2, 0};
    }

    // init yaw/pitch from initial camera
    if (!gAnglesInit) {
        Vector3 f0 = Vector3Normalize(Vector3Subtract(g_camera.target, g_camera.position));
        gYaw   = atan2f(f0.z, f0.x);
        gPitch = asinf(Clamp(f0.y, -1.0f, 1.0f));
        gAnglesInit = true;
    }

    // Load the place script
    if (!gNoPlace) {
        auto placeInitScript = std::make_shared<Script>("place", std::string(place_script, place_script_len));
        placeInitScript->Schedule();
    }

    // Path handling (supports multiple --path)
    if (!gPaths.empty()) {
        namespace fs = std::filesystem;
        for (const auto& pathStr : gPaths) {
            fs::path p(pathStr);
            if (fs::exists(p)) {
                if (fs::is_regular_file(p)) {
                    LoadAndScheduleScript(p.stem().string(), p.string()); // run regardless of extension
                } else if (fs::is_directory(p)) {
                    for (auto& entry : fs::directory_iterator(p)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".luau") {
                            LoadAndScheduleScript(entry.path().stem().string(), entry.path().string());
                        }
                    }
                } else {
                    LOGE("Path is neither file nor directory: %s", pathStr.c_str());
                }
            } else {
                // Already reported during preflight
            }
        }
    }

    LOGI("Stage: Initialization end");
}

static void Stage_Run() {
    LOGI("Stage: Run loop begin");
    auto rs = std::dynamic_pointer_cast<RunService>(Service::Get("RunService"));
    auto ls = std::dynamic_pointer_cast<Lighting>(Service::Get("Lighting"));

    while (!WindowShouldClose()) {
        const double now = GetTime();
        const double dt  = GetFrameTime();

        lua_State* Lm = (g_game && g_game->luaScheduler) ? g_game->luaScheduler->GetMainState() : nullptr;
        if (rs && Lm) {
            rs->EnsureSignals();
            if (rs->PreRender && !rs->PreRender->IsClosed()) {
                lua_pushnumber(Lm, dt);
                rs->PreRender->Fire(Lm, lua_gettop(Lm), 1);
                lua_pop(Lm, 1);
            }
            if (rs->PreAnimation && !rs->PreAnimation->IsClosed()) {
                lua_pushnumber(Lm, dt);
                rs->PreAnimation->Fire(Lm, lua_gettop(Lm), 1);
                lua_pop(Lm, 1);
            }
            if (rs->PreSimulation && !rs->PreSimulation->IsClosed()) {
                lua_pushnumber(Lm, now);
                lua_pushnumber(Lm, dt);
                rs->PreSimulation->Fire(Lm, lua_gettop(Lm)-1, 2);
                lua_pop(Lm, 2);
            }

            PhysicsSimulation();

            if (rs->PostSimulation && !rs->PostSimulation->IsClosed()) {
                lua_pushnumber(Lm, dt);
                rs->PostSimulation->Fire(Lm, lua_gettop(Lm), 1);
                lua_pop(Lm, 1);
            }
            if (rs->Heartbeat && !rs->Heartbeat->IsClosed()) {
                lua_pushnumber(Lm, dt);
                rs->Heartbeat->Fire(Lm, lua_gettop(Lm), 1);
                lua_pop(Lm, 1);
            }
        }
        if (g_game && g_game->luaScheduler)
            g_game->luaScheduler->Step(GetTime(), dt);

        // --- Input and camera ---
        float moveSpeed = 25.0f * dt;
        float rotSpeed  = 2.0f * dt;

        // yaw/pitch
        if (IsKeyDown(KEY_RIGHT)) gYaw   += rotSpeed;
        if (IsKeyDown(KEY_LEFT))  gYaw   -= rotSpeed;
        if (IsKeyDown(KEY_UP))    gPitch += rotSpeed;
        if (IsKeyDown(KEY_DOWN))  gPitch -= rotSpeed;
        const float kMaxPitch = DEG2RAD*89.0f;
        gPitch = Clamp(gPitch, -kMaxPitch, kMaxPitch);

        // rebuild basis
        Vector3 forward = {
            cosf(gPitch)*cosf(gYaw),
            sinf(gPitch),
            cosf(gPitch)*sinf(gYaw)
        };
        Vector3 worldUp = {0,1,0};
        Vector3 right   = Vector3CrossProduct(worldUp, forward);
        if (Vector3LengthSqr(right) < 1e-6f) right = Vector3{1,0,0};
        right = Vector3Normalize(right);
        Vector3 up      = Vector3CrossProduct(forward, right);

        // movement
        Vector3 delta = {0};
        if (IsKeyDown(KEY_W)) delta = Vector3Add(delta, Vector3Scale(forward, moveSpeed));
        if (IsKeyDown(KEY_S)) delta = Vector3Subtract(delta, Vector3Scale(forward, moveSpeed));
        if (IsKeyDown(KEY_D)) delta = Vector3Subtract(delta, Vector3Scale(right,   moveSpeed));
        if (IsKeyDown(KEY_A)) delta = Vector3Add(delta, Vector3Scale(right,   moveSpeed));

        g_camera.position = Vector3Add(g_camera.position, delta);
        g_camera.target   = Vector3Add(g_camera.position, forward);
        g_camera.up       = up;

        RenderFrame(g_camera);
    }

    LOGI("Stage: Run loop end");
}

static void Cleanup() {
    LOGI("Cleanup begin");
    if (g_game) {
        g_game->Shutdown();
        g_game.reset();
    }
    CloseWindow();
    LOGI("Cleanup end");
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--target-fps") == 0 && i + 1 < argc) {
            gTargetFPS = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            gPaths.push_back(argv[++i]);
            args = true;
        } else if (std::strcmp(argv[i], "--no-place") == 0) {
            gNoPlace = true;
        } else if (i == 1) {
            // first non-flag argument
            std::string arg = argv[i];
            if (arg.find(' ') == std::string::npos &&
                arg.size() > 4 &&
                arg.rfind(".luau") == arg.size() - 4) {
                gPaths.push_back(arg);
                args = true;
            }
        }
    }

    Stage_ConfigInitialization();

    if (!Preflight_ValidatePaths()) {
        return EXIT_FAILURE; // print error then exit before window/renderer
    }

    LOGI("-----------------------------");
    LOGI("Librebox Player (Client)");
    LOGI("License: MIT license");
    LOGI("Author: Librebox Developers");
    LOGI("-----------------------------");

    Stage_Initialization();

    if (gTargetFPS > 0) {
        SetTargetFPS(gTargetFPS);
        LOGI("Target FPS set to %d", gTargetFPS);
    }

    Stage_Run();
    return 0;
}