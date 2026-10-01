#ifdef __EMSCRIPTEN__

#include <stdio.h>
#include <string.h>
#include <fstream>
#include <atomic>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "web_main.h"
#include "soh/Extractor/Extract.h"
#include <ship/utils/binarytools/BitConverter.h>
#include <libultraship/libultraship.h>
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/enhancementTypes.h"

// ---- Touch Gamepad Bridge ----
// Read touch gamepad state from JavaScript and merge into OSContPad

// N64 button masks (from libultra/os.h)
#define N64_A      0x8000
#define N64_B      0x4000
#define N64_Z      0x2000
#define N64_START  0x1000
#define N64_L      0x0020
#define N64_R      0x0010
#define N64_CU     0x0008
#define N64_CD     0x0004
#define N64_CL     0x0002
#define N64_CR     0x0001

EM_JS(int, web_touch_active, (), {
    return (typeof TouchGamepad !== 'undefined' && TouchGamepad.isActive()) ? 1 : 0;
});

EM_JS(int, web_touch_stick_x, (), {
    return (typeof TouchGamepad !== 'undefined') ? TouchGamepad.getStickX() : 0;
});

EM_JS(int, web_touch_stick_y, (), {
    return (typeof TouchGamepad !== 'undefined') ? TouchGamepad.getStickY() : 0;
});

EM_JS(int, web_touch_buttons, (), {
    if (typeof TouchGamepad === 'undefined' || !TouchGamepad.isActive()) return 0;
    var b = 0;
    if (TouchGamepad.isButtonPressed('tb-a'))     b |= 0x8000;
    if (TouchGamepad.isButtonPressed('tb-b'))     b |= 0x4000;
    if (TouchGamepad.isButtonPressed('tb-z'))     b |= 0x2000;
    if (TouchGamepad.isButtonPressed('tb-start')) b |= 0x1000;
    if (TouchGamepad.isButtonPressed('tb-l'))     b |= 0x0020;
    if (TouchGamepad.isButtonPressed('tb-r'))     b |= 0x0010;
    if (TouchGamepad.isButtonPressed('tb-cu'))    b |= 0x0008;
    if (TouchGamepad.isButtonPressed('tb-cd'))    b |= 0x0004;
    if (TouchGamepad.isButtonPressed('tb-cl'))    b |= 0x0002;
    if (TouchGamepad.isButtonPressed('tb-cr'))    b |= 0x0001;
    return b;
});

#include <libultraship/libultra/controller.h>

extern "C" void WebTouchGamepad_MergeInput(OSContPad* pad) {
    if (!web_touch_active()) return;

    // Merge buttons (OR with existing)
    pad->button |= (uint16_t)web_touch_buttons();

    // Merge stick (touch overrides if non-zero)
    int sx = web_touch_stick_x();
    int sy = web_touch_stick_y();
    if (sx != 0 || sy != 0) {
        pad->stick_x = (int8_t)sx;
        pad->stick_y = (int8_t)sy;
    }
}

// ---- Anchor config from URL hash (read JS globals set by shell.html) ----

EM_JS(int, web_has_anchor_config, (), {
    return (typeof window._anchorConfig !== 'undefined') ? 1 : 0;
});

EM_JS(const char*, web_anchor_config_get, (const char* key), {
    if (typeof window._anchorConfig === 'undefined') return 0;
    var k = UTF8ToString(key);
    var val = window._anchorConfig[k] || '';
    var len = lengthBytesUTF8(val) + 1;
    var ptr = _malloc(len);
    stringToUTF8(val, ptr, len);
    return ptr;
});

// Called from OTRGlobals.cpp after the CVar system is ready
// Same-origin relay URL: wss://<host>/anchor on https pages, ws:// otherwise.
EM_JS(char*, web_default_anchor_url, (), {
    var proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    var url = proto + location.host + '/anchor';
    var len = lengthBytesUTF8(url) + 1;
    var buf = _malloc(len);
    stringToUTF8(url, buf, len);
    return buf;
});

void web_apply_anchor_config() {
    // The relay URL always defaults to this page's own host, so a self-hosted
    // bundle (page + bridge behind one reverse proxy) needs no configuration.
    char* defUrl = web_default_anchor_url();
    std::string wsUrl = defUrl ? defUrl : "ws://localhost:8080/anchor";
    free(defUrl);

    if (!web_has_anchor_config()) {
        if (std::string(CVarGetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), "")).empty()) {
            CVarSetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), wsUrl.c_str());
        }
        return;
    }

    char* room  = (char*)web_anchor_config_get("room");
    char* name  = (char*)web_anchor_config_get("name");
    char* color = (char*)web_anchor_config_get("color");
    char* team  = (char*)web_anchor_config_get("team");
    char* ws    = (char*)web_anchor_config_get("ws");
    char* horde = (char*)web_anchor_config_get("horde");
    char* hordeForce = (char*)web_anchor_config_get("hordeforce");

    if (ws && ws[0]) {
        wsUrl = ws;
    }
    CVarSetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), wsUrl.c_str());

    if (room && room[0]) {
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), room);
    }
    if (name && name[0]) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), name);
    }
    if (color && color[0] && strlen(color) == 6) {
        unsigned int r = 100, g = 255, b = 100;
        sscanf(color, "%02x%02x%02x", &r, &g, &b);
        CVarSetColor24(CVAR_REMOTE_ANCHOR("Color"), { (uint8_t)r, (uint8_t)g, (uint8_t)b });
    }
    if (team && team[0]) {
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), team);
    }

    // Horde night is an opt-in game mode; the link can switch it on for the session.
    if (horde && horde[0]) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("HordeNight"), horde[0] == '1' ? 1 : 0);
    }
    if (hordeForce && hordeForce[0]) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("HordeNightForce"), hordeForce[0] == '1' ? 1 : 0);
    }

    // A room + name in the link means "join this co-op session": connect on boot.
    if (room && room[0] && name && name[0]) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
        CVarSetInteger(CVAR_SETTING("BootSequence"), BOOTSEQUENCE_FILESELECT);
    }

    printf("[Web] Anchor configured. room=%s name=%s url=%s\n", room ? room : "", name ? name : "", wsUrl.c_str());
    free(room); free(name); free(color); free(team); free(ws); free(horde); free(hordeForce);
}

