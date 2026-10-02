#ifdef __EMSCRIPTEN__

#include <stdio.h>
#include "soh/ShipInit.hpp"
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
#include "soh/Network/Anchor/Anchor.h"

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
        // Solo (no co-op link). Settings now persist across visits, so a co-op
        // session's Enabled=1 would otherwise auto-connect a solo game next time.
        CVarSetString(CVAR_REMOTE_ANCHOR("WebSocketURL"), wsUrl.c_str());
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0);
        // The lobby's 7 Days to Zelda checkbox applies to solo play too.
        int soloSevenDays = EM_ASM_INT({
            return (typeof window._sevenDaysSolo === 'string') ? (window._sevenDaysSolo === '1' ? 1 : 0) : -1;
        });
        if (soloSevenDays >= 0) {
            CVarSetInteger("gSevenDays.Enabled", soloSevenDays);
            ShipInit::Init("gSevenDays.Enabled");
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
    char* sevenDays = (char*)web_anchor_config_get("sevendays");
    char* fairy = (char*)web_anchor_config_get("fairy");
    char* tunic = (char*)web_anchor_config_get("tunic");

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
    // Fairy gradient "RRGGBB-RRGGBB" (core-aura). The aura is the old single color.
    if (fairy && strlen(fairy) == 13 && fairy[6] == '-') {
        unsigned int r = 255, g = 255, b = 255;
        sscanf(fairy, "%02x%02x%02x", &r, &g, &b);
        CVarSetColor24(CVAR_REMOTE_ANCHOR("FairyInner"), { (uint8_t)r, (uint8_t)g, (uint8_t)b });
        r = 100, g = 255, b = 100;
        sscanf(fairy + 7, "%02x%02x%02x", &r, &g, &b);
        CVarSetColor24(CVAR_REMOTE_ANCHOR("Color"), { (uint8_t)r, (uint8_t)g, (uint8_t)b });
    }
    if (tunic && strlen(tunic) == 6) {
        unsigned int r = 100, g = 255, b = 100;
        sscanf(tunic, "%02x%02x%02x", &r, &g, &b);
        CVarSetColor24(CVAR_REMOTE_ANCHOR("Tunic"), { (uint8_t)r, (uint8_t)g, (uint8_t)b });
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

    // 7 Days to Zelda (materials, crafting, tunic colors), opt-in like horde night.
    if (sevenDays && sevenDays[0]) {
        CVarSetInteger("gSevenDays.Enabled", sevenDays[0] == '1' ? 1 : 0);
        ShipInit::Init("gSevenDays.Enabled");
    }

    // A room + name in the link means "join this co-op session": connect on boot.
    if (room && room[0] && name && name[0]) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
        CVarSetInteger(CVAR_SETTING("BootSequence"), BOOTSEQUENCE_FILESELECT);
    }

    printf("[Web] Anchor configured. room=%s name=%s url=%s\n", room ? room : "", name ? name : "", wsUrl.c_str());
    free(room); free(name); free(color); free(team); free(ws); free(horde); free(hordeForce); free(sevenDays); free(fairy); free(tunic);
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
    EM_ASM({ if (window.sohPersist) window.sohPersist(); });
}

// Phones freeze a backgrounded page completely (no timers either), so a phone
// that switches to TeamSpeak keeps its Anchor seat while doing nothing. If it
// was the room's enemy authority, every enemy froze for everyone until the relay
// noticed the silence (~50 s). Instead the page leaves the room when hidden and
// rejoins when shown: authority moves to the next player within a tick.
static bool s_anchorSuspended = false;

EMSCRIPTEN_KEEPALIVE
void web_anchor_suspend(int suspend) {
    if (Anchor::Instance == nullptr) {
        return;
    }
    if (suspend) {
        if (Anchor::Instance->isEnabled) {
            printf("[Web] Page hidden on a phone: leaving the co-op room until it's back.\n");
            Anchor::Instance->Disable();
            s_anchorSuspended = true;
        }
    } else if (s_anchorSuspended) {
        s_anchorSuspended = false;
        if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
            printf("[Web] Page visible again: rejoining the co-op room.\n");
            Anchor::Instance->Enable();
        }
    }
}

