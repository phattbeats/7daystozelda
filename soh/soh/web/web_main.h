#ifndef WEB_MAIN_H
#define WEB_MAIN_H

#ifdef __EMSCRIPTEN__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Called from JavaScript when OTR files have been written to the Emscripten FS.
 */
void web_otr_loaded(void);

/**
 * Returns 1 if OTR files are loaded and available, 0 otherwise.
 */
int web_get_otr_status(void);

/**
 * Triggers FS.syncfs() to persist IDBFS data (saves, config) to IndexedDB.
 */
void web_save_to_idb(void);

/**
 * Initializes IDBFS mounts at /Save and loads persisted data from IndexedDB.
 */
void web_fs_init(void);

/**
 * Returns 1 when IDBFS has finished loading from IndexedDB, 0 otherwise.
 */
int web_is_idbfs_ready(void);

/**
 * Extracts a .z64 ROM file into a .o2r archive using ZAPD.
 * @param romPath   Path to the ROM in MEMFS (e.g., "/rom.z64")
 * @param outputPath Path for the output .o2r file (e.g., "/oot.o2r")
 * @return 0 on success, negative error code on failure
 */
int web_extract_rom(const char* romPath, const char* outputPath);

/**
 * Returns the ROM version string for a given ROM file.
 * @param romPath Path to the ROM in MEMFS
 * @return Static string with version info (e.g., "Vanilla", "MQ Master Quest")
 */
const char* web_get_rom_version(const char* romPath);

/**
 * Stub for exported function (config is now applied from C++ side).
 */
void web_configure_anchor(const char* room, const char* name, const char* color, const char* team);

#ifdef __cplusplus
/**
 * Reads Anchor config from JS globals (window._anchorConfig) set by shell.html
 * and applies it as CVars. Call this from OTRGlobals after CVar system is ready.
 */
void web_apply_anchor_config();
#endif

#ifdef __cplusplus
}
#endif

#endif /* __EMSCRIPTEN__ */

#endif /* WEB_MAIN_H */
