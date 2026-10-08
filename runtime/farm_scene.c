#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "farm_scene.h"
#include "farm_math.h"
#include "farm_rt.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: clamp double to [min, max]
static inline double clamp(double v, double min, double max) {
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

// Helper: check for cycles in hierarchy
static bool is_ancestor_of(farm_Object3D* potential_ancestor, farm_Object3D* node) {
  farm_Object3D* cur = node->parent;
  while (cur) {
    if (cur == potential_ancestor) return true;
    cur = cur->parent;
  }
  return false;
}

// Helper: initialize Object3D fields (used by all subclass constructors)
static void init_Object3D_fields(farm_Object3D* obj, farm_Object3DType type) {
  obj->type = type;
  obj->f_position = farm_Vector3_zero();
  obj->f_rotation = farm_Euler_new(0, 0, 0, "XYZ");
  obj->f_quaternion = farm_Quaternion_identity();
  obj->f_scale = farm_Vector3_new(1, 1, 1);
  obj->f_matrix = farm_Matrix4_identity();
  obj->f_matrixWorld = farm_Matrix4_identity();
  obj->matrixAutoUpdate = true;
  obj->f_visible = true;
  obj->parent = NULL;
  obj->children.items = NULL;
  obj->children.count = 0;
  obj->children.capacity = 0;
  obj->rotation_dirty = false;
  obj->quaternion_dirty = false;
}

// Helper: sync rotation/quaternion (exposed for generated code)
void sync_quaternion_from_rotation(farm_Object3D* self);
void sync_rotation_from_quaternion(farm_Object3D* self);

void sync_quaternion_from_rotation(farm_Object3D* self) {
  if (self->rotation_dirty) {
    self->f_quaternion = farm_Quaternion_setFromEuler(self->f_rotation);
    self->rotation_dirty = false;
    self->quaternion_dirty = false;
  }
}

void sync_rotation_from_quaternion(farm_Object3D* self) {
  if (self->quaternion_dirty) {
    self->f_rotation = farm_Euler_setFromQuaternion(self->f_quaternion, self->f_rotation.order);
    self->quaternion_dirty = false;
    self->rotation_dirty = false;
  }
}

// Object3D implementation
farm_Object3D* farm_Object3D_new() {
  farm_Object3D* obj = (farm_Object3D*)farm_arena_alloc(sizeof(farm_Object3D));
  init_Object3D_fields(obj, FARM_OBJECT3D_TYPE_OBJECT3D);
  return obj;
}

farm_Object3D* farm_Object3D_add(farm_Object3D* self, farm_Object3D* child) {
  if (child == self || is_ancestor_of(child, self)) {
    farm_trap(106, "Object3D hierarchy cycle");
  }
  
  // Remove from current parent
  if (child->parent) {
    farm_Object3D_remove(child->parent, child);
  }
  
  // Add to this parent
  if (self->children.count >= self->children.capacity) {
    int32_t new_cap = self->children.capacity == 0 ? 4 : self->children.capacity * 2;
    farm_Object3D** new_items = (farm_Object3D**)farm_arena_alloc(new_cap * sizeof(farm_Object3D*));
    if (self->children.items) {
      memcpy(new_items, self->children.items, self->children.count * sizeof(farm_Object3D*));
    }
    self->children.items = new_items;
    self->children.capacity = new_cap;
  }
  
  self->children.items[self->children.count++] = child;
  child->parent = self;
  return self;
}

farm_Object3D* farm_Object3D_remove(farm_Object3D* self, farm_Object3D* child) {
  for (int32_t i = 0; i < self->children.count; i++) {
    if (self->children.items[i] == child) {
      // Remove by shifting
      for (int32_t j = i; j < self->children.count - 1; j++) {
        self->children.items[j] = self->children.items[j + 1];
      }
      self->children.count--;
      child->parent = NULL;
      break;
    }
  }
  return self;
}

farm_Object3D* farm_Object3D_addAt(farm_Object3D* self, farm_Object3D* child, int64_t index) {
  if (index < 0 || index > self->children.count) {
    farm_trap(102, "index out of bounds");
  }
  
  if (child == self || is_ancestor_of(child, self)) {
    farm_trap(106, "Object3D hierarchy cycle");
  }
  
  // Remove from current parent
  if (child->parent) {
    farm_Object3D_remove(child->parent, child);
  }
  
  // Ensure capacity
  if (self->children.count >= self->children.capacity) {
    int32_t new_cap = self->children.capacity == 0 ? 4 : self->children.capacity * 2;
    farm_Object3D** new_items = (farm_Object3D**)farm_arena_alloc(new_cap * sizeof(farm_Object3D*));
    if (self->children.items) {
      memcpy(new_items, self->children.items, self->children.count * sizeof(farm_Object3D*));
    }
    self->children.items = new_items;
    self->children.capacity = new_cap;
  }
  
  // Shift elements
  for (int32_t i = self->children.count; i > index; i--) {
    self->children.items[i] = self->children.items[i - 1];
  }
  
  self->children.items[index] = child;
  self->children.count++;
  child->parent = self;
  return self;
}

int64_t farm_Object3D_childCount(farm_Object3D* self) {
  return self->children.count;
}

farm_Object3D* farm_Object3D_getChild(farm_Object3D* self, int64_t index) {
  if (index < 0 || index >= self->children.count) {
    farm_trap(102, "index out of bounds");
  }
  return self->children.items[index];
}

void farm_Object3D_updateMatrix(farm_Object3D* self) {
  sync_quaternion_from_rotation(self);
  sync_rotation_from_quaternion(self);
  self->f_matrix = farm_Matrix4_compose(self->f_position, self->f_quaternion, self->f_scale);
}

void farm_Object3D_updateMatrixWorld(farm_Object3D* self, bool force) {
  if (self->matrixAutoUpdate) {
    farm_Object3D_updateMatrix(self);
  }
  
  if (self->parent) {
    self->f_matrixWorld = farm_Matrix4_multiplyMatrices(self->parent->f_matrixWorld, self->f_matrix);
  } else {
    self->f_matrixWorld = self->f_matrix;
  }
  
  for (int32_t i = 0; i < self->children.count; i++) {
    farm_Object3D_updateMatrixWorld(self->children.items[i], force);
  }
}

void farm_Object3D_lookAt_xyz(farm_Object3D* self, double x, double y, double z) {
  farm_Vector3 target = farm_Vector3_new(x, y, z);
  farm_Vector3 up = farm_Vector3_new(0, 1, 0);
  // M4 §6.1: z = normalize(eye - target); local +Z. Zero → (0,0,1).
  farm_Vector3 z_axis = farm_Vector3_sub(self->f_position, target);
  double zlen = farm_Vector3_length(z_axis);
  if (zlen == 0.0) z_axis = farm_Vector3_new(0, 0, 1);
  else z_axis = farm_Vector3_normalize(z_axis);
  farm_Vector3 x_axis = farm_Vector3_cross(up, z_axis);
  if (farm_Vector3_length(x_axis) == 0.0) x_axis = farm_Vector3_new(1, 0, 0);
  else x_axis = farm_Vector3_normalize(x_axis);
  farm_Vector3 y_axis = farm_Vector3_cross(z_axis, x_axis);
  
  farm_Matrix4 rot_mat = farm_Matrix4_identity();
  rot_mat.elements[0] = x_axis.f_x;
  rot_mat.elements[1] = x_axis.f_y;
  rot_mat.elements[2] = x_axis.f_z;
  rot_mat.elements[4] = y_axis.f_x;
  rot_mat.elements[5] = y_axis.f_y;
  rot_mat.elements[6] = y_axis.f_z;
  rot_mat.elements[8] = z_axis.f_x;
  rot_mat.elements[9] = z_axis.f_y;
  rot_mat.elements[10] = z_axis.f_z;
  
  self->f_quaternion = farm_Quaternion_setFromRotationMatrix(rot_mat);
  self->quaternion_dirty = true;
  sync_rotation_from_quaternion(self);
}

void farm_Object3D_lookAt_v(farm_Object3D* self, farm_Vector3 target) {
  farm_Object3D_lookAt_xyz(self, target.f_x, target.f_y, target.f_z);
}

void farm_Object3D_setRotationFromEuler(farm_Object3D* self, farm_Euler e) {
  self->f_rotation = e;
  self->rotation_dirty = true;
  sync_quaternion_from_rotation(self);
}

void farm_Object3D_setRotationFromQuaternion(farm_Object3D* self, farm_Quaternion q) {
  self->f_quaternion = q;
  self->quaternion_dirty = true;
  sync_rotation_from_quaternion(self);
}

static int euler_order_ok(const char* o) {
  return strcmp(o, "XYZ") == 0 || strcmp(o, "YZX") == 0 || strcmp(o, "ZXY") == 0 ||
         strcmp(o, "XZY") == 0 || strcmp(o, "YXZ") == 0 || strcmp(o, "ZYX") == 0;
}

void farm_euler_set_order(farm_Euler* e, FarmString s) {
  if (!e || !s.ptr || s.len != 3) farm_trap(104, "invalid Euler order");
  char buf[4] = {s.ptr[0], s.ptr[1], s.ptr[2], 0};
  if (!euler_order_ok(buf)) farm_trap(104, "invalid Euler order");
  e->order[0] = buf[0];
  e->order[1] = buf[1];
  e->order[2] = buf[2];
  e->order[3] = 0;
}

FarmString farm_euler_order_string(const farm_Euler* e) {
  if (!e) return (FarmString){"XYZ", 3};
  if (strcmp(e->order, "YZX") == 0) return (FarmString){"YZX", 3};
  if (strcmp(e->order, "ZXY") == 0) return (FarmString){"ZXY", 3};
  if (strcmp(e->order, "XZY") == 0) return (FarmString){"XZY", 3};
  if (strcmp(e->order, "YXZ") == 0) return (FarmString){"YXZ", 3};
  if (strcmp(e->order, "ZYX") == 0) return (FarmString){"ZYX", 3};
  return (FarmString){"XYZ", 3};
}

// Scene implementation
farm_Scene* farm_Scene_new() {
  farm_Scene* scene = (farm_Scene*)farm_arena_alloc(sizeof(farm_Scene));
  init_Object3D_fields((farm_Object3D*)scene, FARM_OBJECT3D_TYPE_SCENE);
  scene->hasBackground = false;
  scene->background = farm_Color_zero();
  return scene;
}

void farm_Scene_setBackground(farm_Scene* self, farm_Color color) {
  self->background = color;
  self->hasBackground = true;
}

void farm_Scene_clearBackground(farm_Scene* self) {
  self->hasBackground = false;
}

// PerspectiveCamera implementation
farm_PerspectiveCamera* farm_PerspectiveCamera_new(double fov, double aspect, double near, double far) {
  farm_PerspectiveCamera* cam = (farm_PerspectiveCamera*)farm_arena_alloc(sizeof(farm_PerspectiveCamera));
  init_Object3D_fields((farm_Object3D*)cam, FARM_OBJECT3D_TYPE_CAMERA);
  cam->f_fov = fov;
  cam->f_aspect = aspect;
  cam->f_near = near;
  cam->f_far = far;
  cam->matrixWorldInverse = farm_Matrix4_identity();
  cam->projectionMatrix = farm_Matrix4_identity();
  farm_PerspectiveCamera_updateProjectionMatrix(cam);
  return cam;
}

void farm_PerspectiveCamera_updateProjectionMatrix(farm_PerspectiveCamera* self) {
  double fov_rad = (M_PI / 180.0) * self->f_fov;
  double top = self->f_near * tan(0.5 * fov_rad);
  double height = 2.0 * top;
  double width = self->f_aspect * height;
  double left = -0.5 * width;
  double right = left + width;
  double bottom = -top;
  
  self->projectionMatrix = farm_Matrix4_makePerspective(left, right, top, bottom, self->f_near, self->f_far);
}

void farm_PerspectiveCamera_lookAt_xyz(farm_PerspectiveCamera* self, double x, double y, double z) {
  farm_Object3D_lookAt_xyz((farm_Object3D*)self, x, y, z);
}

void farm_PerspectiveCamera_lookAt_v(farm_PerspectiveCamera* self, farm_Vector3 target) {
  farm_Object3D_lookAt_v((farm_Object3D*)self, target);
}

void farm_PerspectiveCamera_updateMatrixWorld(farm_PerspectiveCamera* self, bool force) {
  farm_Object3D_updateMatrixWorld((farm_Object3D*)self, force);
  self->matrixWorldInverse = farm_Matrix4_invert(self->f_matrixWorld);
}

// Geometry builders
static void build_box_geometry(farm_GeometryData* data, double width, double height, double depth) {
  double w = width / 2.0, h = height / 2.0, d = depth / 2.0;
  
  // 24 vertices (4 per face, unique normals)
  data->vertex_count = 24;
  data->positions = (double*)farm_arena_alloc(24 * 3 * sizeof(double));
  data->normals = (double*)farm_arena_alloc(24 * 3 * sizeof(double));
  
  double positions[] = {
    // Front face (+Z)
    -w, -h,  d,   w, -h,  d,   w,  h,  d,  -w,  h,  d,
    // Back face (-Z)
     w, -h, -d,  -w, -h, -d,  -w,  h, -d,   w,  h, -d,
    // Right face (+X)
     w, -h,  d,   w, -h, -d,   w,  h, -d,   w,  h,  d,
    // Left face (-X)
    -w, -h, -d,  -w, -h,  d,  -w,  h,  d,  -w,  h, -d,
    // Top face (+Y)
    -w,  h,  d,   w,  h,  d,   w,  h, -d,  -w,  h, -d,
    // Bottom face (-Y)
    -w, -h, -d,   w, -h, -d,   w, -h,  d,  -w, -h,  d
  };
  
  memcpy(data->positions, positions, 24 * 3 * sizeof(double));
  
  // Normals (each face has same normal for all 4 vertices)
  double normals[] = {
    0,0,1, 0,0,1, 0,0,1, 0,0,1,     // Front
    0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1, // Back
    1,0,0, 1,0,0, 1,0,0, 1,0,0,     // Right
    -1,0,0, -1,0,0, -1,0,0, -1,0,0, // Left
    0,1,0, 0,1,0, 0,1,0, 0,1,0,     // Top
    0,-1,0, 0,-1,0, 0,-1,0, 0,-1,0  // Bottom
  };
  
  memcpy(data->normals, normals, 24 * 3 * sizeof(double));

  data->uvs = (double*)farm_arena_alloc(24 * 2 * sizeof(double));
  {
    double uvs[] = {
      0,0, 1,0, 1,1, 0,1,
      0,0, 1,0, 1,1, 0,1,
      0,0, 1,0, 1,1, 0,1,
      0,0, 1,0, 1,1, 0,1,
      0,0, 1,0, 1,1, 0,1,
      0,0, 1,0, 1,1, 0,1
    };
    memcpy(data->uvs, uvs, 24 * 2 * sizeof(double));
  }
  
  // 12 triangles (2 per face)
  data->index_count = 36;
  data->indices = (int32_t*)farm_arena_alloc(36 * sizeof(int32_t));
  
  int32_t indices[] = {
    0,1,2, 0,2,3,       // Front
    4,5,6, 4,6,7,       // Back
    8,9,10, 8,10,11,    // Right
    12,13,14, 12,14,15, // Left
    16,17,18, 16,18,19, // Top
    20,21,22, 20,22,23  // Bottom
  };
  
  memcpy(data->indices, indices, 36 * sizeof(int32_t));
  data->disposed = false;
}

static void build_plane_geometry(farm_GeometryData* data, double width, double height) {
  double w = width / 2.0, h = height / 2.0;
  
  // 4 vertices, facing +Z
  data->vertex_count = 4;
  data->positions = (double*)farm_arena_alloc(4 * 3 * sizeof(double));
  data->normals = (double*)farm_arena_alloc(4 * 3 * sizeof(double));
  
  double positions[] = {
    -w, -h, 0,
     w, -h, 0,
     w,  h, 0,
    -w,  h, 0
  };
  memcpy(data->positions, positions, 4 * 3 * sizeof(double));
  
  double normals[] = {
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    0, 0, 1
  };
  memcpy(data->normals, normals, 4 * 3 * sizeof(double));

  data->uvs = (double*)farm_arena_alloc(4 * 2 * sizeof(double));
  {
    double uvs[] = {0,0, 1,0, 1,1, 0,1};
    memcpy(data->uvs, uvs, 4 * 2 * sizeof(double));
  }
  
  // 2 triangles, CCW from +Z
  data->index_count = 6;
  data->indices = (int32_t*)farm_arena_alloc(6 * sizeof(int32_t));
  int32_t indices[] = {0, 1, 2, 0, 2, 3};
  memcpy(data->indices, indices, 6 * sizeof(int32_t));
  data->disposed = false;
}

static void build_sphere_geometry(farm_GeometryData* data, double radius, int32_t widthSeg, int32_t heightSeg) {
  int32_t ws = widthSeg < 3 ? 3 : widthSeg;
  int32_t hs = heightSeg < 2 ? 2 : heightSeg;
  
  int32_t vc = (ws + 1) * (hs + 1);
  data->vertex_count = vc;
  data->positions = (double*)farm_arena_alloc(vc * 3 * sizeof(double));
  data->normals = (double*)farm_arena_alloc(vc * 3 * sizeof(double));
  data->uvs = (double*)farm_arena_alloc(vc * 2 * sizeof(double));
  
  // Generate vertices (M4 §7.2 / Three.js-style)
  int32_t idx = 0;
  for (int32_t iy = 0; iy <= hs; iy++) {
    double v = (double)iy / (double)hs;
    double theta = v * M_PI;
    
    for (int32_t ix = 0; ix <= ws; ix++) {
      double u = (double)ix / (double)ws;
      double phi = u * 2.0 * M_PI;
      
      double x = -radius * cos(phi) * sin(theta);
      double y = radius * cos(theta);
      double z = radius * sin(phi) * sin(theta);
      
      data->positions[idx * 3 + 0] = x;
      data->positions[idx * 3 + 1] = y;
      data->positions[idx * 3 + 2] = z;
      
      double len = sqrt(x*x + y*y + z*z);
      data->normals[idx * 3 + 0] = (len == 0.0) ? 0.0 : x / len;
      data->normals[idx * 3 + 1] = (len == 0.0) ? 0.0 : y / len;
      data->normals[idx * 3 + 2] = (len == 0.0) ? 0.0 : z / len;
      data->uvs[idx * 2 + 0] = u;
      data->uvs[idx * 2 + 1] = 1.0 - v;
      
      idx++;
    }
  }
  
  // Generate indices (M4 §7.2): (a, b, a+1) and (b, b+1, a+1)
  int32_t tri_count = ws * hs * 2;
  data->index_count = tri_count * 3;
  data->indices = (int32_t*)farm_arena_alloc(data->index_count * sizeof(int32_t));
  
  idx = 0;
  for (int32_t iy = 0; iy < hs; iy++) {
    for (int32_t ix = 0; ix < ws; ix++) {
      int32_t a = iy * (ws + 1) + ix;
      int32_t b = a + ws + 1;
      data->indices[idx++] = a;
      data->indices[idx++] = b;
      data->indices[idx++] = a + 1;
      data->indices[idx++] = b;
      data->indices[idx++] = b + 1;
      data->indices[idx++] = a + 1;
    }
  }
  
  data->disposed = false;
}

// BoxGeometry
farm_BoxGeometry* farm_BoxGeometry_new() {
  return farm_BoxGeometry_new_whd(1, 1, 1);
}

farm_BoxGeometry* farm_BoxGeometry_new_whd(double width, double height, double depth) {
  farm_BoxGeometry* geom = (farm_BoxGeometry*)farm_arena_alloc(sizeof(farm_BoxGeometry));
  build_box_geometry(&geom->data, width, height, depth);
  return geom;
}

void farm_BoxGeometry_dispose(farm_BoxGeometry* self) {
  self->data.disposed = true;
}

// SphereGeometry
farm_SphereGeometry* farm_SphereGeometry_new() {
  return farm_SphereGeometry_new_full(1, 32, 16);
}

farm_SphereGeometry* farm_SphereGeometry_new_r(double radius) {
  return farm_SphereGeometry_new_full(radius, 32, 16);
}

farm_SphereGeometry* farm_SphereGeometry_new_full(double radius, int64_t widthSegments, int64_t heightSegments) {
  farm_SphereGeometry* geom = (farm_SphereGeometry*)farm_arena_alloc(sizeof(farm_SphereGeometry));
  geom->radius = radius;
  geom->widthSegments = (int32_t)widthSegments;
  geom->heightSegments = (int32_t)heightSegments;
  build_sphere_geometry(&geom->data, radius, (int32_t)widthSegments, (int32_t)heightSegments);
  return geom;
}

void farm_SphereGeometry_dispose(farm_SphereGeometry* self) {
  self->data.disposed = true;
}

// PlaneGeometry
farm_PlaneGeometry* farm_PlaneGeometry_new() {
  return farm_PlaneGeometry_new_wh(1, 1);
}

farm_PlaneGeometry* farm_PlaneGeometry_new_wh(double width, double height) {
  farm_PlaneGeometry* geom = (farm_PlaneGeometry*)farm_arena_alloc(sizeof(farm_PlaneGeometry));
  geom->width = width;
  geom->height = height;
  build_plane_geometry(&geom->data, width, height);
  return geom;
}

void farm_PlaneGeometry_dispose(farm_PlaneGeometry* self) {
  self->data.disposed = true;
}

// MeshBasicMaterial
farm_MeshBasicMaterial* farm_MeshBasicMaterial_new() {
  farm_MeshBasicMaterial* mat = (farm_MeshBasicMaterial*)farm_arena_alloc(sizeof(farm_MeshBasicMaterial));
  mat->material_type = 0;
  mat->f_color = farm_Color_new(1, 1, 1);
  mat->disposed = false;
  return mat;
}

farm_MeshBasicMaterial* farm_MeshBasicMaterial_new_color(farm_Color color) {
  farm_MeshBasicMaterial* mat = farm_MeshBasicMaterial_new();
  mat->f_color = color;
  return mat;
}

farm_MeshBasicMaterial* farm_MeshBasicMaterial_new_hex(int64_t hex) {
  farm_MeshBasicMaterial* mat = farm_MeshBasicMaterial_new();
  mat->f_color = farm_Color_setHex((int32_t)hex);
  return mat;
}

void farm_MeshBasicMaterial_dispose(farm_MeshBasicMaterial* self) {
  self->disposed = true;
}

// MeshStandardMaterial
farm_MeshStandardMaterial* farm_MeshStandardMaterial_new() {
  farm_MeshStandardMaterial* mat = (farm_MeshStandardMaterial*)farm_arena_alloc(sizeof(farm_MeshStandardMaterial));
  mat->material_type = 1;
  mat->f_color = farm_Color_new(1, 1, 1);
  mat->roughness = 1.0;
  mat->metalness = 0.0;
  mat->transmission = 0.0;
  mat->ior = 1.5;
  mat->f_emissive = farm_Color_zero();
  mat->emissiveIntensity = 1.0;
  mat->map = NULL;
  mat->disposed = false;
  return mat;
}

farm_MeshStandardMaterial* farm_MeshStandardMaterial_new_color(farm_Color color) {
  farm_MeshStandardMaterial* mat = farm_MeshStandardMaterial_new();
  mat->f_color = color;
  return mat;
}

farm_MeshStandardMaterial* farm_MeshStandardMaterial_new_hex(int64_t hex) {
  farm_MeshStandardMaterial* mat = farm_MeshStandardMaterial_new();
  mat->f_color = farm_Color_setHex((int32_t)hex);
  return mat;
}

void farm_MeshStandardMaterial_set(farm_MeshStandardMaterial* self, farm_Color color) {
  self->f_color = color;
}

void farm_MeshStandardMaterial_setRoughness(farm_MeshStandardMaterial* self, double r) {
  self->roughness = r;
}

void farm_MeshStandardMaterial_setMetalness(farm_MeshStandardMaterial* self, double m) {
  self->metalness = m;
}

void farm_MeshStandardMaterial_setTransmission(farm_MeshStandardMaterial* self, double v) {
  self->transmission = v;
}

void farm_MeshStandardMaterial_setIor(farm_MeshStandardMaterial* self, double v) {
  self->ior = v;
}

void farm_MeshStandardMaterial_setEmissive(farm_MeshStandardMaterial* self, farm_Color c) {
  self->f_emissive = c;
}

void farm_MeshStandardMaterial_setEmissive_hex(farm_MeshStandardMaterial* self, int64_t hex) {
  self->f_emissive = farm_Color_setHex((int32_t)hex);
}

void farm_MeshStandardMaterial_setEmissiveIntensity(farm_MeshStandardMaterial* self, double v) {
  self->emissiveIntensity = v;
}

void farm_MeshStandardMaterial_setMap(farm_MeshStandardMaterial* self, farm_Texture* tex) {
  self->map = tex;
}

void farm_MeshStandardMaterial_dispose(farm_MeshStandardMaterial* self) {
  self->disposed = true;
}

// Mesh
farm_Mesh* farm_Mesh_new_box(farm_BoxGeometry* geometry, void* material, uint8_t mat_type) {
  farm_Mesh* mesh = (farm_Mesh*)farm_arena_alloc(sizeof(farm_Mesh));
  init_Object3D_fields((farm_Object3D*)mesh, FARM_OBJECT3D_TYPE_MESH);
  mesh->f_geometry = geometry;
  mesh->f_material = material;
  mesh->geometry_type = 0;
  mesh->material_type = mat_type;
  return mesh;
}

farm_Mesh* farm_Mesh_new_sphere(farm_SphereGeometry* geometry, void* material, uint8_t mat_type) {
  farm_Mesh* mesh = (farm_Mesh*)farm_arena_alloc(sizeof(farm_Mesh));
  init_Object3D_fields((farm_Object3D*)mesh, FARM_OBJECT3D_TYPE_MESH);
  mesh->f_geometry = geometry;
  mesh->f_material = material;
  mesh->geometry_type = 1;
  mesh->material_type = mat_type;
  return mesh;
}

farm_Mesh* farm_Mesh_new_plane(farm_PlaneGeometry* geometry, void* material, uint8_t mat_type) {
  farm_Mesh* mesh = (farm_Mesh*)farm_arena_alloc(sizeof(farm_Mesh));
  init_Object3D_fields((farm_Object3D*)mesh, FARM_OBJECT3D_TYPE_MESH);
  mesh->f_geometry = geometry;
  mesh->f_material = material;
  mesh->geometry_type = 2;
  mesh->material_type = mat_type;
  return mesh;
}

// Generic Mesh constructor - determines types at runtime
// This requires us to identify geometry/material types, which we'll do via pointer ranges
// For simplicity in M3, we'll use a type-tagging approach
farm_Mesh* farm_Mesh_new(void* geometry, void* material) {
  farm_Mesh* mesh = (farm_Mesh*)farm_arena_alloc(sizeof(farm_Mesh));
  init_Object3D_fields((farm_Object3D*)mesh, FARM_OBJECT3D_TYPE_MESH);
  mesh->f_geometry = geometry;
  mesh->f_material = material;
  
  // Type detection: check the first few bytes of the structure
  // BoxGeometry, SphereGeometry, PlaneGeometry all start with farm_GeometryData
  // We'll use a simple heuristic: check if disposed field pattern
  farm_BoxGeometry* box_test = (farm_BoxGeometry*)geometry;
  farm_SphereGeometry* sphere_test = (farm_SphereGeometry*)geometry;
  farm_PlaneGeometry* plane_test = (farm_PlaneGeometry*)geometry;
  
  // Check vertex counts to distinguish geometry types
  // Box: 24 vertices, Sphere: varies, Plane: 4 vertices
  if (box_test->data.vertex_count == 24) {
    mesh->geometry_type = 0; // Box
  } else if (plane_test->data.vertex_count == 4) {
    mesh->geometry_type = 2; // Plane
  } else {
    mesh->geometry_type = 1; // Sphere (or other)
  }
  
  // Material type detection: Basic (0) vs Standard (1)
  // Check the material_type field that all materials now have
  uint8_t* mat_type_ptr = (uint8_t*)material;
  mesh->material_type = *mat_type_ptr;
  
  return mesh;
}

// Lights
farm_AmbientLight* farm_AmbientLight_new() {
  farm_AmbientLight* light = (farm_AmbientLight*)farm_arena_alloc(sizeof(farm_AmbientLight));
  init_Object3D_fields((farm_Object3D*)light, FARM_OBJECT3D_TYPE_AMBIENT_LIGHT);
  light->f_color = farm_Color_new(1, 1, 1);
  light->f_intensity = 1.0;
  return light;
}

farm_AmbientLight* farm_AmbientLight_new_hex(int64_t hex) {
  farm_AmbientLight* light = farm_AmbientLight_new();
  light->f_color = farm_Color_setHex((int32_t)hex);
  return light;
}

farm_AmbientLight* farm_AmbientLight_new_hex_i(int64_t hex, double intensity) {
  farm_AmbientLight* light = farm_AmbientLight_new_hex(hex);
  light->f_intensity = intensity;
  return light;
}

farm_AmbientLight* farm_AmbientLight_new_color_i(farm_Color color, double intensity) {
  farm_AmbientLight* light = farm_AmbientLight_new();
  light->f_color = color;
  light->f_intensity = intensity;
  return light;
}

// DirectionalLight
farm_DirectionalLight* farm_DirectionalLight_new() {
  farm_DirectionalLight* light = (farm_DirectionalLight*)farm_arena_alloc(sizeof(farm_DirectionalLight));
  init_Object3D_fields((farm_Object3D*)light, FARM_OBJECT3D_TYPE_DIRECTIONAL_LIGHT);
  light->f_color = farm_Color_new(1, 1, 1);
  light->f_intensity = 1.0;
  return light;
}

farm_DirectionalLight* farm_DirectionalLight_new_hex(int64_t hex) {
  farm_DirectionalLight* light = farm_DirectionalLight_new();
  light->f_color = farm_Color_setHex((int32_t)hex);
  return light;
}

farm_DirectionalLight* farm_DirectionalLight_new_hex_i(int64_t hex, double intensity) {
  farm_DirectionalLight* light = farm_DirectionalLight_new_hex(hex);
  light->f_intensity = intensity;
  return light;
}

farm_DirectionalLight* farm_DirectionalLight_new_color_i(farm_Color color, double intensity) {
  farm_DirectionalLight* light = farm_DirectionalLight_new();
  light->f_color = color;
  light->f_intensity = intensity;
  return light;
}

// PointLight
farm_PointLight* farm_PointLight_new() {
  farm_PointLight* light = (farm_PointLight*)farm_arena_alloc(sizeof(farm_PointLight));
  init_Object3D_fields((farm_Object3D*)light, FARM_OBJECT3D_TYPE_POINT_LIGHT);
  light->f_color = farm_Color_new(1, 1, 1);
  light->f_intensity = 1.0;
  light->f_distance = 0.0;
  light->f_decay = 2.0;
  return light;
}

farm_PointLight* farm_PointLight_new_hex(int64_t hex) {
  farm_PointLight* light = farm_PointLight_new();
  light->f_color = farm_Color_setHex((int32_t)hex);
  return light;
}

farm_PointLight* farm_PointLight_new_hex_i(int64_t hex, double intensity) {
  farm_PointLight* light = farm_PointLight_new_hex(hex);
  light->f_intensity = intensity;
  return light;
}

farm_PointLight* farm_PointLight_new_color_i(farm_Color color, double intensity) {
  farm_PointLight* light = farm_PointLight_new();
  light->f_color = color;
  light->f_intensity = intensity;
  return light;
}

farm_PointLight* farm_PointLight_new_full(farm_Color color, double intensity, double distance, double decay) {
  farm_PointLight* light = farm_PointLight_new();
  light->f_color = color;
  light->f_intensity = intensity;
  light->f_distance = distance;
  light->f_decay = decay;
  return light;
}

farm_PointLight* farm_PointLight_new_hex_full(int64_t hex, double intensity, double distance, double decay) {
  return farm_PointLight_new_full(farm_Color_setHex((int32_t)hex), intensity, distance, decay);
}

farm_RectAreaLight* farm_RectAreaLight_new() {
  farm_RectAreaLight* light = (farm_RectAreaLight*)farm_arena_alloc(sizeof(farm_RectAreaLight));
  init_Object3D_fields((farm_Object3D*)light, FARM_OBJECT3D_TYPE_RECT_AREA_LIGHT);
  light->f_color = farm_Color_new(1, 1, 1);
  light->f_intensity = 1.0;
  light->width = 1.0;
  light->height = 1.0;
  return light;
}

farm_RectAreaLight* farm_RectAreaLight_new_hex_i(int64_t hex, double intensity) {
  farm_RectAreaLight* light = farm_RectAreaLight_new();
  light->f_color = farm_Color_setHex((int32_t)hex);
  light->f_intensity = intensity;
  return light;
}

farm_RectAreaLight* farm_RectAreaLight_new_hex_i_wh(int64_t hex, double intensity, double width, double height) {
  farm_RectAreaLight* light = farm_RectAreaLight_new_hex_i(hex, intensity);
  light->width = width;
  light->height = height;
  return light;
}

farm_RectAreaLight* farm_RectAreaLight_new_color_i_wh(farm_Color color, double intensity, double width, double height) {
  farm_RectAreaLight* light = farm_RectAreaLight_new();
  light->f_color = color;
  light->f_intensity = intensity;
  light->width = width;
  light->height = height;
  return light;
}

static uint32_t tex_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t tex_crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    for (int j = 0; j < 8; j++) {
      if ((crc ^ b) & 1) crc = (crc >> 1) ^ 0xEDB88320u;
      else crc >>= 1;
      b >>= 1;
    }
  }
  return ~crc;
}

