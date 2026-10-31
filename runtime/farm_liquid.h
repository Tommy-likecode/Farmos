#pragma once
#include "farm_rt.h"
#include "farm_scene.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { FARM_LIQUID_BACKEND_CPU = 0 };

typedef struct farm_LiquidWorld farm_LiquidWorld;
typedef struct farm_HeightField farm_HeightField;

farm_LiquidWorld* farm_LiquidWorld_new(void);
void farm_LiquidWorld_setFixedTimeStep(farm_LiquidWorld* self, double dt);
void farm_LiquidWorld_addHeightField(farm_LiquidWorld* self, farm_HeightField* field);
void farm_LiquidWorld_removeHeightField(farm_LiquidWorld* self, farm_HeightField* field);
void farm_LiquidWorld_step(farm_LiquidWorld* self);
void farm_LiquidWorld_dispose(farm_LiquidWorld* self);

farm_HeightField* farm_HeightField_new(int64_t nx, int64_t nz, double hx, double hz);
void farm_HeightField_setHeight(farm_HeightField* self, int64_t i, int64_t j, double h);
double farm_HeightField_getHeight(farm_HeightField* self, int64_t i, int64_t j);
void farm_HeightField_linkMesh(farm_HeightField* self, farm_Mesh* mesh);
void farm_HeightField_clearMeshLink(farm_HeightField* self);

#ifdef __cplusplus
}
#endif
