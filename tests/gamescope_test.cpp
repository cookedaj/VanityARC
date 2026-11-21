//gamescope command-line reading, and where the game's picture ends up on the overlay.
#include <sys/wait.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "utils/gamescope.h"
#include "utils/gameview.h"

static int fails = 0;
#define CHECK(cond, msg) do { bool ok_ = (cond); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) fails++; } while (0)
static bool near(double a, double b) { return std::abs(a - b) < 1e-3; }

int main() {
    using gamescope::parseArgs;
    //the command from the launch options
    auto g = parseArgs({"gamescope", "-w", "1440", "-h", "1080", "-W", "1920", "-H", "1080", "-S", "stretch", "-f", "--", "/path/game", "-w", "1"});
    CHECK(g.found && g.nestedW == 1440 && g.nestedH == 1080 && g.outW == 1920 && g.outH == 1080, "sizes read from the launch command");
    CHECK(g.scaler == "stretch" && g.fullscreen, "scaler and -f read");
    CHECK(g.renderW() == 1440 && g.renderH() == 1080, "the game renders at -w x -h");

    auto g2 = parseArgs({"gamescope", "--nested-width=1280", "--nested-height", "720", "--output-width=2560", "--output-height=1440", "--scaler", "fit"});
    CHECK(g2.nestedW == 1280 && g2.nestedH == 720 && g2.outW == 2560 && g2.outH == 1440 && g2.scaler == "fit", "long options, with = or a space");
    auto g3 = parseArgs({"gamescope", "-w1440", "-h1080"});
    CHECK(g3.nestedW == 1440 && g3.nestedH == 1080 && g3.outW == 1440 && g3.outH == 1080, "-w1440 style, and an unset output follows the game");
    auto g4 = parseArgs({"gamescope", "-W", "1920", "-H", "1080", "--", "-w", "640", "-h", "480"});
    CHECK(g4.renderW() == 1920 && g4.renderH() == 1080 && g4.nestedW == 1920, "the game's own arguments after -- are ignored");
    auto g5 = parseArgs({"gamescope", "-w", "abc", "-h", "-5", "-W", "99999"});
    CHECK(g5.found && g5.renderW() == 0 && g5.renderH() == 0, "nonsense sizes are ignored, not trusted");
    auto g6 = parseArgs({"gamescope"});
    CHECK(g6.found && g6.renderW() == 0, "no options: found, sizes unknown");
    auto g7 = parseArgs({"gamescope", "-w"});
    CHECK(g7.found, "an option without its value does not crash");

    //find(): a process whose name is gamescope
    CHECK(!gamescope::find().found || gamescope::find().pid > 0, "find() works while nothing is running");
    pid_t child = fork();
    if (child == 0) { execl("/bin/bash", "bash", "-c", "exec -a gamescope sleep 20", (char*)nullptr); _exit(1); }
    usleep(300000);
    gamescope::Info f = gamescope::find();
    CHECK(f.found && f.pid == child, "find() sees a running process called gamescope");
    kill(child, SIGTERM); waitpid(child, nullptr, 0);

    //--- the picture on the overlay
    GameViewSettings st;
    gamescope::Info none;
    GameView v = ComputeGameView(2560, 1440, none, st);
    CHECK(v.renderW == 2560 && v.renderH == 1440 && near(v.dx, 0) && near(v.dw, 2560) && near(v.dh, 1440), "no gamescope: the picture is the whole overlay");

    v = ComputeGameView(2560, 1440, g, st);
    CHECK(v.renderW == 1440 && v.renderH == 1080 && near(v.dx, 0) && near(v.dy, 0) && near(v.dw, 2560) && near(v.dh, 1440), "gamescope -S stretch: 1440x1080 stretched over the whole 2560x1440 overlay");

    auto fit = parseArgs({"gamescope", "-w", "1440", "-h", "1080", "-W", "1920", "-H", "1080", "-S", "fit", "-f"});
    v = ComputeGameView(2560, 1440, fit, st);
    //4:3 picture inside 16:9: 1440x1080 fills the height, bars left and right: (1920-1440)/2 = 240 of 1920 -> 320 px of 2560
    CHECK(near(v.dx, 320) && near(v.dy, 0) && near(v.dw, 1920) && near(v.dh, 1440), "gamescope -S fit: keeps the shape, bars at the sides");
    auto fill = parseArgs({"gamescope", "-w", "1440", "-h", "1080", "-W", "1920", "-H", "1080", "-S", "fill"});
    v = ComputeGameView(2560, 1440, fill, st);
    CHECK(near(v.dw, 2560) && v.dh > 1440 && v.dy < 0, "gamescope -S fill: covers the screen, the top and bottom are cut off");
    auto integer = parseArgs({"gamescope", "-w", "640", "-h", "360", "-W", "1920", "-H", "1080", "-S", "integer"});
    v = ComputeGameView(1920, 1080, integer, st);
    CHECK(near(v.dw, 1920) && near(v.dh, 1080), "integer scaling that fits exactly");
    auto integer2 = parseArgs({"gamescope", "-w", "1000", "-h", "500", "-W", "1920", "-H", "1080", "-S", "integer"});
    v = ComputeGameView(1920, 1080, integer2, st);
    CHECK(near(v.dw, 1000) && near(v.dh, 500) && near(v.dx, 460) && near(v.dy, 290), "integer scaling that does not fit: 1x, centered");

    st.autoDetect = false; st.manualW = 1440; st.manualH = 1080; st.manualStretch = true;
    v = ComputeGameView(2560, 1440, none, st);
    CHECK(v.renderW == 1440 && near(v.dw, 2560) && near(v.dh, 1440), "set by hand: stretched");
    st.manualStretch = false;
    v = ComputeGameView(2560, 1440, none, st);
    CHECK(near(v.dx, 320) && near(v.dw, 1920), "set by hand: kept in shape");
    CHECK(ComputeGameView(0, 0, none, st).renderW == 0, "an overlay of size 0 gives an empty view, not a crash");

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
