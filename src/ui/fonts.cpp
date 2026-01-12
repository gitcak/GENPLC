#include "fonts.h"
#include <M5Unified.h>
#include "config/system_config.h"
#if ENABLE_SD
#include <SD.h>
#endif

/*
 * Font System Documentation:
 * - Fonts are loaded from SD card at /fonts/*.vlw
 * - Font files are stored in data/fonts/ directory (copied to SD during deployment)
 * - No embedded fonts in src/assets/ - all fonts loaded dynamically from SD
 * - Fallback to system default fonts if SD fonts unavailable
 */

namespace ui { namespace fonts {

static String s_fontPath = "/fonts/Montserrat-SemiBoldItalic-16.vlw"; // default
static bool s_checked = false;
static bool s_available = false;

bool init() {
    if (s_checked) return s_available;
    s_checked = true;
#if ENABLE_SD
    // Check SD for font file
    if (SD.begin() && SD.exists(s_fontPath.c_str())) {
        s_available = true;
    } else {
        // Try a couple of alternates
        const char* alts[] = {
            "/fonts/Montserrat-SemiBoldItalic-12.vlw",
            "/fonts/Montserrat-SemiBoldItalic-16.vlw",
            "/fonts/Montserrat-SemiBoldItalic-24.vlw"
        };
        for (auto p : alts) {
            if (SD.begin() && SD.exists(p)) { s_fontPath = p; s_available = true; break; }
        }
    }
#else
    // SD disabled; no custom fonts
    s_available = false;
#endif
    return s_available;
}

bool applyToDisplay() {
    if (!s_checked) init();
    if (!s_available) return false;
    return M5.Display.loadFont(s_fontPath.c_str());
}

bool applyToSprite(lgfx::LGFX_Sprite* s) {
    if (!s) return false;
    if (!s_checked) init();
    if (!s_available) return false;
    return s->loadFont(s_fontPath.c_str());
}

} }