#include "farm_math.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Matrix3 identity
farm_Matrix3 farm_Matrix3_identity() {
  return (farm_Matrix3){{1,0,0, 0,1,0, 0,0,1}};
}

// Matrix4 identity  
farm_Matrix4 farm_Matrix4_identity() {
  return (farm_Matrix4){{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

// Box3 empty
farm_Box3 farm_Box3_empty() {
  const double inf = INFINITY;
  return (farm_Box3){{inf,inf,inf}, {-inf,-inf,-inf}};
}

// Vector3 length
double farm_Vector3_length(farm_Vector3 v) {
  return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

// Vector3 normalize
farm_Vector3 farm_Vector3_normalize(farm_Vector3 v) {
  double len = farm_Vector3_length(v);
  if (len == 0.0) return v;  // zero unchanged per spec
  return (farm_Vector3){v.x/len, v.y/len, v.z/len};
}

// Vector3 add
farm_Vector3 farm_Vector3_add(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){a.x+b.x, a.y+b.y, a.z+b.z};
}

// Vector3 sub
farm_Vector3 farm_Vector3_sub(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){a.x-b.x, a.y-b.y, a.z-b.z};
}

// Vector3 multiply scalar
farm_Vector3 farm_Vector3_multiplyScalar(farm_Vector3 v, double s) {
  return (farm_Vector3){v.x*s, v.y*s, v.z*s};
}

// Vector3 dot
double farm_Vector3_dot(farm_Vector3 a, farm_Vector3 b) {
  return a.x*b.x + a.y*b.y + a.z*b.z;
}

// Vector3 cross
farm_Vector3 farm_Vector3_cross(farm_Vector3 a, farm_Vector3 b) {
  return (farm_Vector3){
    a.y*b.z - a.z*b.y,
    a.z*b.x - a.x*b.z,
    a.x*b.y - a.y*b.x
  };
}
