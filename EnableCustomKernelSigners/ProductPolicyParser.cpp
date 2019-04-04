#include "ProductPolicyParser.hpp"

ProductPolicy ProductPolicyParser::FromBinary(const std::vector<uint8_t>& Binary) {
    ProductPolicy Result;
    const uint8_t* pb = Binary.data();
    const uint8_t* EndOfpb = Binary.data() + Binary.size();

    if (pb + sizeof(PPBinaryHeader) < EndOfpb) {
        auto HeaderPtr = AsPtrOf<PPBinaryHeader>(pb);

        if (HeaderPtr->TotalSize != Binary.size())
            throw std::invalid_argument("HeaderPtr->TotalSize is incorrect.");

        if (HeaderPtr->EndMarkerSize != sizeof(uint32_t))
            throw std::invalid_argument("HeaderPtr->EndMarkerSize is incorrect.");

        if (HeaderPtr->DataSize + HeaderPtr->EndMarkerSize + sizeof(PPBinaryHeader) != HeaderPtr->TotalSize)
            throw std::invalid_argument("HeaderPtr->DataSize is incorrect.");

        if (HeaderPtr->Revision != 1)
            throw std::invalid_argument("HeaderPtr->Revision is incorrect.");

        pb += sizeof(PPBinaryHeader);
        EndOfpb -= sizeof(uint32_t);
    } else {
        throw std::invalid_argument("Invalid binary data.");
    }

    if (*AsPtrOf<uint32_t>(EndOfpb) != EndMarker)
        throw std::invalid_argument("EndMarker is incorrect.");

    while (pb < EndOfpb) {
        auto pVal = AsPtrOf<PPBinaryValue>(pb);
        auto pValName = AsPtrOf<wchar_t>(pb + sizeof(PPBinaryValue));
        auto pValData = pb + sizeof(PPBinaryValue) + pVal->NameSize;

        if (pb + pVal->TotalSize > EndOfpb)
            throw std::invalid_argument("pBinaryVal->TotalSize is incorrect.");
        
        switch (pVal->DataType) {
            case REG_SZ: {
                PolicyValue PolicyVal(std::in_place_type_t<PolicyValue::TypeOfString>{});

                PolicyVal.Name.assign(pValName, pVal->NameSize / 2);
                PolicyVal.GetData<PolicyValue::TypeOfString>().assign(AsPtrOf<wchar_t>(pValData), pVal->DataSize / sizeof(wchar_t));
                PolicyVal.Flags = pVal->Flags;
                PolicyVal.Reserved = pVal->Reserved;

                if (Result._Policies.size() == 0 || Result._Policies.back().GetName() <= PolicyVal.GetName()) {
                    Result._Policies.emplace_back(PolicyVal);
                } else {
                    throw std::invalid_argument("Unsorted policy binary data.");
                }

                break;
            }
            case REG_BINARY: {
                PolicyValue PolicyVal(std::in_place_type_t<PolicyValue::TypeOfBinary>{});

                PolicyVal.Name.assign(pValName, pVal->NameSize / 2);
                PolicyVal.GetData<PolicyValue::TypeOfBinary>().assign(AsPtrOf<uint8_t>(pValData), AsPtrOf<uint8_t>(pValData) + pVal->DataSize);
                PolicyVal.Flags = pVal->Flags;
                PolicyVal.Reserved = pVal->Reserved;

                if (Result._Policies.size() == 0 || Result._Policies.back().GetName() <= PolicyVal.GetName()) {
                    Result._Policies.emplace_back(PolicyVal);
                } else {
                    throw std::invalid_argument("Unsorted policy binary data.");
                }

                break;
            }
            case REG_DWORD: {
                PolicyValue PolicyVal(std::in_place_type_t<PolicyValue::TypeOfUInt32>{});

                PolicyVal.Name.assign(pValName, pVal->NameSize / 2);
                PolicyVal.GetData<PolicyValue::TypeOfUInt32>() = *AsPtrOf<uint32_t>(pValData);
                PolicyVal.Flags = pVal->Flags;
                PolicyVal.Reserved = pVal->Reserved;

                if (Result._Policies.size() == 0 || Result._Policies.back().GetName() <= PolicyVal.GetName()) {
                    Result._Policies.emplace_back(PolicyVal);
                } else {
                    throw std::invalid_argument("Unsorted binary policy data.");
                }

                break;
            }
            default:
                throw std::invalid_argument("Unexpected value type.");
        }

        pb += pVal->TotalSize;
    }

    return Result;
}

