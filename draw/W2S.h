#ifndef KWINHELPER_W2S_H
#define KWINHELPER_W2S_H
#include <algorithm>
#include <cmath>

#include "../utils/gameUtil.h"

//How the 3D view maps to the screen. The defaults are the original behaviour; the Calibration section of the
//Insert menu changes them if markers drift away from their targets.
struct Projection {
    int width = 2560, height = 1440;      //the game's own pixels
    double dx = 0, dy = 0, dw = 0, dh = 0; //where that picture shows on screen (dw/dh 0 = same size as the game's pixels, at 0,0)
    bool fovHorizontal = false; //which side of the screen the game's FOV number belongs to
    double scale = 1.0;         //multiplies the focal length
    double offX = 0, offY = 0;  //px, shifts the middle of the projection
    bool useRoll = true;        //take the camera's roll into account (it is 0 in normal play)
};

//The camera's directions and focal length, worked out once per frame instead of once per marker
struct View {
    Vector3 loc, fwd, right, up;
    double focal = 0;           //in the game's pixels
    double cx = 0, cy = 0;      //middle of the projection, in screen pixels
    double sx = 1, sy = 1;      //how much the game's picture is stretched on screen
    double rx = 0, ry = 0, rw = 0, rh = 0; //the picture's rectangle on screen
    bool ok = false;

    View() = default;
    View(const FminimalViewInfo& cam, const Projection& p) {
        const double Pi = 3.14159265358979;
        const double yaw = cam.Rotation.y * Pi / 180.0, pitch = cam.Rotation.x * Pi / 180.0;
        double roll = p.useRoll ? cam.Rotation.z : 0.0;
        if (!std::isfinite(roll) || std::abs(roll) > 360.0) roll = 0.0; //garbage is not a tilt
        roll *= Pi / 180.0;
        const double sinP = std::sin(pitch), cosP = std::cos(pitch), sinY = std::sin(yaw), cosY = std::cos(yaw);
        const double sinR = std::sin(roll), cosR = std::cos(roll);
        loc = cam.Location;
        //Unreal's rotation matrix (FRotationMatrix): with roll 0 this is exactly the forward / right / up of before
        fwd = Vector3(cosP * cosY, cosP * sinY, sinP);
        right = Vector3(sinR * sinP * cosY - cosR * sinY, sinR * sinP * sinY + cosR * cosY, -sinR * cosP);
        up = Vector3(-cosR * sinP * cosY - sinR * sinY, -cosR * sinP * sinY + sinR * cosY, cosR * cosP);

        const double tanHalf = std::tan((cam.FOV * Pi / 180.0) / 2.0);
        if (!(tanHalf > 1e-3) || !std::isfinite(tanHalf)) return;
        //One focal length for both axes (square pixels). Basis depends on which axis the engine's FOV refers to.
        focal = (p.fovHorizontal ? p.width / 2.0 : p.height / 2.0) / tanHalf * p.scale;
        rw = p.dw > 0 ? p.dw : p.width;
        rh = p.dh > 0 ? p.dh : p.height;
        rx = p.dx;
        ry = p.dy;
        sx = rw / p.width;
        sy = rh / p.height;
        cx = rx + rw / 2.0 + p.offX;
        cy = ry + rh / 2.0 + p.offY;
        ok = focal > 0 && p.width > 0 && p.height > 0;
    }

    //Camera space of a world point: x right, y up, z forward (centimetres)
    void toCamera(const Vector3& w, double& x, double& y, double& z) const {
        const Vector3 d = w - loc;
        x = d.Dot(right);
        y = d.Dot(up);
        z = d.Dot(fwd);
    }

    //False when the point is behind the camera (or the camera is unusable)
    bool project(const Vector3& w, Vector2& out) const {
        if (!ok) return false;
        double x, y, z;
        toCamera(w, x, y, z);
        if (z < 1.0) return false;
        out = Vector2(cx + (x / z) * focal * sx, cy - (y / z) * focal * sy);
        return true;
    }

    static constexpr double NEAR = 10.0; //cm: a line is cut where it passes this close in front of the camera

    //A line between two world points on screen, cut where it goes behind the camera (NEAR) and where it leaves the
    //picture plus margin px. So a limb right next to the camera is still drawn, and never streaks across the screen.
    //False when nothing of it is on screen.
    bool projectSegment(const Vector3& a, const Vector3& b, Vector2& pa, Vector2& pb, double margin = 60) const {
        if (!ok) return false;
        double ax, ay, az, bx, by, bz;
        toCamera(a, ax, ay, az);
        toCamera(b, bx, by, bz);
        if (!std::isfinite(ax + ay + az + bx + by + bz) || (az < NEAR && bz < NEAR)) return false;
        if (az < NEAR || bz < NEAR) { //one end behind: move it to where the line crosses NEAR
            const double f = (NEAR - az) / (bz - az);
            const double nx = ax + (bx - ax) * f, ny = ay + (by - ay) * f;
            if (az < NEAR) { ax = nx; ay = ny; az = NEAR; } else { bx = nx; by = ny; bz = NEAR; }
        }
        double x0 = cx + ax / az * focal * sx, y0 = cy - ay / az * focal * sy, x1 = cx + bx / bz * focal * sx, y1 = cy - by / bz * focal * sy;
        //Liang-Barsky against the picture plus margin
        const double dx = x1 - x0, dy = y1 - y0;
        const double p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - (rx - margin), rx + rw + margin - x0, y0 - (ry - margin), ry + rh + margin - y0};
        double t0 = 0, t1 = 1;
        for (int i = 0; i < 4; i++) {
            if (p[i] == 0) { if (q[i] < 0) return false; continue; }
            const double r = q[i] / p[i];
            if (p[i] < 0) t0 = std::max(t0, r); else t1 = std::min(t1, r);
        }
        if (t0 > t1) return false;
        pa = Vector2(x0 + dx * t0, y0 + dy * t0);
        pb = Vector2(x0 + dx * t1, y0 + dy * t1);
        return true;
    }

    //True when a screen point lies on the picture plus margin px
    bool onPicture(const Vector2& s, double margin = 60) const {
        return s.x >= rx - margin && s.x <= rx + rw + margin && s.y >= ry - margin && s.y <= ry + rh + margin;
    }
};

//Kept for older callers and tests: {-9999, -9999} when the point is behind the camera
inline Vector2 WorldToScreen(Vector3 WorldLocation, FminimalViewInfo CameraInfo,
                             int Width, int Height, bool FovIsHorizontal = false) {
    Projection p;
    p.width = Width;
    p.height = Height;
    p.fovHorizontal = FovIsHorizontal;
    Vector2 out;
    if (!View(CameraInfo, p).project(WorldLocation, out)) return Vector2(-9999.0, -9999.0);
    return out;
}

#endif //KWINHELPER_W2S_H
