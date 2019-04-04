#pragma once
#include "ProductPolicy.hpp"

//
// Reference:
// 1. https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/slmem/productpolicy.htm
//

class ProductPolicyParser {
    friend class ProductPolicy;
private:
    static inline const uint32_t EndMarker = 0x45;

    ProductPolicyParser() = delete;
    ProductPolicyParser(const ProductPolicyParser&) = delete;
    ProductPolicyParser(ProductPolicyParser&&) = delete;
    ProductPolicyParser& operator=(const ProductPolicyParser&) = delete;
    ProductPolicyParser& operator=(ProductPolicyParser&&) = delete;

    template<typename __Type>
    static const __Type* AsPtrOf(const void* p) {
        return reinterpret_cast<const __Type*>(p);
    }
    
    template<uint32_t __Pos>
    static void WriteIn(std::vector<uint8_t>& buf, const void* p, size_t s) {
        if constexpr (__Pos == 'BEGN') {
            auto pb = reinterpret_cast<const uint8_t*>(p);
            buf.insert(buf.begin(), pb, pb + s);
        } else if constexpr (__Pos == 'END') {
            auto pb = reinterpret_cast<const uint8_t*>(p);
            buf.insert(buf.end(), pb, pb + s);
        } else {
            static_assert(__Pos == 'BEGN' || __Pos == 'END');
        }
    }

    template<uint32_t __Pos>
    static void WriteNullBytesIn(std::vector<uint8_t>& buf, size_t s) {
        if constexpr (__Pos == 'BEGN') {
            buf.insert(buf.begin(), s, 0);
        } else if constexpr (__Pos == 'END') {
            buf.insert(buf.end(), s, 0);
        } else {
            static_assert(__Pos == 'BEGN' || __Pos == 'END');
        }
    }

    struct PPBinaryHeader {
        uint32_t TotalSize;
        uint32_t DataSize;
        uint32_t EndMarkerSize;
        uint32_t Reserved;
        uint32_t Revision;
    };

    struct PPBinaryValue {
        uint16_t TotalSize;
        uint16_t NameSize;
        uint16_t DataType;
        uint16_t DataSize;
        uint32_t Flags;
        uint32_t Reserved;
    };

    static_assert(sizeof(PPBinaryHeader) == 20);
    static_assert(sizeof(PPBinaryValue) == 16);
public:
    static ProductPolicy FromBinary(const std::vector<uint8_t>& Binary);
    static std::vector<uint8_t> ToBinary(const ProductPolicy& PolicyObject);
};
