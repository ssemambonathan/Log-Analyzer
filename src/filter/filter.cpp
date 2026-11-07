#include "filter/filter.hpp"

#include <cstddef>
#include <utility>

namespace hpla {
namespace {

bool equals_ignore_case(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        char a = left[i];
        char b = right[i];
        if (a >= 'a' && a <= 'z') {
            a = static_cast<char>(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = static_cast<char>(b - 'a' + 'A');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

}  // namespace

void FilterSet::set_statuses(const std::vector<std::uint16_t>& statuses) {
    for (const std::uint16_t status : statuses) {
        if (status < status_lookup_.size() && !status_lookup_[status]) {
            status_lookup_[status] = true;
            ++status_count_;
        }
    }
}

void FilterSet::set_status_range(std::uint16_t minimum, std::uint16_t maximum) {
    for (std::uint32_t status = minimum; status <= maximum && status < status_lookup_.size(); ++status) {
        if (!status_lookup_[status]) {
            status_lookup_[status] = true;
            ++status_count_;
        }
    }
}

void FilterSet::add_ip(IpAddress address) {
    ips_.push_back(address);
}

void FilterSet::add_method(std::string method) {
    methods_.push_back(std::move(method));
}

void FilterSet::add_exact_path(std::string path) {
    exact_paths_.push_back(std::move(path));
}

void FilterSet::add_path_contains(std::string needle) {
    path_substrings_.push_back(std::move(needle));
}

void FilterSet::add_user_agent_contains(std::string needle) {
    ua_substrings_.push_back(std::move(needle));
}

void FilterSet::add_description(std::string name, std::string value) {
    descriptions_.push_back(FilterInfo{std::move(name), std::move(value)});
}

bool FilterSet::active() const noexcept {
    return status_count_ > 0 || !ips_.empty() || !methods_.empty() || !exact_paths_.empty() ||
           !path_substrings_.empty() || !ua_substrings_.empty() || from_.has_value() ||
           to_.has_value() || min_size_.has_value() || max_size_.has_value();
}

bool FilterSet::matches(const LogEntry& entry) const noexcept {
    if (status_count_ > 0) {
        if (entry.status >= status_lookup_.size() || !status_lookup_[entry.status]) {
            return false;
        }
    }

    if (!ips_.empty()) {
        const auto address = parse_ip(entry.client_ip);
        if (!address.has_value()) {
            return false;
        }
        bool matched = false;
        for (const IpAddress& candidate : ips_) {
            if (candidate == *address) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    if (!methods_.empty()) {
        bool matched = false;
        for (const std::string& method : methods_) {
            if (equals_ignore_case(entry.method, method)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    bool matched_target = false;
    for (const std::string& path : exact_paths_) {
        if (entry.target == path) {
            matched_target = true;
            break;
        }
    }
    if (!exact_paths_.empty() && !matched_target) {
        return false;
    }

    if (!path_substrings_.empty()) {
        bool matched = false;
        for (const std::string& needle : path_substrings_) {
            if (entry.target.find(needle) != std::string_view::npos) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    if (!ua_substrings_.empty()) {
        bool matched = false;
        for (const std::string& needle : ua_substrings_) {
            if (entry.user_agent.find(needle) != std::string_view::npos) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    if (from_.has_value() && entry.timestamp_epoch < *from_) {
        return false;
    }
    if (to_.has_value() && entry.timestamp_epoch > *to_) {
        return false;
    }

    if (min_size_.has_value()) {
        if (!entry.has_response_bytes || entry.response_bytes < *min_size_) {
            return false;
        }
    }
    if (max_size_.has_value()) {
        if (!entry.has_response_bytes || entry.response_bytes > *max_size_) {
            return false;
        }
    }

    return true;
}

}  // namespace hpla