std::vector<uint8_t> ProductPolicyParser::ToBinary(const ProductPolicy& PolicyObject) {
    std::vector<uint8_t> Result;
    PPBinaryHeader Header;

    for (const PolicyValue& Policy : PolicyObject._Policies) {
        PPBinaryValue BinaryValue;
        size_t PaddingSize;

        BinaryValue.NameSize = 
            static_cast<uint16_t>(Policy.GetName().length() * sizeof(wchar_t));
        BinaryValue.DataType = 
            static_cast<uint16_t>(Policy.GetType());
        switch (Policy.GetType()) {
            case PolicyValue::TypeLabel::String:
                BinaryValue.DataSize = 
                    static_cast<uint16_t>(Policy.GetData<PolicyValue::TypeOfString>().length() * sizeof(wchar_t));
                break;
            case PolicyValue::TypeLabel::Binary:
                BinaryValue.DataSize = 
                    static_cast<uint16_t>(Policy.GetData<PolicyValue::TypeOfBinary>().size());
                break;
            case PolicyValue::TypeLabel::UInt32:
                BinaryValue.DataSize = 
                    sizeof(Policy.GetData<PolicyValue::TypeOfUInt32>());
                break;
            default:
                throw std::invalid_argument("Unexpected value type.");
        } 
        BinaryValue.Flags = Policy.Flags;
        BinaryValue.Reserved = Policy.Reserved;
        BinaryValue.TotalSize = (sizeof(PPBinaryValue) + BinaryValue.NameSize + BinaryValue.DataSize + 2 + 3) / 4 * 4;
        PaddingSize = BinaryValue.TotalSize - (sizeof(PPBinaryValue) + BinaryValue.NameSize + BinaryValue.DataSize);

        WriteIn<'END'>(Result, &BinaryValue, sizeof(PPBinaryValue));
        WriteIn<'END'>(Result, Policy.GetName().data(), BinaryValue.NameSize);
        switch (Policy.GetType()) {
            case PolicyValue::TypeLabel::String:
                WriteIn<'END'>(Result, Policy.GetData<PolicyValue::TypeOfString>().data(), BinaryValue.DataSize);
                break;
            case PolicyValue::TypeLabel::Binary:
                WriteIn<'END'>(Result, Policy.GetData<PolicyValue::TypeOfBinary>().data(), BinaryValue.DataSize);
                break;
            case PolicyValue::TypeLabel::UInt32:
                WriteIn<'END'>(Result, &Policy.GetData<PolicyValue::TypeOfUInt32>(), BinaryValue.DataSize);
                break;
            default:
                throw std::invalid_argument("Unexpected value type.");
        }

        if (PaddingSize)
            WriteNullBytesIn<'END'>(Result, PaddingSize);
    }

    Header.DataSize = static_cast<uint32_t>(Result.size());
    Header.EndMarkerSize = sizeof(uint32_t);
    Header.Reserved = 0;
    Header.Revision = 1;
    Header.TotalSize = sizeof(PPBinaryHeader) + Header.DataSize + Header.EndMarkerSize;

    WriteIn<'BEGN'>(Result, &Header, sizeof(PPBinaryHeader));
    WriteIn<'END'>(Result, &EndMarker, sizeof(uint32_t));

    return Result;
}

