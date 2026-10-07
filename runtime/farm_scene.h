#pragma once
#include "farm_rt.h"
#include "farm_math.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct farm_Object3D farm_Object3D;
typedef struct farm_Scene farm_Scene;
typedef struct farm_PerspectiveCamera farm_PerspectiveCamera;
typedef struct farm_Mesh farm_Mesh;
typedef struct farm_BoxGeometry farm_BoxGeometry;
typedef struct farm_SphereGeometry farm_SphereGeometry;
typedef struct farm_PlaneGeometry farm_PlaneGeometry;
typedef struct farm_MeshBasicMaterial farm_MeshBasicMaterial;
typedef struct farm_MeshStandardMaterial farm_MeshStandardMaterial;
typedef struct farm_AmbientLight farm_AmbientLight;
typedef struct farm_DirectionalLight farm_DirectionalLight;
typedef struct farm_PointLight farm_PointLight;
typedef struct farm_Renderer farm_Renderer;

// Forward declarations for sync functions (used by generated code)
void sync_quaternion_from_rotation(farm_Object3D* self);
void sync_rotation_from_quaternion(farm_Object3D* self);

// Internal structures for hierarchy and geometry
typedef enum {
  FARM_OBJECT3D_TYPE_OBJECT3D = 0,
  FARM_OBJECT3D_TYPE_SCENE = 1,
  FARM_OBJECT3D_TYPE_CAMERA = 2,
  FARM_OBJECT3D_TYPE_MESH = 3,
  FARM_OBJECT3D_TYPE_AMBIENT_LIGHT = 4,
  FARM_OBJECT3D_TYPE_DIRECTIONAL_LIGHT = 5,
  FARM_OBJECT3D_TYPE_POINT_LIGHT = 6
} farm_Object3DType;

typedef struct {
  farm_Object3D** items;
  int32_t count;
  int32_t capacity;
} farm_ChildList;

typedef struct {
  double* positions;  // 3 doubles per vertex
  double* normals;    // 3 doubles per vertex
  int32_t* indices;   // triangle indices
  int32_t vertex_count;
  int32_t index_count;
  bool disposed;
} farm_GeometryData;

// Object3D - base scene graph node
struct farm_Object3D {
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  // Flags for sync
  bool rotation_dirty;
  bool quaternion_dirty;
};

// Scene - root of scene graph
struct farm_Scene {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Scene-specific
  bool hasBackground;
  farm_Color background;
};

// PerspectiveCamera
struct farm_PerspectiveCamera {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Camera-specific
  double f_fov;
  double f_aspect;
  double f_near;
  double f_far;
  farm_Matrix4 matrixWorldInverse;
  farm_Matrix4 projectionMatrix;
};

// Geometries
struct farm_BoxGeometry {
  farm_GeometryData data;
};

struct farm_SphereGeometry {
  farm_GeometryData data;
  double radius;
  int32_t widthSegments;
  int32_t heightSegments;
};

struct farm_PlaneGeometry {
  farm_GeometryData data;
  double width;
  double height;
};

// Materials
struct farm_MeshBasicMaterial {
  uint8_t material_type; // 0 for Basic
  farm_Color f_color;
  bool disposed;
};

struct farm_MeshStandardMaterial {
  uint8_t material_type; // 1 for Standard
  farm_Color f_color;
  double roughness;
  double metalness;
  bool disposed;
};

// Mesh
struct farm_Mesh {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Mesh-specific
  void* f_geometry;       // Points to one of the geometry types
  void* f_material;       // Points to one of the material types
  uint8_t geometry_type; // 0=Box, 1=Sphere, 2=Plane
  uint8_t material_type; // 0=Basic, 1=Standard
};

// Lights
// Lights
struct farm_AmbientLight {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Light-specific
  farm_Color f_color;
  double f_intensity;
};

struct farm_DirectionalLight {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Light-specific
  farm_Color f_color;
  double f_intensity;
};

