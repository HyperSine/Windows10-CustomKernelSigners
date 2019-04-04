#pragma once
#include <type_traits>

template<typename __ResourceTraits>
class OwnedResource {
public:
    using HandleType = typename __ResourceTraits::HandleType;
private:
    HandleType _HandleValue;
public:

    OwnedResource() noexcept :
        _HandleValue(__ResourceTraits::InvalidValue) {}

    explicit OwnedResource(const HandleType& Handle) noexcept :
        _HandleValue(Handle) {}

    //
    // Copy constructor is not allowed
    //
    OwnedResource(const OwnedResource<__ResourceTraits>& Other) = delete;

    OwnedResource(OwnedResource<__ResourceTraits>&& Other) noexcept :
        _HandleValue(Other._HandleValue) 
    {
        Other._HandleValue = __ResourceTraits::InvalidValue;
    }

    //
    // Copy assignment is not allowed
    //
    OwnedResource<__ResourceTraits>& operator=(const OwnedResource<__ResourceTraits>& Other) = delete;

    OwnedResource<__ResourceTraits>& operator=(OwnedResource<__ResourceTraits>&& Other) noexcept {
        _HandleValue = Other._HandleValue;
        Other._HandleValue = __ResourceTraits::InvalidValue;
        return *this;
    }

    template<typename = typename std::enable_if<std::is_pointer<HandleType>::value>::type>
    HandleType operator->() const noexcept {
        return _HandleValue;
    }

    operator HandleType() const noexcept {
        return _HandleValue;
    }

    //
    // Check if handle is a valid handle
    //
    bool IsValid() const noexcept {
        return _HandleValue != __ResourceTraits::InvalidValue;
    }

    //
    // Retrieve handle
    //
    HandleType Get() const noexcept {
        return _HandleValue;
    }

    //
    // Get address of _HandleValue
    //
    HandleType* GetAddress() noexcept {
        return &_HandleValue;
    }

    //
    // A const version for GetAddress
    //
    const HandleType* GetAddress() const noexcept {
        return &_HandleValue;
    }

    template<bool __NoRelease = false>
    void TakeOver(const HandleType& Handle) noexcept {
        if constexpr (__NoRelease == false) {
            if (_HandleValue != __ResourceTraits::InvalidValue)
                __ResourceTraits::Releasor(_HandleValue);
        }
        _HandleValue = Handle;
    }

    template<bool __NoRelease = true>
    void Abandon() noexcept {
        if constexpr (__NoRelease == false) {
            if (_HandleValue != __ResourceTraits::InvalidValue)
                __ResourceTraits::Releasor(_HandleValue);
        }
        _HandleValue = __ResourceTraits::InvalidValue;
    }

    //
    // Force release
    //
    void Release() {
        if (_HandleValue != __ResourceTraits::InvalidValue) {
            __ResourceTraits::Releasor(_HandleValue);
            _HandleValue = __ResourceTraits::InvalidValue;
        }
    }

    ~OwnedResource() {
        if (_HandleValue != __ResourceTraits::InvalidValue) {
            __ResourceTraits::Releasor(_HandleValue);
            _HandleValue = __ResourceTraits::InvalidValue;
        }
    }
};

template<typename __ClassType>
struct CppObjectTraits {
    using HandleType = __ClassType * ;
    static inline const HandleType InvalidValue = nullptr;
    static inline void Releasor(HandleType pObject) {
        delete pObject;
    }
};

template<typename __ClassType>
struct CppDynamicArrayTraits {
    using HandleType = __ClassType * ;
    static inline const HandleType InvalidValue = nullptr;
    static inline void Releasor(HandleType ArrayPtr) {
        delete[] ArrayPtr;
    }
};