// Background tabs get no requestAnimationFrame, so the game (and its Anchor
// traffic) froze whenever someone tabbed out. For the room's authority that
// froze every enemy for everyone. While hidden, drive the loop from timers.
// Browsers keep ~60 Hz timers for tabs that are playing audio; a silent tab
// may drop to 1 Hz, which still keeps the connection and authority alive.
EMSCRIPTEN_KEEPALIVE
void web_set_hidden(int hidden) {
    if (hidden) {
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 16);
    } else {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
    }
}

// Track whether IDBFS has finished loading from IndexedDB
static volatile int s_idbfs_ready = 0;

EM_JS(void, web_mount_idbfs, (), {
    // Two persistent directories:
    //   /Save     save files (the game writes there directly)
    //   /persist  copies of settings files that live in "/" (cvars, controller
    //             bindings, window layout). "/" itself cannot be an IDBFS mount
    //             (the game archives are written there), so they are copied in
    //             before boot and copied back on every sync.
    var dirs = ["/Save", "/persist"];
    for (var i = 0; i < dirs.length; i++) {
        try { FS.mkdir(dirs[i]); } catch (e) { /* may exist */ }
        FS.mount(IDBFS, {}, dirs[i]);
    }
    var settingsFiles = ["shipofharkinian.json", "imgui.ini"];

    var syncing = false, again = false;
    // Copy settings into /persist and flush both mounts to IndexedDB. Safe to
    // call often: overlapping calls coalesce into one follow-up sync.
    window.sohPersist = function() {
        if (!window._sohIdbReady) return;
        if (syncing) { again = true; return; }
        for (var i = 0; i < settingsFiles.length; i++) {
            try {
                var data = FS.readFile("/" + settingsFiles[i]);
                var dst = "/persist/" + settingsFiles[i];
                var same = false;
                try {
                    var old = FS.readFile(dst);
                    if (old.length === data.length) {
                        same = true;
                        for (var j = 0; j < data.length; j++) { if (old[j] !== data[j]) { same = false; break; } }
                    }
                } catch (e) {}
                if (!same) FS.writeFile(dst, data);
            } catch (e) { /* not written yet */ }
        }
        syncing = true;
        FS.syncfs(false, function(err) {
            syncing = false;
            if (err) console.error("[Web] Saving to browser storage failed:", err);
            if (again) { again = false; window.sohPersist(); }
        });
    };

    FS.syncfs(true, function(err) {
        if (err) {
            console.error("[Web] IDBFS load failed:", err);
        } else {
            try { console.log("[Web] IDBFS loaded. Saves: " + FS.readdir("/Save").filter(function(f) { return f[0] !== "."; }).join(", ")); } catch (e) {}
            for (var i = 0; i < settingsFiles.length; i++) {
                try {
                    var data = FS.readFile("/persist/" + settingsFiles[i]);
                    if (settingsFiles[i] === "shipofharkinian.json") {
                        // Do not restore fullscreen: the browser refuses it without a
                        // click, and a stuck fullscreen flag fights the page layout.
                        var cfg = JSON.parse(new TextDecoder().decode(data));
                        if (cfg && cfg.Window) delete cfg.Window.Fullscreen;
                        data = new TextEncoder().encode(JSON.stringify(cfg, null, 4));
                    }
                    FS.writeFile("/" + settingsFiles[i], data);
                    console.log("[Web] Restored " + settingsFiles[i]);
                } catch (e) { /* first visit, or unreadable: start from defaults */ }
            }
        }
        window._sohIdbReady = true;
        // Saves are flushed right after each save; this catches settings changes.
        setInterval(window.sohPersist, 20000);
        document.addEventListener("visibilitychange", function() {
            if (document.visibilityState === "hidden") window.sohPersist();
        });
        window.addEventListener("pagehide", window.sohPersist);
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
