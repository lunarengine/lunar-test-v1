#pragma once
#include "bootstrap/Instance.h"
#include <memory>
#include <string>
#include <unordered_map>

struct Service : Instance {
    // Use construct-on-first-use idiom to avoid static destruction order fiasco
    static std::unordered_map<std::string, std::shared_ptr<Service>>& GetRegistry();

    explicit Service(std::string name, InstanceClass cls = InstanceClass::Unknown);
    ~Service() override;

    // mark as service so base guards apply
    bool IsService() const override { return true; }

    // lookup or create via Instance::New
    static std::shared_ptr<Service> Get(const std::string& name);
    static std::shared_ptr<Service> Create(const std::string& name); 
};