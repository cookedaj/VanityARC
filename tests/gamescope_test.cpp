//gamescope command-line reading, and where the game's picture ends up on the overlay.
#include <sys/wait.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    CHECK(g3.nestedW == 1440 && g3.nestedH == 1080 && g3.outW == 1280 && g3.outH == 720, "-w1440 style, and an unset output is gamescope's 1280x720");
    auto g4 = parseArgs({"gamescope", "-W", "1920", "-H", "1080", "--", "-w", "640", "-h", "480"});
    CHECK(g4.renderW() == 1920 && g4.renderH() == 1080 && g4.nestedW == 1920, "the game's own arguments after -- are ignored");
    auto g5 = parseArgs({"gamescope", "-w", "abc", "-h", "-5", "-W", "99999"});
    CHECK(g5.found && g5.renderW() == 1280 && g5.renderH() == 720, "nonsense sizes are ignored, not trusted: gamescope's defaults");
    auto g6 = parseArgs({"gamescope"});
    CHECK(g6.found && g6.renderW() == 1280 && g6.renderH() == 720 && g6.outW == 1280 && g6.outH == 720, "no options: gamescope's default 1280x720");
    auto g7 = parseArgs({"gamescope", "-w"});
    CHECK(g7.found, "an option without its value does not crash");
    auto g8 = parseArgs({"gamescope", "-h", "1080", "-W", "2560", "-H", "1440"});
    CHECK(g8.nestedW == 1920 && g8.nestedH == 1080 && g8.outW == 2560 && g8.outH == 1440, "-h alone: a 16:9 game width, not -W");
    auto g9 = parseArgs({"gamescope", "-H", "1440"});
    CHECK(g9.outW == 2560 && g9.outH == 1440 && g9.renderW() == 2560 && g9.renderH() == 1440, "-H alone: a 16:9 output width, the game follows it");
    CHECK(parseArgs({"gamescope", "-S", "fit", "-i"}).scaler == "integer" && parseArgs({"gamescope", "--integer-scale"}).scaler == "integer", "-i / --integer-scale mean integer scaling");

    //find(): the gamescope the game runs in. The "gamescope" is a process of that name; the "game" is its child,
    //started with the variable gamescope sets for what it runs.
    const bool testInGamescope = std::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr;
    CHECK(!gamescope::find(getpid()).found || testInGamescope, "find() works while nothing is running");
    int fd[2] = {-1, -1};
    CHECK(pipe(fd) == 0, "pipe for the game's pid");
    pid_t child = fork();
    if (child == 0) {
        setenv("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0", 1);
        pid_t game = fork();
        if (game == 0) { execlp("sleep", "sleep", "20", (char*)nullptr); _exit(1); }
        if (write(fd[1], &game, sizeof game) != (ssize_t)sizeof game) _exit(1);
        execl("/bin/bash", "bash", "-c", "exec -a gamescope sleep 20", (char*)nullptr); _exit(1);
    }
    pid_t game = 0;
    if (read(fd[0], &game, sizeof game) != (ssize_t)sizeof game) game = 0;
    usleep(300000);
    gamescope::Info f = gamescope::find(game);
    CHECK(game > 0 && f.found && f.pid == child, "find() takes the gamescope the game runs in");
    if (!testInGamescope) CHECK(!gamescope::find(getpid()).found, "a gamescope the game does not run in is ignored");
    if (game > 0) kill(game, SIGTERM);
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
    //4:3 picture inside the 16:9 screen (fullscreen: the output is the screen): 1440x1080 fills the height -> 1920 wide, bars (2560-1920)/2 = 320
    CHECK(near(v.dx, 320) && near(v.dy, 0) && near(v.dw, 1920) && near(v.dh, 1440), "gamescope -S fit: keeps the shape, bars at the sides");
    //fullscreen, no -W -H: the output is the screen, not the 1280x720 default and not the game's 4:3 size
    v = ComputeGameView(2560, 1440, parseArgs({"gamescope", "-w", "1440", "-h", "1080", "-f"}), st);
    CHECK(near(v.dx, 320) && near(v.dy, 0) && near(v.dw, 1920) && near(v.dh, 1440), "fullscreen auto: 4:3 kept in shape on the screen, not stretched");
    v = ComputeGameView(2560, 1600, parseArgs({"gamescope", "-f"}), st);
    CHECK(v.renderW == 1280 && v.renderH == 720 && near(v.dx, 0) && near(v.dy, 80) && near(v.dw, 2560) && near(v.dh, 1440), "fullscreen with no sizes: 1280x720 fitted to a 16:10 screen, bars top and bottom");
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
