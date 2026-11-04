#pragma once
#include <vector>
#include <memory>
struct SyEntity;
class SceneEditService3D {public:virtual ~SceneEditService3D()=default;
void addEntities(std::vector<std::unique_ptr<SyEntity>>&&,const char*){}};
