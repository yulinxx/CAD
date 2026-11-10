#pragma once
#include <vector>
#include <memory>
struct AprEntity;
class SceneEditService3D {public:virtual ~SceneEditService3D()=default;
void addEntities(std::vector<std::unique_ptr<AprEntity>>&&,const char*){}};
