#include <gtest/gtest.h>

#include <string>

#include "core/ip.hpp"

TEST(Ip, ParsesIpv4) {
    const auto address = hpla::parse_ip("192.168.1.10");
    ASSERT_TRUE(address.has_value());
    EXPECT_TRUE(address->is_v4);
    EXPECT_EQ(address->bytes[0], 192U);
    EXPECT_EQ(address->bytes[3], 10U);
    EXPECT_EQ(hpla::format_ip(*address), "192.168.1.10");
}

TEST(Ip, RejectsInvalidIpv4) {
    EXPECT_FALSE(hpla::parse_ip("256.1.1.1").has_value());
    EXPECT_FALSE(hpla::parse_ip("1.2.3").has_value());
    EXPECT_FALSE(hpla::parse_ip("1.2.3.4.5").has_value());
    EXPECT_FALSE(hpla::parse_ip("1..2.3").has_value());
    EXPECT_FALSE(hpla::parse_ip("1.2.3.x").has_value());
    EXPECT_FALSE(hpla::parse_ip("").has_value());
    EXPECT_FALSE(hpla::parse_ip("hostname").has_value());
}

TEST(Ip, ParsesIpv6Forms) {
    const auto loopback = hpla::parse_ip("::1");
    ASSERT_TRUE(loopback.has_value());
    EXPECT_FALSE(loopback->is_v4);
    EXPECT_EQ(loopback->bytes[15], 1U);
    EXPECT_EQ(hpla::format_ip(*loopback), "::1");

    const auto compressed = hpla::parse_ip("2001:db8::1");
    ASSERT_TRUE(compressed.has_value());
    EXPECT_EQ(hpla::format_ip(*compressed), "2001:db8::1");

    const auto full = hpla::parse_ip("2001:0db8:0000:0000:0000:0000:0000:0001");
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(*full, *compressed);
}

TEST(Ip, ParsesEmbeddedIpv4) {
    const auto address = hpla::parse_ip("::ffff:192.168.1.10");
    ASSERT_TRUE(address.has_value());
    EXPECT_TRUE(address->is_v4);
    EXPECT_EQ(hpla::format_ip(*address), "192.168.1.10");

    const auto mixed = hpla::parse_ip("2001:db8::192.0.2.1");
    ASSERT_TRUE(mixed.has_value());
    EXPECT_FALSE(mixed->is_v4);
    EXPECT_EQ(mixed->bytes[12], 192U);
    EXPECT_EQ(mixed->bytes[13], 0U);
    EXPECT_EQ(mixed->bytes[14], 2U);
    EXPECT_EQ(mixed->bytes[15], 1U);
}

TEST(Ip, RejectsInvalidIpv6) {
    EXPECT_FALSE(hpla::parse_ip(":::").has_value());
    EXPECT_FALSE(hpla::parse_ip("1::2::3").has_value());
    EXPECT_FALSE(hpla::parse_ip("fe80::1%eth0").has_value());
    EXPECT_FALSE(hpla::parse_ip("12345::").has_value());
    EXPECT_FALSE(hpla::parse_ip(":1:2").has_value());
    EXPECT_FALSE(hpla::parse_ip("1:2:").has_value());
    EXPECT_FALSE(hpla::parse_ip("2001:db8::1/64").has_value());
}

TEST(Ip, DistinguishesAddresses) {
    const auto first = hpla::parse_ip("10.0.0.1");
    const auto second = hpla::parse_ip("10.0.0.2");
    const auto mapped = hpla::parse_ip("::ffff:10.0.0.1");
    ASSERT_TRUE(first && second && mapped);
    EXPECT_EQ(*first, *mapped);
    EXPECT_NE(*first, *second);
}

TEST(Ip, FormatsZeroAddress) {
    const auto address = hpla::parse_ip("::");
    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(hpla::format_ip(*address), "::");
}
