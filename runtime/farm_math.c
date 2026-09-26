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

// Vector4 applyMatrix4
farm_Vector4 farm_Vector4_applyMatrix4(farm_Vector4 v, farm_Matrix4 m) {
  double* e = m.elements;
  return (farm_Vector4){
    e[0]*v.x + e[4]*v.y + e[8]*v.z + e[12]*v.w,
    e[1]*v.x + e[5]*v.y + e[9]*v.z + e[13]*v.w,
    e[2]*v.x + e[6]*v.y + e[10]*v.z + e[14]*v.w,
    e[3]*v.x + e[7]*v.y + e[11]*v.z + e[15]*v.w
  };
}

// Matrix4 multiplyMatrices
farm_Matrix4 farm_Matrix4_multiplyMatrices(farm_Matrix4 a, farm_Matrix4 b) {
  farm_Matrix4 result;
  double* ae = a.elements;
  double* be = b.elements;
  double* re = result.elements;
  
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      re[i + j*4] = 0;
      for (int k = 0; k < 4; k++) {
        re[i + j*4] += ae[i + k*4] * be[k + j*4];
      }
    }
  }
  return result;
}

// Matrix4 compose
farm_Matrix4 farm_Matrix4_compose(farm_Vector3 position, farm_Quaternion quaternion, farm_Vector3 scale) {
  farm_Matrix4 m;
  double* e = m.elements;
  
  double x = quaternion.x, y = quaternion.y, z = quaternion.z, w = quaternion.w;
  double x2 = x + x, y2 = y + y, z2 = z + z;
  double xx = x * x2, xy = x * y2, xz = x * z2;
  double yy = y * y2, yz = y * z2, zz = z * z2;
  double wx = w * x2, wy = w * y2, wz = w * z2;
  
  double sx = scale.x, sy = scale.y, sz = scale.z;
  
  e[0] = (1 - (yy + zz)) * sx;
  e[1] = (xy + wz) * sx;
  e[2] = (xz - wy) * sx;
  e[3] = 0;
  
  e[4] = (xy - wz) * sy;
  e[5] = (1 - (xx + zz)) * sy;
  e[6] = (yz + wx) * sy;
  e[7] = 0;
  
  e[8] = (xz + wy) * sz;
  e[9] = (yz - wx) * sz;
  e[10] = (1 - (xx + yy)) * sz;
  e[11] = 0;
  
  e[12] = position.x;
  e[13] = position.y;
  e[14] = position.z;
  e[15] = 1;
  
  return m;
}

