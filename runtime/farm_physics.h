#pragma once
#include "farm_rt.h"
#include "farm_math.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct farm_Object3D farm_Object3D;
typedef struct farm_World farm_World;
typedef struct farm_Collider farm_Collider;
typedef struct farm_Collider farm_SphereCollider;
typedef struct farm_Collider farm_BoxCollider;
typedef struct farm_Collider farm_PlaneCollider;

enum {
  FARM_BODY_DYNAMIC = 0,
  FARM_BODY_STATIC = 1,
  FARM_BODY_KINEMATIC = 2
};

enum {
  FARM_SHAPE_SPHERE = 0,
  FARM_SHAPE_BOX = 1,
  FARM_SHAPE_PLANE = 2
};

struct farm_RigidBody {
  farm_Vector3 f_position;
  farm_Quaternion f_quaternion;
  farm_Vector3 f_linearVelocity;
  farm_Vector3 f_angularVelocity;
  int64_t f_id;
  int64_t f_bodyType;
  double mass;
  double restitution;
  double friction;
  double lin_damp;
  double ang_damp;
  farm_Collider* collider;
  farm_Object3D* object;
  farm_World* world;
  double inv_mass;
  farm_Vector3 inv_inertia_local;
};

typedef struct farm_RigidBody farm_RigidBody;

farm_World* farm_World_new(void);
void farm_World_setGravity(farm_World* self, farm_Vector3 v);
void farm_World_setFixedTimeStep(farm_World* self, double dt);
void farm_World_add(farm_World* self, farm_RigidBody* body);
void farm_World_remove(farm_World* self, farm_RigidBody* body);
int64_t farm_World_bodyCount(farm_World* self);
void farm_World_step(farm_World* self);
void farm_World_dispose(farm_World* self);

farm_RigidBody* farm_RigidBody_new(int64_t bodyType);
void farm_RigidBody_setMass(farm_RigidBody* self, double m);
void farm_RigidBody_setRestitution(farm_RigidBody* self, double e);
void farm_RigidBody_setFriction(farm_RigidBody* self, double f);
void farm_RigidBody_setLinearDamping(farm_RigidBody* self, double d);
void farm_RigidBody_setAngularDamping(farm_RigidBody* self, double d);
void farm_RigidBody_setCollider(farm_RigidBody* self, farm_Collider* c);
void farm_RigidBody_setObject(farm_RigidBody* self, farm_Object3D* obj);
void farm_RigidBody_clearObject(farm_RigidBody* self);
int64_t farm_RigidBody_getBodyType(farm_RigidBody* self);

farm_SphereCollider* farm_SphereCollider_new(double radius);
farm_BoxCollider* farm_BoxCollider_new_xyz(double hx, double hy, double hz);
farm_BoxCollider* farm_BoxCollider_new_v(farm_Vector3 halfExtents);
farm_PlaneCollider* farm_PlaneCollider_new(void);

#ifdef __cplusplus
}
#endif