static int tex_inflate_stored(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len) {
  if (src_len < 2) return 0;
  size_t p = 2; /* zlib CMF/FLG */
  size_t out = 0;
  for (;;) {
    if (p >= src_len) return 0;
    uint8_t hdr = src[p++];
    int bfinal = hdr & 1;
    int btype = (hdr >> 1) & 3;
    if (btype != 0) return 0;
    if (p + 4 > src_len) return 0;
    uint32_t len = (uint32_t)src[p] | ((uint32_t)src[p + 1] << 8);
    uint32_t nlen = (uint32_t)src[p + 2] | ((uint32_t)src[p + 3] << 8);
    p += 4;
    if ((len ^ 0xFFFFu) != nlen) return 0;
    if (p + len > src_len) return 0;
    if (out + len > dst_len) return 0;
    memcpy(dst + out, src + p, len);
    out += len;
    p += len;
    if (bfinal) break;
  }
  return out == dst_len;
}

farm_Texture* farm_Texture_new(FarmString path) {
  char* path_cstr = (char*)farm_arena_alloc(path.len + 1);
  memcpy(path_cstr, path.ptr, path.len);
  path_cstr[path.len] = '\0';
  FILE* f = fopen(path_cstr, "rb");
  if (!f) farm_trap(110, "texture load failed");
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); farm_trap(110, "texture load failed"); }
  long sz = ftell(f);
  if (sz < 33) { fclose(f); farm_trap(110, "texture load failed"); }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); farm_trap(110, "texture load failed"); }
  uint8_t* file = (uint8_t*)farm_arena_alloc((size_t)sz);
  if (fread(file, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); farm_trap(110, "texture load failed"); }
  fclose(f);
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (memcmp(file, sig, 8) != 0) farm_trap(110, "texture load failed");
  int32_t tw = 0, th = 0;
  uint8_t* idat = NULL;
  size_t idat_len = 0;
  size_t pos = 8;
  int saw_ihdr = 0, saw_iend = 0;
  while (pos + 12 <= (size_t)sz) {
    uint32_t ln = tex_be32(file + pos);
    if (pos + 12 + ln > (size_t)sz) farm_trap(110, "texture load failed");
    const uint8_t* tag = file + pos + 4;
    const uint8_t* chunk = file + pos + 8;
    uint32_t crc_got = tex_be32(file + pos + 8 + ln);
    if (tex_crc32(tag, 4 + ln) != crc_got) farm_trap(110, "texture load failed");
    if (memcmp(tag, "IHDR", 4) == 0) {
      if (ln != 13 || saw_ihdr) farm_trap(110, "texture load failed");
      tw = (int32_t)tex_be32(chunk);
      th = (int32_t)tex_be32(chunk + 4);
      if (tw <= 0 || th <= 0) farm_trap(110, "texture load failed");
      if (chunk[8] != 8 || chunk[9] != 2 || chunk[10] != 0 || chunk[11] != 0 || chunk[12] != 0)
        farm_trap(110, "texture load failed");
      saw_ihdr = 1;
    } else if (memcmp(tag, "IDAT", 4) == 0) {
      uint8_t* nbuf = (uint8_t*)farm_arena_alloc(idat_len + ln);
      if (idat_len) memcpy(nbuf, idat, idat_len);
      memcpy(nbuf + idat_len, chunk, ln);
      idat = nbuf;
      idat_len += ln;
    } else if (memcmp(tag, "IEND", 4) == 0) {
      saw_iend = 1;
      break;
    }
    pos += 12 + ln;
  }
  if (!saw_ihdr || !saw_iend || !idat) farm_trap(110, "texture load failed");
  size_t raw_size = (size_t)th * (1 + (size_t)tw * 3);
  uint8_t* raw = (uint8_t*)farm_arena_alloc(raw_size);
  if (!tex_inflate_stored(idat, idat_len, raw, raw_size)) farm_trap(110, "texture load failed");
  farm_Texture* tex = (farm_Texture*)farm_arena_alloc(sizeof(farm_Texture));
  tex->width = tw;
  tex->height = th;
  tex->rgb = (double*)farm_arena_alloc((size_t)tw * (size_t)th * 3 * sizeof(double));
  for (int32_t y = 0; y < th; y++) {
    size_t row = (size_t)y * (1 + (size_t)tw * 3);
    if (raw[row] != 0) farm_trap(110, "texture load failed");
    for (int32_t x = 0; x < tw; x++) {
      size_t si = row + 1 + (size_t)x * 3;
      size_t di = ((size_t)y * (size_t)tw + (size_t)x) * 3;
      tex->rgb[di + 0] = raw[si + 0] / 255.0;
      tex->rgb[di + 1] = raw[si + 1] / 255.0;
      tex->rgb[di + 2] = raw[si + 2] / 255.0;
    }
  }
  return tex;
}

