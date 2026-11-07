#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ip.hpp"
#include "parser/log_entry.hpp"

namespace hpla {

struct FilterInfo {
    std::string name;
    std::string value;
};

// Different filter options are ANDed together. Values repeated within one
// option (multiple statuses, IPs, methods, exact paths, path substrings, or
// user-agent substrings) are ORed. Status matching uses a 600-entry lookup for
// speed; IP matching parses the record address once per record and compares
// canonical bytes, so equivalent IPv4/IPv6 spellings match.
class FilterSet {
public:
    void set_statuses(const std::vector<std::uint16_t>& statuses);
    void set_status_range(std::uint16_t minimum, std::uint16_t maximum);
    void add_ip(IpAddress address);
    void add_method(std::string method);  // stored uppercase
    void add_exact_path(std::string path);
    void add_path_contains(std::string needle);
    void add_user_agent_contains(std::string needle);

    void set_from(std::int64_t epoch_seconds) { from_ = epoch_seconds; }
    void set_to(std::int64_t epoch_seconds) { to_ = epoch_seconds; }
    void set_min_size(std::uint64_t bytes) { min_size_ = bytes; }
    void set_max_size(std::uint64_t bytes) { max_size_ = bytes; }

    std::optional<std::int64_t> from() const { return from_; }
    std::optional<std::int64_t> to() const { return to_; }
    std::optional<std::uint64_t> min_size() const { return min_size_; }
    std::optional<std::uint64_t> max_size() const { return max_size_; }
    const std::vector<IpAddress>& ips() const { return ips_; }
    const std::vector<std::string>& methods() const { return methods_; }
    const std::vector<std::string>& exact_paths() const { return exact_paths_; }
    const std::vector<std::string>& path_substrings() const { return path_substrings_; }
    const std::vector<std::string>& user_agent_substrings() const { return ua_substrings_; }
    const std::array<bool, 600>& status_lookup() const { return status_lookup_; }
    bool has_status_filter() const noexcept { return status_count_ > 0; }
    std::uint32_t status_count() const noexcept { return status_count_; }

    void add_description(std::string name, std::string value);
    const std::vector<FilterInfo>& descriptions() const { return descriptions_; }

    bool active() const noexcept;
    bool matches(const LogEntry& entry) const noexcept;

private:
    std::array<bool, 600> status_lookup_{};
    std::uint32_t status_count_ = 0;
    std::vector<IpAddress> ips_;
    std::vector<std::string> methods_;
    std::vector<std::string> exact_paths_;
    std::vector<std::string> path_substrings_;
    std::vector<std::string> ua_substrings_;
    std::optional<std::int64_t> from_;
    std::optional<std::int64_t> to_;
    std::optional<std::uint64_t> min_size_;
    std::optional<std::uint64_t> max_size_;
    std::vector<FilterInfo> descriptions_;
};

}  // namespace hpla