// Matrix4 invert
farm_Matrix4 farm_Matrix4_invert(farm_Matrix4 m) {
  farm_Matrix4 result;
  double* te = result.elements;
  double* me = m.elements;
  
  double n11 = me[0], n21 = me[1], n31 = me[2], n41 = me[3];
  double n12 = me[4], n22 = me[5], n32 = me[6], n42 = me[7];
  double n13 = me[8], n23 = me[9], n33 = me[10], n43 = me[11];
  double n14 = me[12], n24 = me[13], n34 = me[14], n44 = me[15];
  
  double t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
  double t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
  double t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
  double t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
  
  double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
  
  if (det == 0) {
    // Singular matrix - return identity
    return farm_Matrix4_identity();
  }
  
  double detInv = 1.0 / det;
  
  te[0] = t11 * detInv;
  te[1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * detInv;
  te[2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * detInv;
  te[3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * detInv;
  
  te[4] = t12 * detInv;
  te[5] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * detInv;
  te[6] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * detInv;
  te[7] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * detInv;
  
  te[8] = t13 * detInv;
  te[9] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * detInv;
  te[10] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * detInv;
  te[11] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * detInv;
  
  te[12] = t14 * detInv;
  te[13] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * detInv;
  te[14] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * detInv;
  te[15] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * detInv;
  
  return result;
}

// Matrix4 makePerspective
farm_Matrix4 farm_Matrix4_makePerspective(double left, double right, double top, double bottom, double near, double far) {
  farm_Matrix4 m;
  double* e = m.elements;
  
  double x = 2.0 * near / (right - left);
  double y = 2.0 * near / (top - bottom);
  
  double a = (right + left) / (right - left);
  double b = (top + bottom) / (top - bottom);
  double c = -(far + near) / (far - near);
  double d = -2.0 * far * near / (far - near);
  
  e[0] = x;   e[4] = 0;   e[8] = a;    e[12] = 0;
  e[1] = 0;   e[5] = y;   e[9] = b;    e[13] = 0;
  e[2] = 0;   e[6] = 0;   e[10] = c;   e[14] = d;
  e[3] = 0;   e[7] = 0;   e[11] = -1;  e[15] = 0;
  
  return m;
}

// Quaternion setFromEuler
farm_Quaternion farm_Quaternion_setFromEuler(farm_Euler e) {
  double x = e.x, y = e.y, z = e.z;
  double c1 = cos(x / 2.0);
  double c2 = cos(y / 2.0);
  double c3 = cos(z / 2.0);
  double s1 = sin(x / 2.0);
  double s2 = sin(y / 2.0);
  double s3 = sin(z / 2.0);
  
  farm_Quaternion q;
  
  // Assuming "XYZ" order (most common)
  if (strcmp(e.order, "XYZ") == 0 || e.order[0] == 0) {
    q.x = s1 * c2 * c3 + c1 * s2 * s3;
    q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3;
    q.w = c1 * c2 * c3 - s1 * s2 * s3;
  } else if (strcmp(e.order, "YXZ") == 0) {
    q.x = s1 * c2 * c3 + c1 * s2 * s3;
    q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3;
    q.w = c1 * c2 * c3 + s1 * s2 * s3;
  } else if (strcmp(e.order, "ZXY") == 0) {
    q.x = s1 * c2 * c3 - c1 * s2 * s3;
    q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3;
    q.w = c1 * c2 * c3 - s1 * s2 * s3;
  } else if (strcmp(e.order, "ZYX") == 0) {
    q.x = s1 * c2 * c3 - c1 * s2 * s3;
    q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3;
    q.w = c1 * c2 * c3 + s1 * s2 * s3;
  } else if (strcmp(e.order, "YZX") == 0) {
    q.x = s1 * c2 * c3 + c1 * s2 * s3;
    q.y = c1 * s2 * c3 + s1 * c2 * s3;
    q.z = c1 * c2 * s3 - s1 * s2 * c3;
    q.w = c1 * c2 * c3 - s1 * s2 * s3;
  } else if (strcmp(e.order, "XZY") == 0) {
    q.x = s1 * c2 * c3 - c1 * s2 * s3;
    q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3;
    q.w = c1 * c2 * c3 + s1 * s2 * s3;
  } else {
    // Default to XYZ
    q.x = s1 * c2 * c3 + c1 * s2 * s3;
    q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3;
    q.w = c1 * c2 * c3 - s1 * s2 * s3;
  }
  
  return q;
}

// Quaternion setFromRotationMatrix
farm_Quaternion farm_Quaternion_setFromRotationMatrix(farm_Matrix4 m) {
  double* e = m.elements;
  
  double m11 = e[0], m12 = e[4], m13 = e[8];
  double m21 = e[1], m22 = e[5], m23 = e[9];
  double m31 = e[2], m32 = e[6], m33 = e[10];
  
  double trace = m11 + m22 + m33;
  farm_Quaternion q;
  
  if (trace > 0) {
    double s = 0.5 / sqrt(trace + 1.0);
    q.w = 0.25 / s;
    q.x = (m32 - m23) * s;
    q.y = (m13 - m31) * s;
    q.z = (m21 - m12) * s;
  } else if (m11 > m22 && m11 > m33) {
    double s = 2.0 * sqrt(1.0 + m11 - m22 - m33);
    q.w = (m32 - m23) / s;
    q.x = 0.25 * s;
    q.y = (m12 + m21) / s;
    q.z = (m13 + m31) / s;
  } else if (m22 > m33) {
    double s = 2.0 * sqrt(1.0 + m22 - m11 - m33);
    q.w = (m13 - m31) / s;
    q.x = (m12 + m21) / s;
    q.y = 0.25 * s;
    q.z = (m23 + m32) / s;
  } else {
    double s = 2.0 * sqrt(1.0 + m33 - m11 - m22);
    q.w = (m21 - m12) / s;
    q.x = (m13 + m31) / s;
    q.y = (m23 + m32) / s;
    q.z = 0.25 * s;
  }
  
  return q;
}

// Euler setFromQuaternion
farm_Euler farm_Euler_setFromQuaternion(farm_Quaternion q, const char* order) {
  farm_Euler e;
  strncpy(e.order, order, 3);
  e.order[3] = '\0';
  
  double x = q.x, y = q.y, z = q.z, w = q.w;
  double x2 = x * x, y2 = y * y, z2 = z * z, w2 = w * w;
  
  // Build rotation matrix elements we need
  double m11 = 1 - 2*y2 - 2*z2;
  double m12 = 2*x*y + 2*w*z;
  double m13 = 2*x*z - 2*w*y;
  double m21 = 2*x*y - 2*w*z;
  double m22 = 1 - 2*x2 - 2*z2;
  double m23 = 2*y*z + 2*w*x;
  double m31 = 2*x*z + 2*w*y;
  double m32 = 2*y*z - 2*w*x;
  double m33 = 1 - 2*x2 - 2*y2;
  
  // Extract Euler angles (XYZ order as default)
  if (strcmp(order, "XYZ") == 0 || order[0] == 0) {
    e.y = asin(fmax(-1.0, fmin(1.0, m13)));
    if (fabs(m13) < 0.9999999) {
      e.x = atan2(-m23, m33);
      e.z = atan2(-m12, m11);
    } else {
      e.x = atan2(m32, m22);
      e.z = 0;
    }
  } else {
    // Simplified - only support XYZ for now
    e.x = 0; e.y = 0; e.z = 0;
  }
  
  return e;
}

// Color setHex
farm_Color farm_Color_setHex(int32_t hex) {
  farm_Color c;
  c.r = ((hex >> 16) & 0xFF) / 255.0;
  c.g = ((hex >> 8) & 0xFF) / 255.0;
  c.b = (hex & 0xFF) / 255.0;
  return c;
}
