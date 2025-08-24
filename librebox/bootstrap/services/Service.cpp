#include "bootstrap/services/Service.h"
#include "bootstrap/Game.h"              // for g_game
#include "core/logging/Logging.h"

// Construct-on-first-use idiom to prevent static destruction order fiasco
std::unordered_map<std::string, std::shared_ptr<Service>>& Service::GetRegistry() {
    // Use a static local variable with construct-on-first-use idiom
    // This ensures the registry outlives any Service objects
    static std::unordered_map<std::string, std::shared_ptr<Service>>* registry = 
        new std::unordered_map<std::string, std::shared_ptr<Service>>();
    return *registry;
}

Service::Service(std::string name, InstanceClass cls)
    : Instance(std::move(name), cls) {
    // do not add to registry here; we do it when first accessed
}

Service::~Service() {
    // Safe access to registry - the registry is never destroyed due to construct-on-first-use with new
    auto& registry = GetRegistry();
    registry.erase(Name);
}

std::shared_ptr<Service> Service::Get(const std::string& name) {
    auto& registry = GetRegistry();
    auto it = registry.find(name);
    return it == registry.end() ? nullptr : it->second;
}

std::shared_ptr<Service> Service::Create(const std::string& name){
    if (auto s = Get(name)) return nullptr;               // or throw
    auto inst = Instance::New(name);
    auto svc  = std::dynamic_pointer_cast<Service>(inst);
    if (!svc) return nullptr;
    if (g_game) svc->SetParent(g_game);
    auto& registry = GetRegistry();
    registry[name] = svc;
    LOGI("Service created '%s'", name.c_str());
    return svc;
}
