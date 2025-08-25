#include "bootstrap/Instance.h"
#include "core/logging/Logging.h"
#include <algorithm>
#include <unordered_map>

// -------- ctors --------
Instance::Instance(std::string name, InstanceClass c) : Name(std::move(name)), Class(c) {}
Instance::~Instance() = default;

// instance classname mapping
static const char* ToClassName(InstanceClass c) {
    switch (c) {
        case InstanceClass::Game:        return "Game";
        case InstanceClass::Workspace:   return "Workspace";
        case InstanceClass::Part:        return "Part";
        case InstanceClass::Script:      return "Script";
        case InstanceClass::LocalScript: return "LocalScript";
        case InstanceClass::Folder:      return "Folder";
        case InstanceClass::Camera:      return "Camera";
        case InstanceClass::RunService:  return "RunService";
        case InstanceClass::Lighting:    return "Lighting";
        default:                         return "Unknown";
    }
}

// -------- attributes --------
void Instance::SetAttribute(const std::string& name, const Attribute& value) {
    if (name.empty()) return;
    Attributes[name] = value;
    LOGI("SetAttribute: %s.%s", Name.c_str(), name.c_str());
}
std::optional<Attribute> Instance::GetAttribute(const std::string& name) const {
    auto it = Attributes.find(name);
    if (it == Attributes.end()) return std::nullopt;
    return it->second;
}

// -------- tiny signal system --------
size_t Instance::OnChildAdded(CB cb){ auto id=nextId++; childAdded_[id]=std::move(cb); return id; }
size_t Instance::OnChildRemoved(CB cb){ auto id=nextId++; childRemoved_[id]=std::move(cb); return id; }
size_t Instance::OnDescendantAdded(CB cb){ auto id=nextId++; descAdded_[id]=std::move(cb); return id; }
size_t Instance::OnDescendantRemoved(CB cb){ auto id=nextId++; descRemoved_[id]=std::move(cb); return id; }
void   Instance::Disconnect(size_t id){
    childAdded_.erase(id); childRemoved_.erase(id); descAdded_.erase(id); descRemoved_.erase(id);
}
void Instance::fireChildAdded(const std::shared_ptr<Instance>& c){ for(auto& kv:childAdded_) kv.second(c); }
void Instance::fireChildRemoved(const std::shared_ptr<Instance>& c){ for(auto& kv:childRemoved_) kv.second(c); }
void Instance::fireDescendantAdded(const std::shared_ptr<Instance>& c){ for(auto& kv:descAdded_) kv.second(c); }
void Instance::fireDescendantRemoved(const std::shared_ptr<Instance>& c){ for(auto& kv:descRemoved_) kv.second(c); }

// helper: apply f to node and all descendants
static void forEachDesc(const std::shared_ptr<Instance>& n,
                        const std::function<void(const std::shared_ptr<Instance>&)>& f){
    if (!n) return;
    f(n);
    for (auto& ch : n->Children) if (ch) forEachDesc(ch, f);
}

// -------- parenting --------
void Instance::SetParent(const std::shared_ptr<Instance>& parent) {
    auto self = shared_from_this();

    if (IsService()) {
        if (!Parent.expired()) return;                       // lock after first set
        if (!(parent && parent->Class == InstanceClass::Game)) return;
    }

    // detach from old
    if (auto old = Parent.lock()) {
        old->Children.erase(std::remove(old->Children.begin(), old->Children.end(), self), old->Children.end());
        auto it = old->ChildrenByName.find(Name);
        if (it != old->ChildrenByName.end() && it->second.get() == this) old->ChildrenByName.erase(it);

        // direct child removed
        old->fireChildRemoved(self);
        // subtree: notify all ancestors of old
        for (auto a = old; a; a = a->Parent.lock()) {
            forEachDesc(self, [&](const std::shared_ptr<Instance>& d){ a->fireDescendantRemoved(d); });
        }
    }

    Parent = parent;

    // attach to new
    if (parent) {
        parent->Children.push_back(self);
        parent->ChildrenByName[Name] = self;

        // direct child added
        parent->fireChildAdded(self);
        // subtree: notify all ancestors of new
        for (auto a = parent; a; a = a->Parent.lock()) {
            forEachDesc(self, [&](const std::shared_ptr<Instance>& d){ a->fireDescendantAdded(d); });
        }
    }
}

