#pragma once
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <functional>
#include <type_traits>
#include <utility>
#include <stdlib>

// Raylib
#include <raylib.h>

// Forward declare Lua to avoid coupling headers to Lua includes
struct lua_State;

enum class InstanceClass { Game, Workspace, Part, Script, LocalScript, Folder, Camera, RunService, Lighting, Unknown };
using Attribute = std::variant<bool,double,std::string,::Vector3,::Color>;

struct Instance : std::enable_shared_from_this<Instance> {
    // -------- core state --------
    std::string Name;
    InstanceClass Class{ InstanceClass::Unknown };
    std::weak_ptr<Instance> Parent;
    std::vector<std::shared_ptr<Instance>> Children;
    std::unordered_map<std::string, std::shared_ptr<Instance>> ChildrenByName;
    bool Alive{ true };

    // Attributes
    std::unordered_map<std::string, Attribute> Attributes;

    // -------- ctor/dtor --------
    Instance(std::string name, InstanceClass c);
    virtual ~Instance();

    // -------- lifetime --------
    virtual void Destroy();
    void SetParent(const std::shared_ptr<Instance>& parent);
    void LegacyFunctionRemove();

    // -------- queries --------
    std::string GetClassName() const;
    bool IsA(const std::string& className) const;
    void SetName(const std::string& newName);
    std::string GetFullName() const;

    // -------- queries --------
    std::shared_ptr<Instance> FindFirstChild(const std::string& name) {
        auto it = ChildrenByName.find(name);
        return it == ChildrenByName.end() ? nullptr : it->second;
    }
    std::shared_ptr<Instance> WaitForChild(const std::string& name) {
        auto it = ChildrenByName.find(name);
        if it ~= ChildrenByName.end() {
            return it
        } else {
            while (it == ChildrenByName.end()) {
                sleep(1);
                it = ChildrenByName.find(name);
            }
        }
        
        return it;
    }
    std::shared_ptr<Instance> FindFirstChildOfClass(const std::string& className) const;
    std::shared_ptr<Instance> FindFirstChildWhichIsA(const std::string& className) const;
    std::shared_ptr<Instance> FindFirstAncestor(const std::string& name) const;
    std::shared_ptr<Instance> FindFirstAncestorOfClass(const std::string& className) const;
    std::shared_ptr<Instance> FindFirstAncestorWhichIsA(const std::string& className) const;

    std::vector<std::shared_ptr<Instance>> GetChildren() const;
    std::vector<std::shared_ptr<Instance>> GetDescendants() const;

    bool IsDescendantOf(const std::shared_ptr<Instance>& other) const;
    bool IsAncestorOf(const std::shared_ptr<Instance>& other) const;
    void ClearAllChildren();

    // -------- attributes API --------
    void SetAttribute(const std::string& name, const Attribute& value);
    std::optional<Attribute> GetAttribute(const std::string& name) const;
    const std::unordered_map<std::string, Attribute>& GetAttributes() const { return Attributes; }

    // -------- signals --------
    using CB = std::function<void(const std::shared_ptr<Instance>&)>;
    size_t OnChildAdded(CB cb);
    size_t OnChildRemoved(CB cb);
    size_t OnDescendantAdded(CB cb);
    size_t OnDescendantRemoved(CB cb);
    void   Disconnect(size_t id);

    // -------- cloning --------
    using CloneMap = std::unordered_map<const Instance*, std::shared_ptr<Instance>>;

    std::shared_ptr<Instance> Clone() const;
    virtual void RemapReferences(const CloneMap&) {}
    virtual bool IsService() const { return false; }
    
    // -------- Lua property hooks (object-specific, but out of ScriptingAPI) --------
    // Return true if handled. For reads, you must push a Lua value onto the stack.
    virtual bool LuaGet(lua_State* L, const char* key) const { (void)L; (void)key; return false; }
    // For writes, read the value at 'valueIndex'.
    virtual bool LuaSet(lua_State* L, const char* key, int valueIndex) { (void)L; (void)key; (void)valueIndex; return false; }

protected:
    // Helpers for RemapReferences implementations
    template<class T>
    static std::shared_ptr<T> RemapShared(const CloneMap& m, std::shared_ptr<T> p){
        if (!p) return nullptr;
        auto it = m.find(p.get());
        if (it == m.end()) return p;
        return std::dynamic_pointer_cast<T>(it->second);
    }
    template<class T>
    static std::weak_ptr<T> RemapWeak(const CloneMap& m, std::weak_ptr<T> p){
        auto sp = p.lock();
        auto sp2 = RemapShared<T>(m, sp);
        return std::weak_ptr<T>(sp2);
    }
    template<class T>
    static T* RemapRaw(const CloneMap& m, T* p){
        if (!p) return nullptr;
        auto it = m.find(p);
        if (it == m.end()) return p;
        return dynamic_cast<T*>(it->second.get());
    }

public:
    // -------- factory/registry --------
    using Factory = std::function<std::shared_ptr<Instance>()>;
    using Copier  = std::function<void(const Instance* src, Instance* dst)>;

    struct TypeInfo {
        Factory factory;
        Copier  copier;
    };

    static std::shared_ptr<Instance> New(const std::string& typeName);

    struct Registrar {
        template <class F>
        Registrar(const char* type, F&& f) {
            using Ret = std::invoke_result_t<F>;
            static_assert(std::is_same_v<Ret, std::shared_ptr<typename Ret::element_type>>);
            using Derived = typename Ret::element_type;
            static_assert(std::is_base_of_v<Instance, Derived>);

            TypeInfo ti;
            ti.factory = Factory(std::forward<F>(f));
            ti.copier  = [](const Instance* s, Instance* d) {
                auto* sd = static_cast<const Derived*>(s);
                auto* dd = static_cast<Derived*>(d);
                *dd = *sd;
            };
            types().emplace(type, std::move(ti));
        }
    };

private:
    size_t nextId{1};
    std::unordered_map<size_t, CB> childAdded_, childRemoved_, descAdded_, descRemoved_;

    void fireChildAdded(const std::shared_ptr<Instance>& c);
    void fireChildRemoved(const std::shared_ptr<Instance>& c);
    void fireDescendantAdded(const std::shared_ptr<Instance>& c);
    void fireDescendantRemoved(const std::shared_ptr<Instance>& c);

    static std::unordered_map<std::string, TypeInfo>& types();
};