// Renderer - rasterizer and PNG writer
farm_Renderer* farm_Renderer_new() {
  farm_Renderer* r = (farm_Renderer*)farm_arena_alloc(sizeof(farm_Renderer));
  r->framebuffer = NULL;
  r->depthbuffer = NULL;
  r->accum = NULL;
  r->accum_count = 0;
  r->samples = 1;
  r->max_bounces = 4;
  r->width = 0;
  r->height = 0;
  r->disposed = false;
  return r;
}

farm_Renderer* farm_Renderer_new_wh(int64_t width, int64_t height) {
  farm_Renderer* r = farm_Renderer_new();
  farm_Renderer_setSize(r, width, height);
  return r;
}

void farm_Renderer_setSize(farm_Renderer* self, int64_t width, int64_t height) {
  if (width <= 0 || height <= 0) {
    farm_trap(107, "invalid renderer size");
  }
  
  self->width = (int32_t)width;
  self->height = (int32_t)height;
  
  int64_t n = width * height;
  self->framebuffer = (uint8_t*)farm_arena_alloc((size_t)n * 3);
  self->depthbuffer = (float*)farm_arena_alloc((size_t)n * sizeof(float));
  self->accum = (double*)farm_arena_alloc((size_t)n * 3 * sizeof(double));
  memset(self->framebuffer, 0, (size_t)n * 3);
  memset(self->accum, 0, (size_t)n * 3 * sizeof(double));
  self->accum_count = 0;
}