struct farm_PointLight {
  // Inherited from Object3D
  farm_Object3DType type;
  farm_Vector3 f_position;
  farm_Euler f_rotation;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_scale;
  farm_Matrix4 f_matrix;
  farm_Matrix4 f_matrixWorld;
  bool matrixAutoUpdate;
  bool f_visible;
  farm_Object3D* parent;
  farm_ChildList children;
  bool rotation_dirty;
  bool quaternion_dirty;
  // Light-specific
  farm_Color f_color;
  double f_intensity;
  double f_distance;
  double f_decay;
};

// Renderer
struct farm_Renderer {
  uint8_t* framebuffer;  // RGB8
  float* depthbuffer;
  int32_t width;
  int32_t height;
  bool disposed;
};

// Object3D methods
farm_Object3D* farm_Object3D_new();
farm_Object3D* farm_Object3D_add(farm_Object3D* self, farm_Object3D* child);
farm_Object3D* farm_Object3D_remove(farm_Object3D* self, farm_Object3D* child);
farm_Object3D* farm_Object3D_addAt(farm_Object3D* self, farm_Object3D* child, int64_t index);
int64_t farm_Object3D_childCount(farm_Object3D* self);
farm_Object3D* farm_Object3D_getChild(farm_Object3D* self, int64_t index);
void farm_Object3D_updateMatrix(farm_Object3D* self);
void farm_Object3D_updateMatrixWorld(farm_Object3D* self, bool force);
void farm_Object3D_lookAt_xyz(farm_Object3D* self, double x, double y, double z);
void farm_Object3D_lookAt_v(farm_Object3D* self, farm_Vector3 target);
void farm_Object3D_setRotationFromEuler(farm_Object3D* self, farm_Euler e);
void farm_Object3D_setRotationFromQuaternion(farm_Object3D* self, farm_Quaternion q);

// Euler.order is char[4] in farm_Euler; generated code maps it to FarmString.
void farm_euler_set_order(farm_Euler* e, FarmString s);
FarmString farm_euler_order_string(const farm_Euler* e);

// Scene methods
farm_Scene* farm_Scene_new();
void farm_Scene_setBackground(farm_Scene* self, farm_Color color);
void farm_Scene_clearBackground(farm_Scene* self);

// Scene also inherits Object3D methods - cast and use
static inline farm_Object3D* farm_Scene_add(farm_Scene* self, farm_Object3D* child) {
  return farm_Object3D_add((farm_Object3D*)self, child);
}
static inline farm_Object3D* farm_Scene_remove(farm_Scene* self, farm_Object3D* child) {
  return farm_Object3D_remove((farm_Object3D*)self, child);
}
static inline farm_Object3D* farm_Scene_addAt(farm_Scene* self, farm_Object3D* child, int64_t index) {
  return farm_Object3D_addAt((farm_Object3D*)self, child, index);
}
static inline int64_t farm_Scene_childCount(farm_Scene* self) {
  return farm_Object3D_childCount((farm_Object3D*)self);
}
static inline farm_Object3D* farm_Scene_getChild(farm_Scene* self, int64_t index) {
  return farm_Object3D_getChild((farm_Object3D*)self, index);
}
static inline void farm_Scene_updateMatrixWorld(farm_Scene* self, bool force) {
  farm_Object3D_updateMatrixWorld((farm_Object3D*)self, force);
}

// PerspectiveCamera methods
farm_PerspectiveCamera* farm_PerspectiveCamera_new(double fov, double aspect, double near, double far);
void farm_PerspectiveCamera_updateProjectionMatrix(farm_PerspectiveCamera* self);
void farm_PerspectiveCamera_lookAt_xyz(farm_PerspectiveCamera* self, double x, double y, double z);
void farm_PerspectiveCamera_lookAt_v(farm_PerspectiveCamera* self, farm_Vector3 target);
void farm_PerspectiveCamera_updateMatrixWorld(farm_PerspectiveCamera* self, bool force);

// Geometry methods
farm_BoxGeometry* farm_BoxGeometry_new();
farm_BoxGeometry* farm_BoxGeometry_new_whd(double width, double height, double depth);
void farm_BoxGeometry_dispose(farm_BoxGeometry* self);

