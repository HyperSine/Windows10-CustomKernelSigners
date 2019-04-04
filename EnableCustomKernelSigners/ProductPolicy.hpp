#pragma once
#include <stddef.h>
#include <stdint.h>
#include <variant>
#include <vector>
#include <string>
#include <stdexcept>
#include <windows.h>

class PolicyValue {
    friend class ProductPolicyParser;
public:
    enum class TypeLabel {
        String = REG_SZ,
        Binary = REG_BINARY,
        UInt32 = REG_DWORD
    };

    using TypeOfString = std::wstring;
    using TypeOfBinary = std::vector<uint8_t>;
    using TypeOfUInt32 = uint32_t;
private:
    TypeLabel Type;
    std::wstring Name;
    std::variant<TypeOfString, TypeOfBinary, TypeOfUInt32> Data;
public:
    uint32_t Flags;
    uint32_t Reserved;

    template<typename __Type>
    using ConstructAs = std::in_place_type_t<__Type>;

    template<typename __DataType>
    PolicyValue(ConstructAs<__DataType> Hint);

    template<>
    PolicyValue(ConstructAs<TypeOfString> Hint) :
        Type(TypeLabel::String),
        Data(Hint) {}

    template<>
    PolicyValue(ConstructAs<TypeOfBinary> Hint) :
        Type(TypeLabel::Binary),
        Data(Hint) {}

    template<>
    PolicyValue(ConstructAs<TypeOfUInt32> Hint) :
        Type(TypeLabel::UInt32),
        Data(Hint) {}

    TypeLabel GetType() const noexcept {
        return Type;
    }

    const std::wstring& GetName() const noexcept {
        return Name;
    }

    template<typename __Type>
    __Type& GetData() {
        return std::get<__Type>(Data);
    }

    template<typename __Type>
    const __Type& GetData() const {
        return std::get<__Type>(Data);
    }
};

class ProductPolicy {
    friend class ProductPolicyParser;
private:
    std::vector<PolicyValue> _Policies;
public:
    static constexpr size_t InvalidPos = -1;

    size_t NumberOfPolicies() const noexcept;

    //void Add(const PolicyValue& Policy);
    size_t FindPolicy(const std::wstring& Regex, size_t StartPos = 0);

    //void Remove(size_t PolicyIndex);
    //void Remove(const std::wstring& PolicyName);

    PolicyValue& operator[](size_t Index);
    PolicyValue& operator[](const std::wstring& Name);

    const PolicyValue& operator[](size_t Index) const;
    const PolicyValue& operator[](const std::wstring& Name) const;
};