static int s_otr_loaded = 0;

#include <imgui.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE
int web_wants_text_input(void) {
    return ImGui::GetIO().WantTextInput ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void web_otr_loaded(void) {
    s_otr_loaded = 1;
    printf("[Web] OTR files loaded into virtual filesystem.\n");
}

EMSCRIPTEN_KEEPALIVE
int web_get_otr_status(void) {
    return s_otr_loaded;
}

EMSCRIPTEN_KEEPALIVE
void web_save_to_idb(void) {
    emscripten_run_script(
        "if (typeof FS !== 'undefined' && FS.syncfs) {"
        "  FS.syncfs(false, function(err) {"
        "    if (err) console.error('[Web] FS.syncfs save failed:', err);"
        "    else console.log('[Web] FS.syncfs save complete.');"
        "  });"
        "}"
    );
}

// Track whether IDBFS has finished loading from IndexedDB
static volatile int s_idbfs_ready = 0;

EM_JS(void, web_mount_idbfs, (), {
    // Mount IDBFS at the actual directories the game uses:
    // GetAppDirectoryPath() returns "." on Emscripten, so saves go to ./Save/
    // and config goes to ./  (cvars.cfg, imgui.ini)
    var dirs = ["/Save"];
    for (var i = 0; i < dirs.length; i++) {
        try { FS.mkdir(dirs[i]); } catch (e) { /* may exist */ }
        FS.mount(IDBFS, {}, dirs[i]);
    }
    FS.syncfs(true, function(err) {
        if (err) {
            console.error("[Web] IDBFS load failed:", err);
        } else {
            console.log("[Web] IDBFS loaded. Files in /Save/:");
            try {
                var files = FS.readdir("/Save");
                console.log("[Web]   " + files.join(", "));
            } catch(e) {}
        }
        // Signal C side that IDBFS is ready
        setValue(_web_idbfs_ready_ptr(), 1, 'i32');
    });
});

EMSCRIPTEN_KEEPALIVE
int* web_idbfs_ready_ptr(void) {
    return (int*)&s_idbfs_ready;
}

void web_fs_init(void) {
    web_mount_idbfs();
    printf("[Web] IDBFS mount initiated for /Save.\n");
}

int web_is_idbfs_ready(void) {
    return s_idbfs_ready;
}

EMSCRIPTEN_KEEPALIVE
int web_extract_rom(const char* romPath, const char* outputPath) {
    printf("[Web] Starting ROM extraction: %s -> %s\n", romPath, outputPath);

    Extractor extractor;

    // Use RunFileStandalone to validate and set up the extractor
    if (!extractor.RunFileStandalone(std::string(romPath))) {
        fprintf(stderr, "[Web] ROM validation failed\n");
        return -3;
    }

    bool isMQ = extractor.IsMasterQuest();
    const char* expectedFile = isMQ ? "oot-mq.o2r" : "oot.o2r";
    printf("[Web] ROM validated. Version: %s\n", isMQ ? "Master Quest" : "Vanilla");

    // Run ZAPD extraction — output goes to "/" + expectedFile
    extractor.CallZapd("/", "/");

    // Verify output was created
    std::string actualOutput = std::string("/") + expectedFile;
    std::ifstream checkFile(actualOutput, std::ios::binary | std::ios::ate);
    if (!checkFile.is_open() || checkFile.tellg() == 0) {
        fprintf(stderr, "[Web] Extraction failed - output file not created: %s\n", actualOutput.c_str());
        return -4;
    }

    printf("[Web] Extraction complete. Output: %s (%lld bytes)\n", actualOutput.c_str(), (long long)checkFile.tellg());
    return isMQ ? 1 : 0; // 0 = vanilla success, 1 = MQ success, negative = error
}

EMSCRIPTEN_KEEPALIVE
const char* web_get_rom_version(const char* romPath) {
    static char versionBuf[64];
    versionBuf[0] = '\0';

    Extractor extractor;
    if (!extractor.RunFileStandalone(std::string(romPath))) {
        snprintf(versionBuf, sizeof(versionBuf), "unknown");
        return versionBuf;
    }

    snprintf(versionBuf, sizeof(versionBuf), "%s%s",
             extractor.IsMasterQuest() ? "MQ " : "",
             extractor.IsMasterQuest() ? "Master Quest" : "Vanilla");
    return versionBuf;
}

EMSCRIPTEN_KEEPALIVE
void web_configure_anchor(const char* room, const char* name, const char* color, const char* team) {
    // Deprecated: use web_apply_anchor_config() from C++ instead
    // Kept as stub to avoid link errors from EXPORTED_FUNCTIONS
    printf("[Web] web_configure_anchor called (stub) - config is applied from OTRGlobals\n");
}

} // extern "C"

#endif /* __EMSCRIPTEN__ */
