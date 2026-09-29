#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Vector2
typedef struct { double x, y; } farm_Vector2;
static inline farm_Vector2 farm_Vector2_new(double x, double y) { return (farm_Vector2){x, y}; }
static inline farm_Vector2 farm_Vector2_zero() { return (farm_Vector2){0, 0}; }

// Vector3
typedef struct { double f_x, f_y, f_z; } farm_Vector3;
static inline farm_Vector3 farm_Vector3_new(double x, double y, double z) { return (farm_Vector3){x, y, z}; }
static inline farm_Vector3 farm_Vector3_zero() { return (farm_Vector3){0, 0, 0}; }

// Vector4
typedef struct { double f_x, f_y, f_z, w; } farm_Vector4;
static inline farm_Vector4 farm_Vector4_new(double x, double y, double z, double w) { return (farm_Vector4){x, y, z, w}; }
static inline farm_Vector4 farm_Vector4_zero() { return (farm_Vector4){0, 0, 0, 0}; }

// Matrix3 (column-major, 9 elements)
typedef struct { double elements[9]; } farm_Matrix3;
farm_Matrix3 farm_Matrix3_identity();

// Matrix4 (column-major, 16 elements)
typedef struct { double elements[16]; } farm_Matrix4;
farm_Matrix4 farm_Matrix4_identity();

// Quaternion
typedef struct { double f_x, f_y, f_z, w; } farm_Quaternion;
static inline farm_Quaternion farm_Quaternion_identity() { return (farm_Quaternion){0, 0, 0, 1}; }

// Color
typedef struct { double r, g, b; } farm_Color;
static inline farm_Color farm_Color_new(double r, double g, double b) { return (farm_Color){r, g, b}; }
static inline farm_Color farm_Color_zero() { return (farm_Color){0, 0, 0}; }

// Euler
typedef struct { double f_x, f_y, f_z; char order[4]; } farm_Euler;
static inline farm_Euler farm_Euler_new(double x, double y, double z, const char* order) {
  farm_Euler e = {x, y, z, {0}};
  for (int i = 0; i < 3 && order[i]; i++) e.order[i] = order[i];
  return e;
}

// Ray
typedef struct { farm_Vector3 origin, direction; } farm_Ray;
static inline farm_Ray farm_Ray_new(farm_Vector3 origin, farm_Vector3 direction) {
  return (farm_Ray){origin, direction};
}

// Box3
typedef struct { farm_Vector3 min, max; } farm_Box3;
farm_Box3 farm_Box3_empty();

// Sphere
typedef struct { farm_Vector3 center; double radius; } farm_Sphere;
static inline farm_Sphere farm_Sphere_empty() { return (farm_Sphere){{0,0,0}, -1}; }

// RayHit
typedef struct { bool hit; farm_Vector3 point; double distance; } farm_RayHit;
static inline farm_RayHit farm_RayHit_miss() { return (farm_RayHit){false, {0,0,0}, 0}; }

// Math functions
double farm_Vector3_length(farm_Vector3 v);
farm_Vector3 farm_Vector3_normalize(farm_Vector3 v);
farm_Vector3 farm_Vector3_add(farm_Vector3 a, farm_Vector3 b);
farm_Vector3 farm_Vector3_sub(farm_Vector3 a, farm_Vector3 b);
farm_Vector3 farm_Vector3_multiplyScalar(farm_Vector3 v, double s);
double farm_Vector3_dot(farm_Vector3 a, farm_Vector3 b);
farm_Vector3 farm_Vector3_cross(farm_Vector3 a, farm_Vector3 b);

// Vector4 functions
farm_Vector4 farm_Vector4_applyMatrix4(farm_Vector4 v, farm_Matrix4 m);

// Matrix4 functions
farm_Matrix4 farm_Matrix4_compose(farm_Vector3 position, farm_Quaternion quaternion, farm_Vector3 scale);
farm_Matrix4 farm_Matrix4_multiplyMatrices(farm_Matrix4 a, farm_Matrix4 b);
farm_Matrix4 farm_Matrix4_invert(farm_Matrix4 m);
farm_Matrix4 farm_Matrix4_makePerspective(double left, double right, double top, double bottom, double near, double far);

// Quaternion functions
farm_Quaternion farm_Quaternion_setFromEuler(farm_Euler e);
farm_Quaternion farm_Quaternion_setFromRotationMatrix(farm_Matrix4 m);

// Euler functions
farm_Euler farm_Euler_setFromQuaternion(farm_Quaternion q, const char* order);

// Color functions
farm_Color farm_Color_setHex(int32_t hex);
int32_t farm_Color_getHex(farm_Color c);
