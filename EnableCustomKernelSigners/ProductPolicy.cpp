#include "ProductPolicy.hpp"
#include <regex>

size_t ProductPolicy::NumberOfPolicies() const noexcept {
    return _Policies.size();
}

size_t ProductPolicy::FindPolicy(const std::wstring& Regex, size_t StartPos) {
    for (size_t i = StartPos; i < _Policies.size(); ++i) {
        if (regex_match(_Policies[i].GetName(), std::wregex(Regex)))
            return i;
    }
    return InvalidPos;
}

PolicyValue& ProductPolicy::operator[](size_t Index) {
    return _Policies[Index];
}

PolicyValue& ProductPolicy::operator[](const std::wstring& Name) {
    size_t i = 0, j = _Policies.size();
    while (true) {
        size_t mid = i + (j - i) / 2;

        if (i == mid)
            break;

        switch (_Policies[mid].GetName().compare(Name)) {
            case 1:
                j = mid;
                break;
            case -1:
                i = mid;
                break;
            default:
                return _Policies[mid];
        }
    }

    throw std::out_of_range("Cannot find target policy.");
}

const PolicyValue& ProductPolicy::operator[](size_t Index) const {
    return _Policies[Index];
}

const PolicyValue& ProductPolicy::operator[](const std::wstring& Name) const {
    size_t i = 0, j = _Policies.size();
    while (true) {
        size_t mid = i + (j - i) / 2;

        if (i == mid)
            break;

        switch (_Policies[mid].GetName().compare(Name)) {
        case 1:
            j = mid;
            break;
        case -1:
            i = mid;
            break;
        default:
            return _Policies[mid];
        }
    }

    throw std::out_of_range("Cannot find target policy.");
}