farm_SphereGeometry* farm_SphereGeometry_new();
farm_SphereGeometry* farm_SphereGeometry_new_r(double radius);
farm_SphereGeometry* farm_SphereGeometry_new_full(double radius, int64_t widthSegments, int64_t heightSegments);
void farm_SphereGeometry_dispose(farm_SphereGeometry* self);

farm_PlaneGeometry* farm_PlaneGeometry_new();
farm_PlaneGeometry* farm_PlaneGeometry_new_wh(double width, double height);
void farm_PlaneGeometry_dispose(farm_PlaneGeometry* self);

// Material methods
farm_MeshBasicMaterial* farm_MeshBasicMaterial_new();
farm_MeshBasicMaterial* farm_MeshBasicMaterial_new_color(farm_Color color);
farm_MeshBasicMaterial* farm_MeshBasicMaterial_new_hex(int64_t hex);
void farm_MeshBasicMaterial_dispose(farm_MeshBasicMaterial* self);

farm_MeshStandardMaterial* farm_MeshStandardMaterial_new();
farm_MeshStandardMaterial* farm_MeshStandardMaterial_new_color(farm_Color color);
farm_MeshStandardMaterial* farm_MeshStandardMaterial_new_hex(int64_t hex);
void farm_MeshStandardMaterial__setRoughness(farm_MeshStandardMaterial* self, double r);
void farm_MeshStandardMaterial__setMetalness(farm_MeshStandardMaterial* self, double m);
void farm_MeshStandardMaterial_set(farm_MeshStandardMaterial* self, farm_Color color);
void farm_MeshStandardMaterial_setRoughness(farm_MeshStandardMaterial* self, double r);
void farm_MeshStandardMaterial_setMetalness(farm_MeshStandardMaterial* self, double m);
void farm_MeshStandardMaterial_dispose(farm_MeshStandardMaterial* self);

// Mesh methods
farm_Mesh* farm_Mesh_new(void* geometry, void* material);
farm_Mesh* farm_Mesh_new_box(farm_BoxGeometry* geometry, void* material, uint8_t mat_type);
farm_Mesh* farm_Mesh_new_sphere(farm_SphereGeometry* geometry, void* material, uint8_t mat_type);
farm_Mesh* farm_Mesh_new_plane(farm_PlaneGeometry* geometry, void* material, uint8_t mat_type);

// Light methods
farm_AmbientLight* farm_AmbientLight_new();
farm_AmbientLight* farm_AmbientLight_new_hex(int64_t hex);
farm_AmbientLight* farm_AmbientLight_new_hex_i(int64_t hex, double intensity);
farm_AmbientLight* farm_AmbientLight_new_color_i(farm_Color color, double intensity);

farm_DirectionalLight* farm_DirectionalLight_new();
farm_DirectionalLight* farm_DirectionalLight_new_hex(int64_t hex);
farm_DirectionalLight* farm_DirectionalLight_new_hex_i(int64_t hex, double intensity);
farm_DirectionalLight* farm_DirectionalLight_new_color_i(farm_Color color, double intensity);

farm_PointLight* farm_PointLight_new();
farm_PointLight* farm_PointLight_new_hex(int64_t hex);
farm_PointLight* farm_PointLight_new_hex_i(int64_t hex, double intensity);
farm_PointLight* farm_PointLight_new_color_i(farm_Color color, double intensity);
farm_PointLight* farm_PointLight_new_full(farm_Color color, double intensity, double distance, double decay);
farm_PointLight* farm_PointLight_new_hex_full(int64_t hex, double intensity, double distance, double decay);

// Renderer methods
farm_Renderer* farm_Renderer_new();
farm_Renderer* farm_Renderer_new_wh(int64_t width, int64_t height);
void farm_Renderer_setSize(farm_Renderer* self, int64_t width, int64_t height);
void farm_Renderer_render(farm_Renderer* self, farm_Scene* scene, farm_PerspectiveCamera* camera);
void farm_Renderer_savePNG(farm_Renderer* self, FarmString path);
void farm_Renderer_dispose(farm_Renderer* self);

#ifdef __cplusplus
}
#endif