void farm_Renderer_setSamples(farm_Renderer* self, int64_t n) {
  self->samples = (int32_t)n;
}

void farm_Renderer_setMaxBounces(farm_Renderer* self, int64_t n) {
  self->max_bounces = (int32_t)n;
}

void farm_Renderer_resetAccumulation(farm_Renderer* self) {
  if (self->accum && self->width > 0 && self->height > 0) {
    memset(self->accum, 0, (size_t)self->width * (size_t)self->height * 3 * sizeof(double));
  }
  self->accum_count = 0;
}

// Helper: edge function for triangle rasterization
static inline double edge_function(double ax, double ay, double bx, double by, double cx, double cy) {
  return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

// Helper: transform vector by matrix (point)
static farm_Vector3 transform_point(farm_Matrix4 m, farm_Vector3 v) {
  double* e = m.elements;
  double x = e[0]*v.f_x + e[4]*v.f_y + e[8]*v.f_z + e[12];
  double y = e[1]*v.f_x + e[5]*v.f_y + e[9]*v.f_z + e[13];
  double z = e[2]*v.f_x + e[6]*v.f_y + e[10]*v.f_z + e[14];
  double w = e[3]*v.f_x + e[7]*v.f_y + e[11]*v.f_z + e[15];
  return farm_Vector3_new(x/w, y/w, z/w);
}

// Helper: transform direction by matrix (no translation)
static farm_Vector3 transform_direction(farm_Matrix4 m, farm_Vector3 v) {
  double* e = m.elements;
  double x = e[0]*v.f_x + e[4]*v.f_y + e[8]*v.f_z;
  double y = e[1]*v.f_x + e[5]*v.f_y + e[9]*v.f_z;
  double z = e[2]*v.f_x + e[6]*v.f_y + e[10]*v.f_z;
  return farm_Vector3_new(x, y, z);
}

// Collect lights
typedef struct {
  farm_AmbientLight* ambients[16];
  farm_DirectionalLight* directionals[16];
  farm_PointLight* points[16];
  int ambient_count;
  int directional_count;
  int point_count;
} LightList;

static void collect_lights_recursive(farm_Object3D* obj, LightList* lights) {
  if (!obj->f_visible) return;
  
  // Check if this is a light (hack: check if it's an ambient/directional/point)
  // In a real implementation we'd have type tags, but for now we check sizes/patterns
  // For simplicity, we'll use pointer casts with careful memory layout
  
  // Try to detect light types by checking if the object has been allocated as a light
  // This is a simplification - in production we'd use proper type tags
  
  // Skip for now - we'll handle lights in render traversal
  
  for (int32_t i = 0; i < obj->children.count; i++) {
    collect_lights_recursive(obj->children.items[i], lights);
  }
}

// Shader: Basic (unlit)
static farm_Color shade_basic(farm_MeshBasicMaterial* mat) {
  return mat->f_color;
}

// Shader: Standard (Lambert + Blinn-Phong approximation)
static farm_Color shade_standard(
  farm_MeshStandardMaterial* mat,
  farm_Vector3 world_pos,
  farm_Vector3 world_normal,
  farm_Vector3 camera_pos,
  LightList* lights
) {
  farm_Color albedo = mat->f_color;
  double metal = clamp(mat->metalness, 0, 1);
  double rough = clamp(mat->roughness, 0, 1);
  
  farm_Color result = farm_Color_zero();
  
  // Ambient lights
  for (int i = 0; i < lights->ambient_count; i++) {
    farm_AmbientLight* L = lights->ambients[i];
    result.r += albedo.r * L->f_color.r * L->f_intensity;
    result.g += albedo.g * L->f_color.g * L->f_intensity;
    result.b += albedo.b * L->f_color.b * L->f_intensity;
  }
  
  double shininess = 1.0 + (1.0 - rough) * 255.0;
  double specStrength = 0.2 + 0.8 * (1.0 - rough);
  farm_Color specColor;
  specColor.r = (1.0 - metal) * 1.0 + metal * albedo.r;
  specColor.g = (1.0 - metal) * 1.0 + metal * albedo.g;
  specColor.b = (1.0 - metal) * 1.0 + metal * albedo.b;
  
  farm_Vector3 V = farm_Vector3_normalize(farm_Vector3_sub(camera_pos, world_pos));
  farm_Vector3 N = world_normal;
  
  // Directional lights
  for (int i = 0; i < lights->directional_count; i++) {
    farm_DirectionalLight* L = lights->directionals[i];
    
    // World direction: transform local (0,0,-1) by f_matrixWorld
    farm_Vector3 local_dir = farm_Vector3_new(0, 0, -1);
    farm_Vector3 world_dir = transform_direction(L->f_matrixWorld, local_dir);
    world_dir = farm_Vector3_normalize(world_dir);
    
    // Incident light direction (toward surface)
    farm_Vector3 Ldir = farm_Vector3_multiplyScalar(world_dir, -1.0);
    
    double NdotL = farm_Vector3_dot(N, Ldir);
    if (NdotL < 0) NdotL = 0;
    
    // Diffuse
    double diff_factor = NdotL * (1.0 - 0.9 * metal);
    result.r += albedo.r * L->f_color.r * L->f_intensity * diff_factor;
    result.g += albedo.g * L->f_color.g * L->f_intensity * diff_factor;
    result.b += albedo.b * L->f_color.b * L->f_intensity * diff_factor;
    
    // Specular (Blinn-Phong)
    farm_Vector3 H = farm_Vector3_normalize(farm_Vector3_add(Ldir, V));
    double NdotH = farm_Vector3_dot(N, H);
    if (NdotH < 0) NdotH = 0;
    
    double specular = (NdotH > 0) ? pow(NdotH, shininess) * specStrength : 0;
    result.r += specColor.r * L->f_color.r * L->f_intensity * specular;
    result.g += specColor.g * L->f_color.g * L->f_intensity * specular;
    result.b += specColor.b * L->f_color.b * L->f_intensity * specular;
  }
  
  // Point lights
  for (int i = 0; i < lights->point_count; i++) {
    farm_PointLight* L = lights->points[i];
    
    // World position: extract translation from f_matrixWorld
    farm_Vector3 light_pos;
    light_pos.f_x = L->f_matrixWorld.elements[12];
    light_pos.f_y = L->f_matrixWorld.elements[13];
    light_pos.f_z = L->f_matrixWorld.elements[14];
    
    farm_Vector3 toLight = farm_Vector3_sub(light_pos, world_pos);
    double dist = farm_Vector3_length(toLight);
    if (dist < 1e-6) continue;
    
    farm_Vector3 Ldir = farm_Vector3_multiplyScalar(toLight, 1.0 / dist);
    
    // Attenuation
    double attenuation = 1.0 / fmax(pow(dist, L->f_decay), 1e-6);
    if (L->f_distance > 0 && dist >= L->f_distance) {
      attenuation = 0;
    }
    
    double NdotL = farm_Vector3_dot(N, Ldir);
    if (NdotL < 0) NdotL = 0;
    
    // Diffuse
    double diff_factor = NdotL * (1.0 - 0.9 * metal) * attenuation;
    result.r += albedo.r * L->f_color.r * L->f_intensity * diff_factor;
    result.g += albedo.g * L->f_color.g * L->f_intensity * diff_factor;
    result.b += albedo.b * L->f_color.b * L->f_intensity * diff_factor;
    
    // Specular
    farm_Vector3 H = farm_Vector3_normalize(farm_Vector3_add(Ldir, V));
    double NdotH = farm_Vector3_dot(N, H);
    if (NdotH < 0) NdotH = 0;
    
    double specular = (NdotH > 0) ? pow(NdotH, shininess) * specStrength * attenuation : 0;
    result.r += specColor.r * L->f_color.r * L->f_intensity * specular;
    result.g += specColor.g * L->f_color.g * L->f_intensity * specular;
    result.b += specColor.b * L->f_color.b * L->f_intensity * specular;
  }
  
  // Clamp
  result.r = clamp(result.r, 0, 1);
  result.g = clamp(result.g, 0, 1);
  result.b = clamp(result.b, 0, 1);
  
  return result;
}

// Helper: collect lights by traversing scene graph
static void traverse_collect_lights(farm_Object3D* obj, LightList* lights);

static void traverse_collect_lights(farm_Object3D* obj, LightList* lights) {
  if (!obj->f_visible) return;
  
  // Check the type field to determine if this is a light
  if (obj->type == FARM_OBJECT3D_TYPE_AMBIENT_LIGHT && lights->ambient_count < 16) {
    lights->ambients[lights->ambient_count++] = (farm_AmbientLight*)obj;
  } else if (obj->type == FARM_OBJECT3D_TYPE_DIRECTIONAL_LIGHT && lights->directional_count < 16) {
    lights->directionals[lights->directional_count++] = (farm_DirectionalLight*)obj;
  } else if (obj->type == FARM_OBJECT3D_TYPE_POINT_LIGHT && lights->point_count < 16) {
    lights->points[lights->point_count++] = (farm_PointLight*)obj;
  }
  
  for (int32_t i = 0; i < obj->children.count; i++) {
    traverse_collect_lights(obj->children.items[i], lights);
  }
}

// Rasterize a single mesh
static void rasterize_mesh(
  farm_Renderer* renderer,
  farm_Mesh* mesh,
  farm_PerspectiveCamera* camera,
  LightList* lights
) {
  if (!mesh->f_visible) return;
  
  // Check disposed
  farm_GeometryData* geom_data = NULL;
  if (mesh->geometry_type == 0) {
    geom_data = &((farm_BoxGeometry*)mesh->f_geometry)->data;
  } else if (mesh->geometry_type == 1) {
    geom_data = &((farm_SphereGeometry*)mesh->f_geometry)->data;
  } else if (mesh->geometry_type == 2) {
    geom_data = &((farm_PlaneGeometry*)mesh->f_geometry)->data;
  }
  
  if (geom_data->disposed) {
    farm_trap(105, "use after dispose");
  }
  
  bool mat_disposed = false;
  if (mesh->material_type == 0) {
    mat_disposed = ((farm_MeshBasicMaterial*)mesh->f_material)->disposed;
  } else {
    mat_disposed = ((farm_MeshStandardMaterial*)mesh->f_material)->disposed;
  }
  if (mat_disposed) {
    farm_trap(105, "use after dispose");
  }
  
  farm_Matrix4 mvp = farm_Matrix4_multiplyMatrices(
    camera->projectionMatrix,
    farm_Matrix4_multiplyMatrices(camera->matrixWorldInverse, mesh->f_matrixWorld)
  );
  
  farm_Vector3 camera_pos;
  camera_pos.f_x = camera->f_matrixWorld.elements[12];
  camera_pos.f_y = camera->f_matrixWorld.elements[13];
  camera_pos.f_z = camera->f_matrixWorld.elements[14];
  
  // Rasterize each triangle
  for (int32_t ti = 0; ti < geom_data->index_count / 3; ti++) {
    int32_t i0 = geom_data->indices[ti * 3 + 0];
    int32_t i1 = geom_data->indices[ti * 3 + 1];
    int32_t i2 = geom_data->indices[ti * 3 + 2];
    
    // Get positions and normals
    farm_Vector3 p0 = farm_Vector3_new(
      geom_data->positions[i0*3+0],
      geom_data->positions[i0*3+1],
      geom_data->positions[i0*3+2]
    );
    farm_Vector3 p1 = farm_Vector3_new(
      geom_data->positions[i1*3+0],
      geom_data->positions[i1*3+1],
      geom_data->positions[i1*3+2]
    );
    farm_Vector3 p2 = farm_Vector3_new(
      geom_data->positions[i2*3+0],
      geom_data->positions[i2*3+1],
      geom_data->positions[i2*3+2]
    );
    
    farm_Vector3 n0 = farm_Vector3_new(
      geom_data->normals[i0*3+0],
      geom_data->normals[i0*3+1],
      geom_data->normals[i0*3+2]
    );
    farm_Vector3 n1 = farm_Vector3_new(
      geom_data->normals[i1*3+0],
      geom_data->normals[i1*3+1],
      geom_data->normals[i1*3+2]
    );
    farm_Vector3 n2 = farm_Vector3_new(
      geom_data->normals[i2*3+0],
      geom_data->normals[i2*3+1],
      geom_data->normals[i2*3+2]
    );
    
    // Transform to world space
    farm_Vector3 wp0 = transform_point(mesh->f_matrixWorld, p0);
    farm_Vector3 wp1 = transform_point(mesh->f_matrixWorld, p1);
    farm_Vector3 wp2 = transform_point(mesh->f_matrixWorld, p2);
    
    farm_Vector3 wn0 = farm_Vector3_normalize(transform_direction(mesh->f_matrixWorld, n0));
    farm_Vector3 wn1 = farm_Vector3_normalize(transform_direction(mesh->f_matrixWorld, n1));
    farm_Vector3 wn2 = farm_Vector3_normalize(transform_direction(mesh->f_matrixWorld, n2));
    
    // Transform to clip space
    farm_Vector4 clip0 = farm_Vector4_applyMatrix4(farm_Vector4_new(p0.f_x, p0.f_y, p0.f_z, 1), mvp);
    farm_Vector4 clip1 = farm_Vector4_applyMatrix4(farm_Vector4_new(p1.f_x, p1.f_y, p1.f_z, 1), mvp);
    farm_Vector4 clip2 = farm_Vector4_applyMatrix4(farm_Vector4_new(p2.f_x, p2.f_y, p2.f_z, 1), mvp);
    
    // Reject if behind camera
    if (clip0.w <= 0 || clip1.w <= 0 || clip2.w <= 0) continue;
    
    // NDC
    double ndc0_x = clip0.f_x / clip0.w, ndc0_y = clip0.f_y / clip0.w, ndc0_z = clip0.f_z / clip0.w;
    double ndc1_x = clip1.f_x / clip1.w, ndc1_y = clip1.f_y / clip1.w, ndc1_z = clip1.f_z / clip1.w;
    double ndc2_x = clip2.f_x / clip2.w, ndc2_y = clip2.f_y / clip2.w, ndc2_z = clip2.f_z / clip2.w;
    
    // Screen space
    double sx0 = (ndc0_x * 0.5 + 0.5) * renderer->width;
    double sy0 = (1.0 - (ndc0_y * 0.5 + 0.5)) * renderer->height;
    double sx1 = (ndc1_x * 0.5 + 0.5) * renderer->width;
    double sy1 = (1.0 - (ndc1_y * 0.5 + 0.5)) * renderer->height;
    double sx2 = (ndc2_x * 0.5 + 0.5) * renderer->width;
    double sy2 = (1.0 - (ndc2_y * 0.5 + 0.5)) * renderer->height;
    
    // Back-face culling
    double area = edge_function(sx0, sy0, sx1, sy1, sx2, sy2);
    if (area <= 0) continue;
    
    // Bounding box
    int32_t minX = (int32_t)fmax(0, floor(fmin(fmin(sx0, sx1), sx2)));
    int32_t maxX = (int32_t)fmin(renderer->width - 1, ceil(fmax(fmax(sx0, sx1), sx2)));
    int32_t minY = (int32_t)fmax(0, floor(fmin(fmin(sy0, sy1), sy2)));
    int32_t maxY = (int32_t)fmin(renderer->height - 1, ceil(fmax(fmax(sy0, sy1), sy2)));
    
    // Rasterize pixels
    for (int32_t py = minY; py <= maxY; py++) {
      for (int32_t px = minX; px <= maxX; px++) {
        double sample_x = px + 0.5;
        double sample_y = py + 0.5;
        
        // Barycentric coordinates (raw edge function values)
        double w0_raw = edge_function(sx1, sy1, sx2, sy2, sample_x, sample_y);
        double w1_raw = edge_function(sx2, sy2, sx0, sy0, sample_x, sample_y);
        double w2_raw = edge_function(sx0, sy0, sx1, sy1, sample_x, sample_y);
        
        if (w0_raw < 0 || w1_raw < 0 || w2_raw < 0) continue;
        
        // Normalize
        double inv_area = 1.0 / area;
        double w0 = w0_raw * inv_area;
        double w1 = w1_raw * inv_area;
        double w2 = w2_raw * inv_area;
        
        // Interpolate depth (NDC z)
        double z = w0 * ndc0_z + w1 * ndc1_z + w2 * ndc2_z;
        
        int32_t pixel_idx = py * renderer->width + px;
        if (z >= renderer->depthbuffer[pixel_idx]) continue;
        
        renderer->depthbuffer[pixel_idx] = (float)z;
        
        // Interpolate world position and normal
        farm_Vector3 world_pos;
        world_pos.f_x = w0 * wp0.f_x + w1 * wp1.f_x + w2 * wp2.f_x;
        world_pos.f_y = w0 * wp0.f_y + w1 * wp1.f_y + w2 * wp2.f_y;
        world_pos.f_z = w0 * wp0.f_z + w1 * wp1.f_z + w2 * wp2.f_z;
        
        farm_Vector3 world_normal;
        world_normal.f_x = w0 * wn0.f_x + w1 * wn1.f_x + w2 * wn2.f_x;
        world_normal.f_y = w0 * wn0.f_y + w1 * wn1.f_y + w2 * wn2.f_y;
        world_normal.f_z = w0 * wn0.f_z + w1 * wn1.f_z + w2 * wn2.f_z;
        world_normal = farm_Vector3_normalize(world_normal);
        
        // Shade
        farm_Color color;
        if (mesh->material_type == 0) {
          // Basic material (unlit)
          color = shade_basic((farm_MeshBasicMaterial*)mesh->f_material);
        } else {
          // Standard material (lit)
          color = shade_standard(
            (farm_MeshStandardMaterial*)mesh->f_material,
            world_pos,
            world_normal,
            camera_pos,
            lights
          );
        }
        
        // Write to framebuffer
        uint8_t r = (uint8_t)(clamp(color.r, 0, 1) * 255.0 + 0.5);
        uint8_t g = (uint8_t)(clamp(color.g, 0, 1) * 255.0 + 0.5);
        uint8_t b = (uint8_t)(clamp(color.b, 0, 1) * 255.0 + 0.5);
        
        renderer->framebuffer[pixel_idx * 3 + 0] = r;
        renderer->framebuffer[pixel_idx * 3 + 1] = g;
        renderer->framebuffer[pixel_idx * 3 + 2] = b;
      }
    }
  }
}

// Traverse scene and render
static void traverse_render(
  farm_Object3D* obj,
  farm_Renderer* renderer,
  farm_PerspectiveCamera* camera,
  LightList* lights
);

static void traverse_render(
  farm_Object3D* obj,
  farm_Renderer* renderer,
  farm_PerspectiveCamera* camera,
  LightList* lights
) {
  if (!obj->f_visible) return;
  
  // Check if this is a mesh using the type field
  if (obj->type == FARM_OBJECT3D_TYPE_MESH) {
    farm_Mesh* mesh = (farm_Mesh*)obj;
    if (mesh->f_geometry != NULL) {
      rasterize_mesh(renderer, mesh, camera, lights);
    }
  }
  
  for (int32_t i = 0; i < obj->children.count; i++) {
    traverse_render(obj->children.items[i], renderer, camera, lights);
  }
}

void farm_Renderer_render(farm_Renderer* self, farm_Scene* scene, farm_PerspectiveCamera* camera) {
  if (self->disposed) {
    farm_trap(105, "use after dispose");
  }
  
  if (self->width <= 0 || self->height <= 0) {
    farm_trap(107, "invalid renderer size");
  }
  
  // Update matrices
  farm_Scene_updateMatrixWorld(scene, true);
  farm_PerspectiveCamera_updateMatrixWorld(camera, true);
  
  // Update camera inverse and projection
  camera->matrixWorldInverse = farm_Matrix4_invert(camera->f_matrixWorld);
  farm_PerspectiveCamera_updateProjectionMatrix(camera);
  
  // Clear buffers
  farm_Color clear_color = scene->hasBackground ? scene->background : farm_Color_zero();
  uint8_t clear_r = (uint8_t)(clamp(clear_color.r, 0, 1) * 255.0 + 0.5);
  uint8_t clear_g = (uint8_t)(clamp(clear_color.g, 0, 1) * 255.0 + 0.5);
  uint8_t clear_b = (uint8_t)(clamp(clear_color.b, 0, 1) * 255.0 + 0.5);
  
  for (int32_t i = 0; i < self->width * self->height; i++) {
    self->framebuffer[i * 3 + 0] = clear_r;
    self->framebuffer[i * 3 + 1] = clear_g;
    self->framebuffer[i * 3 + 2] = clear_b;
    self->depthbuffer[i] = 1e30f; // Far depth
  }
  
  // Collect lights from scene graph
  LightList lights = {0};
  traverse_collect_lights((farm_Object3D*)scene, &lights);
  
  // Traverse and render
  traverse_render((farm_Object3D*)scene, self, camera, &lights);
}

// PNG writer (canonical format per spec)
// Helper: compute CRC32
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    for (int j = 0; j < 8; j++) {
      if ((crc ^ b) & 1) crc = (crc >> 1) ^ 0xEDB88320;
      else crc >>= 1;
      b >>= 1;
    }
  }
  return crc;
}

