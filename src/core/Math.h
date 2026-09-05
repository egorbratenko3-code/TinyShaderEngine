#pragma once
// Minimal header-only math lib so the project doesn't need GLM as a vendor
// dependency. Swap for GLM later if you want SIMD / more complete coverage.
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const { float l = length(); return l > 1e-6f ? Vec3{x/l,y/l,z/l} : Vec3{0,0,0}; }
    static float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
    }
};

// Column-major 4x4, m[col][row]
struct Mat4 {
    float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

    static Mat4 identity() { return Mat4(); }

    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity(); r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z; return r;
    }
    static Mat4 rotationX(float radians) {
        Mat4 r = identity(); float c = std::cos(radians), s = std::sin(radians);
        r.m[1][1] = c; r.m[1][2] = s; r.m[2][1] = -s; r.m[2][2] = c; return r;
    }
    static Mat4 rotationY(float radians) {
        Mat4 r = identity(); float c = std::cos(radians), s = std::sin(radians);
        r.m[0][0] = c; r.m[0][2] = -s; r.m[2][0] = s; r.m[2][2] = c; return r;
    }
    static Mat4 rotationZ(float radians) {
        Mat4 r = identity(); float c = std::cos(radians), s = std::sin(radians);
        r.m[0][0] = c; r.m[0][1] = s; r.m[1][0] = -s; r.m[1][1] = c; return r;
    }

    static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
        Mat4 r{}; for (int c=0;c<4;c++) for (int row=0;row<4;row++) r.m[c][row]=0;
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = zFar / (zNear - zFar);
        r.m[2][3] = -1.0f;
        r.m[3][2] = (zFar * zNear) / (zNear - zFar);
        return r;
    }

    // Vulkan's viewport coordinate system has Y pointing down. UI projection
    // still uses perspective() and performs its own screen-space flip, while
    // GPU passes use this variant to keep imported models upright.
    static Mat4 perspectiveVulkan(float fovYRadians, float aspect, float zNear, float zFar) {
        Mat4 r = perspective(fovYRadians, aspect, zNear, zFar);
        r.m[1][1] = -r.m[1][1];
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = Vec3::cross(f, up).normalized();
        Vec3 u = Vec3::cross(s, f);
        Mat4 r = identity();
        r.m[0][0]=s.x; r.m[1][0]=s.y; r.m[2][0]=s.z;
        r.m[0][1]=u.x; r.m[1][1]=u.y; r.m[2][1]=u.z;
        r.m[0][2]=-f.x; r.m[1][2]=-f.y; r.m[2][2]=-f.z;
        r.m[3][0] = -Vec3::dot(s, eye);
        r.m[3][1] = -Vec3::dot(u, eye);
        r.m[3][2] =  Vec3::dot(f, eye);
        return r;
    }

    static Mat4 mul(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                float sum = 0;
                for (int k = 0; k < 4; k++) sum += a.m[k][row] * b.m[c][k];
                r.m[c][row] = sum;
            }
        return r;
    }

    // Transforms a point, returns clip-space vec4 as {x,y,z,w}
    void transformPoint(const Vec3& p, float out[4]) const {
        out[0] = m[0][0]*p.x + m[1][0]*p.y + m[2][0]*p.z + m[3][0];
        out[1] = m[0][1]*p.x + m[1][1]*p.y + m[2][1]*p.z + m[3][1];
        out[2] = m[0][2]*p.x + m[1][2]*p.y + m[2][2]*p.z + m[3][2];
        out[3] = m[0][3]*p.x + m[1][3]*p.y + m[2][3]*p.z + m[3][3];
    }
};
