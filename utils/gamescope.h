#pragma once
//When the game is started through gamescope (for example "gamescope -w 1440 -h 1080 -W 1920 -H 1080 -S stretch -f --
//%command%") the game draws a 1440x1080 picture and gamescope stretches it to the screen. The overlay has to know both
//sizes, so it reads them from the running gamescope's command line.
#ifndef VANITYARC_GAMESCOPE_H
#define VANITYARC_GAMESCOPE_H

#include <string>
#include <sys/types.h>
#include <vector>

namespace gamescope {

struct Info {
    bool found = false;
    int nestedW = 0, nestedH = 0;   //-w -h : the picture the game draws
    int outW = 0, outH = 0;         //-W -H : the picture gamescope shows
    std::string scaler = "auto";    //-S : auto fit fill stretch integer none
    bool fullscreen = false;        //-f
    pid_t pid = 0;

    //What the game itself renders at (0 when the command line did not say)
    int renderW() const { return nestedW > 0 ? nestedW : outW; }
    int renderH() const { return nestedH > 0 ? nestedH : outH; }
};

//argv[0] is the program name. Everything after "--" belongs to the game and is ignored.
Info parseArgs(const std::vector<std::string>& argv);

//The running gamescope with the highest pid, or Info{} (found = false) when there is none
Info find();

} // namespace gamescope

#endif //VANITYARC_GAMESCOPE_H