static void write_png_chunk(FILE* f, const char* type, uint8_t* data, uint32_t len) {
  // Length (big-endian)
  uint32_t len_be = ((len & 0xFF) << 24) | ((len & 0xFF00) << 8) | 
                    ((len & 0xFF0000) >> 8) | ((len & 0xFF000000) >> 24);
  fwrite(&len_be, 4, 1, f);
  
  // Type
  fwrite(type, 1, 4, f);
  
  // Data
  if (len > 0) fwrite(data, 1, len, f);
  
  // CRC
  uint32_t crc = 0xFFFFFFFF;
  crc = crc32_update(crc, (const uint8_t*)type, 4);
  crc = crc32_update(crc, data, len);
  crc = ~crc;
  
  uint32_t crc_be = ((crc & 0xFF) << 24) | ((crc & 0xFF00) << 8) | 
                    ((crc & 0xFF0000) >> 8) | ((crc & 0xFF000000) >> 24);
  fwrite(&crc_be, 4, 1, f);
}

void farm_Renderer_savePNG(farm_Renderer* self, FarmString path) {
  if (self->disposed) {
    farm_trap(105, "use after dispose");
  }
  
  // Convert FarmString to C string
  char* path_cstr = (char*)farm_arena_alloc(path.len + 1);
  memcpy(path_cstr, path.ptr, path.len);
  path_cstr[path.len] = '\0';
  
  FILE* f = fopen(path_cstr, "wb");
  if (!f) {
    farm_trap(108, "PNG write failed");
  }
  
  // PNG signature
  uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  
  // IHDR
  uint8_t ihdr[13];
  uint32_t w = self->width, h = self->height;
  ihdr[0] = (w >> 24) & 0xFF; ihdr[1] = (w >> 16) & 0xFF;
  ihdr[2] = (w >> 8) & 0xFF; ihdr[3] = w & 0xFF;
  ihdr[4] = (h >> 24) & 0xFF; ihdr[5] = (h >> 16) & 0xFF;
  ihdr[6] = (h >> 8) & 0xFF; ihdr[7] = h & 0xFF;
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 2;  // RGB
  ihdr[10] = 0; // compression
  ihdr[11] = 0; // filter
  ihdr[12] = 0; // interlace
  write_png_chunk(f, "IHDR", ihdr, 13);
  
  // IDAT (with filter type 0 per scanline, compression level 0)
  uint32_t scanline_size = 1 + self->width * 3; // filter byte + RGB
  uint32_t raw_size = scanline_size * self->height;
  uint8_t* raw_data = (uint8_t*)farm_arena_alloc(raw_size);
  
  for (int32_t y = 0; y < self->height; y++) {
    raw_data[y * scanline_size] = 0; // filter type None
    memcpy(&raw_data[y * scanline_size + 1], 
           &self->framebuffer[y * self->width * 3], 
           self->width * 3);
  }
  
  // Zlib wrapper matching Python zlib.compress(..., level=0) exactly
  // Block pattern: first 65531, second 32773, all remaining 65535
  uint32_t num_blocks = 0;
  uint32_t temp_remaining = raw_size;
  if (temp_remaining > 0) {
    num_blocks++;
    temp_remaining -= (temp_remaining > 65531) ? 65531 : temp_remaining;
    if (temp_remaining > 0) {
      num_blocks++;
      temp_remaining -= (temp_remaining > 32773) ? 32773 : temp_remaining;
      while (temp_remaining > 0) {
        num_blocks++;
        temp_remaining -= (temp_remaining > 65535) ? 65535 : temp_remaining;
      }
    }
  }
  
  uint32_t idat_size = 2 + raw_size + 5 * num_blocks + 4;
  uint8_t* idat_data = (uint8_t*)farm_arena_alloc(idat_size);
  uint32_t idat_pos = 0;
  
  // Zlib header
  idat_data[idat_pos++] = 0x78;
  idat_data[idat_pos++] = 0x01;
  
  // Emit blocks following Python zlib pattern
  uint32_t remaining = raw_size;
  uint32_t offset = 0;
  uint32_t block_num = 0;
  
  while (remaining > 0) {
    uint32_t block_size;
    if (block_num == 0) {
      block_size = (remaining > 65531) ? 65531 : remaining;
    } else if (block_num == 1) {
      block_size = (remaining > 32773) ? 32773 : remaining;
    } else {
      block_size = (remaining > 65535) ? 65535 : remaining;
    }
    
    bool is_final = (remaining == block_size);
    
    idat_data[idat_pos++] = is_final ? 0x01 : 0x00;
    idat_data[idat_pos++] = block_size & 0xFF;
    idat_data[idat_pos++] = (block_size >> 8) & 0xFF;
    idat_data[idat_pos++] = (~block_size) & 0xFF;
    idat_data[idat_pos++] = ((~block_size) >> 8) & 0xFF;
    
    memcpy(&idat_data[idat_pos], &raw_data[offset], block_size);
    idat_pos += block_size;
    
    offset += block_size;
    remaining -= block_size;
    block_num++;
  }
  
  // Adler-32 checksum
  uint32_t a = 1, b = 0;
  for (uint32_t i = 0; i < raw_size; i++) {
    a = (a + raw_data[i]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t adler = (b << 16) | a;
  idat_data[idat_pos++] = (adler >> 24) & 0xFF;
  idat_data[idat_pos++] = (adler >> 16) & 0xFF;
  idat_data[idat_pos++] = (adler >> 8) & 0xFF;
  idat_data[idat_pos++] = adler & 0xFF;
  
  write_png_chunk(f, "IDAT", idat_data, idat_pos);
  
  // IEND
  write_png_chunk(f, "IEND", NULL, 0);
  
  fclose(f);
}

void farm_Renderer_dispose(farm_Renderer* self) {
  self->disposed = true;
}

// Constructor wrapper functions for emit_c compatibility
