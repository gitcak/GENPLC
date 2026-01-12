#include "theme.h"

namespace ui {

static Theme s_theme {
    /*bg*/           0x0008,      // near-black (deep navy)
    /*card*/         0x0841,      // dark blue-gray fill behind gradient edges
    /*border*/       0x7BEF,      // soft light gray border
    /*accent*/       0x4A69,      // subtle accent line
    /*text*/         0xFFFF,      // WHITE
    /*textSecondary*/0xC618,      // light gray text
    /*green*/        0x07E0,      // GREEN
    /*yellow*/       0xFFE0,      // YELLOW
    /*red*/          0xF800,      // RED
    /*gradTop*/      0x780F,      // purple
    /*gradBottom*/   0x07FF,      // cyan
    /*radius*/       10,
    /*pad*/          10
};

const Theme& theme() { return s_theme; }

}
