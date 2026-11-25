/** Contains game struct info
 * Used by all files (memory, gameState, ESP)
 * Should remain constant between updates
 */

#ifndef KWINHELPER_GAMEUTIL_H
#define KWINHELPER_GAMEUTIL_H
#include <cmath>
#include <iostream>
#include <iomanip>

struct Vector3 {
    double x, y, z;

    // Constructor
    Vector3(double _x = 0.0, double _y = 0.0, double _z = 0.0)
        : x(_x), y(_y), z(_z) {}

    // Dot product
    double Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // Subtraction operator
    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    double Dist(const Vector3& other) const {
        return std::sqrt(std::pow(x - other.x, 2) +
                         std::pow(y - other.y, 2) +
                         std::pow(z - other.z, 2));
    }

    bool operator==(const Vector3& other) const {
        return (x == other.x && y == other.y && z == other.z);
    }

    void Print() {
        std::cout << x << " " << y << " " << z << std::endl;
    }
};

struct FminimalViewInfo {
    Vector3 Location{};
    double pad1, pad2 = 0;
    Vector3 Rotation{};   // pitch, yaw, roll in DEGREES
    double pad3, pad4 = 0;
    float FOV;

    void Print() const {
        std::cout << "FminimalViewInfo:" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Location: (" << Location.x << ", " << Location.y << ", " << Location.z << ")" << std::endl;
        std::cout << "  Rotation: (P: " << Rotation.x << ", Y: " << Rotation.y << ", R: " << Rotation.z << ")" << std::endl;
        std::cout << "  FOV:      " << FOV << std::endl;
    }
};

//A view that can be drawn with: finite numbers and a FOV a camera could have. Wide on purpose (a scope zooms far in, a
//FOV setting can go far out); the narrower 10..130 is only used to pick the camera in the first place (GameState::scan)
inline bool viewUsable(const FminimalViewInfo& v) {
    return v.FOV > 1 && v.FOV < 170 && //NaN fails this too
           std::isfinite(v.Location.x) && std::isfinite(v.Location.y) && std::isfinite(v.Location.z) &&
           std::isfinite(v.Rotation.x) && std::isfinite(v.Rotation.y) && std::isfinite(v.Rotation.z);
}

struct Vector2 {
    double x, y;

    // Constructor
    Vector2(double _x = 0.0, double _y = 0.0)
        : x(_x), y(_y) {}
};

#endif //KWINHELPER_GAMEUTIL_H