// -------- destroy --------
void Instance::Destroy() {
    if (!Alive) return;
    Alive = false;
    LOGI("Instance::Destroy '%s'", Name.c_str());

    auto self = shared_from_this();

    // notify and detach from parent first
    if (auto p = Parent.lock()) {
        p->Children.erase(std::remove(p->Children.begin(), p->Children.end(), self), p->Children.end());
        auto it = p->ChildrenByName.find(Name);
        if (it != p->ChildrenByName.end() && it->second.get() == this) p->ChildrenByName.erase(it);

        p->fireChildRemoved(self);
        for (auto a = p; a; a = a->Parent.lock()) {
            forEachDesc(self, [&](const std::shared_ptr<Instance>& d){ a->fireDescendantRemoved(d); });
        }
    }
    Parent.reset();

    // destroy children - snapshot first to avoid iterator invalidation
    auto childrenSnapshot = Children;
    for (auto& c : childrenSnapshot) {
        if (c && c->Alive) c->Destroy();
    }
    Children.clear();
    ChildrenByName.clear();
    Attributes.clear();
}

void Instance::LegacyFunctionRemove() {
    if (!Alive) return; 
    SetParent(nullptr);
}

std::shared_ptr<Instance> Instance::Clone() const {
    if (IsService()) return nullptr;
    using Map = CloneMap;
    Map map;

    // Pass 1: build an isomorphic tree with copied derived fields.
    std::function<std::shared_ptr<Instance>(const Instance*)> build =
    [&](const Instance* src) -> std::shared_ptr<Instance> {
        if (!src || !src->Alive) return nullptr;

        auto it = types().find(src->GetClassName());
        if (it == types().end()) return nullptr;

        auto dst = (it->second.factory)();
        if (!dst) return nullptr;

        // Copy all derived fields
        (it->second.copier)(src, dst.get());

        // Sanitize base
        dst->Parent.reset();
        dst->Children.clear();
        dst->ChildrenByName.clear();
        dst->childAdded_.clear();
        dst->childRemoved_.clear();
        dst->descAdded_.clear();
        dst->descRemoved_.clear();
        dst->nextId = 1;

        // Reapply canonical base values
        dst->Name       = src->Name;
        dst->Class      = src->Class;
        dst->Alive      = true;
        dst->Attributes = src->Attributes;

        map.emplace(src, dst);

        // Children without firing signals
        for (const auto& ch : src->Children) {
            if (!ch || !ch->Alive) continue;
            auto cc = build(ch.get());
            if (!cc) continue;
            cc->Parent = dst;
            dst->Children.push_back(cc);
            dst->ChildrenByName[cc->Name] = cc;
        }
        return dst;
    };

    auto root = build(this);
    if (!root) return nullptr;

    // Pass 2: fix intra-tree references in derived data.
    for (auto& kv : map) kv.second->RemapReferences(map);

    return root;
}

void Instance::SetName(const std::string& newName) {
    if (Name == newName) return;
    if (auto p = Parent.lock()) {
        auto it = p->ChildrenByName.find(Name);
        if (it != p->ChildrenByName.end() && it->second.get() == this) {
            p->ChildrenByName.erase(it);
        }
        p->ChildrenByName[newName] = shared_from_this();
    }
    Name = newName;
}

std::string Instance::GetFullName() const {
    std::string full = Name;
    auto p = Parent.lock();
    while (p) {
        if (p->Class == InstanceClass::Game) break; // don't include DataModel/"game"
        full = p->Name + "." + full;
        p = p->Parent.lock();
    }
    return full;
}

std::string Instance::GetClassName() const {
    return ToClassName(Class);
}

bool Instance::IsA(const std::string& className) const {
    if (className == "Instance") return true;
    if (className == ToClassName(Class)) return true;

    // simple inheritance/aliases
    switch (Class) {
        case InstanceClass::LocalScript:
        case InstanceClass::Script:
            if (className == "BaseScript" || className == "LuaSourceContainer") return true;
            break;
        case InstanceClass::Game:
            if (className == "DataModel") return true; // Roblox alias
            break;
        default:
            break;
    }
    return false;
}

std::vector<std::shared_ptr<Instance>> Instance::GetChildren() const {
    std::vector<std::shared_ptr<Instance>> out;
    out.reserve(Children.size());
    for (const auto& c : Children)
        if (c && c->Alive) out.push_back(c);
    return out;
}

std::vector<std::shared_ptr<Instance>> Instance::GetDescendants() const {
    std::vector<std::shared_ptr<Instance>> out;
    std::vector<std::shared_ptr<Instance>> stack;

    for (const auto& c : Children)
        if (c && c->Alive) stack.push_back(c);

    while (!stack.empty()) {
        auto n = stack.back(); stack.pop_back();
        out.push_back(n);
        for (const auto& ch : n->Children)
            if (ch && ch->Alive) stack.push_back(ch);
    }
    return out;
}

std::shared_ptr<Instance> Instance::FindFirstChildOfClass(const std::string& className) const {
    for (const auto& c : Children)
        if (c && c->Alive && c->GetClassName() == className) return c;
    return nullptr;
}

std::shared_ptr<Instance> Instance::FindFirstChildWhichIsA(const std::string& className) const {
    for (const auto& c : Children)
        if (c && c->Alive && c->IsA(className)) return c;
    return nullptr;
}

std::shared_ptr<Instance> Instance::FindFirstAncestor(const std::string& name) const {
    for (auto a = Parent.lock(); a; a = a->Parent.lock())
        if (a->Alive && a->Name == name) return a;
    return nullptr;
}

std::shared_ptr<Instance> Instance::FindFirstAncestorOfClass(const std::string& className) const {
    for (auto a = Parent.lock(); a; a = a->Parent.lock())
        if (a->Alive && a->GetClassName() == className) return a;
    return nullptr;
}

std::shared_ptr<Instance> Instance::FindFirstAncestorWhichIsA(const std::string& className) const {
    for (auto a = Parent.lock(); a; a = a->Parent.lock())
        if (a->Alive && a->IsA(className)) return a;
    return nullptr;
}

bool Instance::IsDescendantOf(const std::shared_ptr<Instance>& other) const {
    if (!other) return false;
    for (auto a = Parent.lock(); a; a = a->Parent.lock())
        if (a.get() == other.get()) return true;
    return false;
}

bool Instance::IsAncestorOf(const std::shared_ptr<Instance>& other) const {
    if (!other) return false;
    for (auto a = other->Parent.lock(); a; a = a->Parent.lock())
        if (a.get() == this) return true;
    return false;
}

void Instance::ClearAllChildren() {
    // Snapshot first; Destroy() mutates parent->Children.
    auto cur = Children;
    for (auto& c : cur) {
        if (c && c->Alive) c->Destroy(); // recursive by design
    }
    // Ensure containers are empty even if a child skipped notifications.
    Children.clear();
    ChildrenByName.clear();
}

// -------- factory --------
// static std::unordered_map<std::string, Instance::Factory>& factories() {
//     static std::unordered_map<std::string, Instance::Factory> f;
//     return f;
// }
// Instance::Registrar::Registrar(const char* type, Factory func) {
//     factories().emplace(type, std::move(func));
// }
// std::shared_ptr<Instance> Instance::New(const std::string& typeName) {
//     LOGI("Instance::New('%s')", typeName.c_str());
//     auto it = factories().find(typeName);
//     if (it != factories().end()) return (it->second)();
//     LOGW("Instance::New: unknown type '%s'", typeName.c_str());
//     return nullptr;
// }
// -------- factory/registry --------
std::unordered_map<std::string, Instance::TypeInfo>& Instance::types()
{
    static std::unordered_map<std::string, Instance::TypeInfo> t;
    return t;
}

std::shared_ptr<Instance> Instance::New(const std::string& typeName) {
    LOGI("Instance::New('%s')", typeName.c_str());
    auto it = types().find(typeName);
    if (it != types().end()) return (it->second.factory)();
    LOGW("Instance::New: unknown type '%s'", typeName.c_str());
    return nullptr;
}