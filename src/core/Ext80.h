#pragma once

#include <bit>
#include <cfloat>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

// The original computes in the 80-bit x87 format (64-bit significand, round to nearest even) and
// rounds to float on every store; the core repeats that to show the same numbers. GCC and Clang on
// x86 have it as long double. Elsewhere (arm64: 64-bit long double on macOS, 128-bit on Linux;
// MSVC: 64-bit) Ext80 does the same arithmetic in software, bit for bit (tst_ext80 compares it with
// the hardware). TS_SOFT_EXT80 forces it on x86, to run the golden tests with it.
//
// Not covered: exponents beyond the x87 range and its denormals (the core never gets near them).
class Ext80
{
public:
    Ext80() = default;
    template <std::floating_point T>
    Ext80(T v) : Ext80(fromDouble(double(v))) {}
    template <std::signed_integral T>
    Ext80(T v) : Ext80(fromInt(v < 0, v < 0 ? 0 - std::uint64_t(v) : std::uint64_t(v))) {}
    template <std::unsigned_integral T>
    Ext80(T v) : Ext80(fromInt(false, std::uint64_t(v))) {}

    explicit operator double() const { return toBinary<double, std::uint64_t, 53, 1023>(); }
    explicit operator float() const { return toBinary<float, std::uint32_t, 24, 127>(); }
    template <std::integral T>
    explicit operator T() const // toward zero, as a C++ conversion
    {
        if constexpr (std::is_unsigned_v<T>)
            return T(truncatedUnsigned());
        else
            return T(truncated());
    }

    friend Ext80 operator+(const Ext80 &a, const Ext80 &b) { return add(a, b, b.m_neg); }
    friend Ext80 operator-(const Ext80 &a, const Ext80 &b) { return add(a, b, !b.m_neg); }
    friend Ext80 operator*(const Ext80 &a, const Ext80 &b) { return multiply(a, b); }
    friend Ext80 operator/(const Ext80 &a, const Ext80 &b) { return divide(a, b); }
    Ext80 operator-() const
    {
        Ext80 r = *this;
        r.m_neg = !r.m_neg;
        return r;
    }
    Ext80 &operator+=(const Ext80 &b) { return *this = *this + b; }
    Ext80 &operator-=(const Ext80 &b) { return *this = *this - b; }
    Ext80 &operator*=(const Ext80 &b) { return *this = *this * b; }
    Ext80 &operator/=(const Ext80 &b) { return *this = *this / b; }

    friend bool operator==(const Ext80 &a, const Ext80 &b) { return compare(a, b) == 0; }
    friend bool operator!=(const Ext80 &a, const Ext80 &b) { return compare(a, b) != 0; }
    friend bool operator<(const Ext80 &a, const Ext80 &b) { return compare(a, b) == -1; }
    friend bool operator<=(const Ext80 &a, const Ext80 &b) { const int c = compare(a, b); return c == -1 || c == 0; }
    friend bool operator>(const Ext80 &a, const Ext80 &b) { return compare(a, b) == 1; }
    friend bool operator>=(const Ext80 &a, const Ext80 &b) { const int c = compare(a, b); return c == 1 || c == 0; }

    friend Ext80 fabs(Ext80 a)
    {
        a.m_neg = false;
        return a;
    }
    friend Ext80 floor(const Ext80 &a) { return a.floored(); }
    friend bool isfinite(const Ext80 &a) { return a.m_kind == Zero || a.m_kind == Normal; }
    friend bool isnan(const Ext80 &a) { return a.m_kind == NaN; }
    friend bool signbit(const Ext80 &a) { return a.m_neg; }

    // The significand (top bit set for a normal number) and the exponent of its top bit.
    std::uint64_t significand() const { return m_mant; }
    int exponent() const { return m_exp; }

private:
#ifndef __SIZEOF_INT128__
#error "Ext80 needs unsigned __int128 (GCC or Clang)"
#endif
    using u128 = unsigned __int128;
    enum Kind : std::uint8_t { Zero, Normal, Inf, NaN };

    static Ext80 make(Kind kind, bool neg, std::uint64_t mant = 0, int exp = 0)
    {
        Ext80 r;
        r.m_kind = kind;
        r.m_neg = neg;
        r.m_mant = mant;
        r.m_exp = exp;
        return r;
    }

    // value = (sig + something below its last bit when sticky) * 2^e2, rounded to a 64-bit
    // significand, to nearest, ties to even. An inexact value always has more than 64 significant bits.
    static Ext80 pack(bool neg, u128 sig, int e2, bool sticky)
    {
        if (sig == 0)
            return make(Zero, neg);
        const auto hi = std::uint64_t(sig >> 64);
        if (hi == 0) {
            const auto lo = std::uint64_t(sig);
            const int lead = std::countl_zero(lo);
            return make(Normal, neg, lo << lead, e2 + 63 - lead);
        }
        const int shift = 64 - std::countl_zero(hi); // 1..64
        std::uint64_t mant = std::uint64_t(sig >> shift);
        const u128 rem = sig & ((u128(1) << shift) - 1);
        const u128 half = u128(1) << (shift - 1);
        int exp = e2 + 63 + shift;
        // Without branches: the rounding of random data is unpredictable.
        mant += std::uint64_t((rem > half) | ((rem == half) & (sticky | bool(mant & 1))));
        if (mant == 0) [[unlikely]] {
            mant = std::uint64_t(1) << 63;
            ++exp;
        }
        return make(Normal, neg, mant, exp);
    }

    static Ext80 fromDouble(double v)
    {
        const auto bits = std::bit_cast<std::uint64_t>(v);
        const bool neg = bits >> 63;
        const int e = int(bits >> 52) & 0x7FF;
        const std::uint64_t frac = bits & ((std::uint64_t(1) << 52) - 1);
        if (e == 0x7FF)
            return make(frac ? NaN : Inf, neg);
        if (e == 0)
            return frac ? pack(neg, frac, -1074, false) : make(Zero, neg);
        return make(Normal, neg, (frac | std::uint64_t(1) << 52) << 11, e - 1023);
    }

    static Ext80 fromInt(bool neg, std::uint64_t magnitude)
    {
        return magnitude ? pack(neg, magnitude, 0, false) : Ext80();
    }

    // The significand rounded to `bits` bits, to nearest, ties to even (may become 2^bits); for fewer
    // than one bit, the value is below half of the last place or at most one place.
    static std::uint64_t roundTo(std::uint64_t mant, int bits)
    {
        if (bits <= 0)
            return bits == 0 && mant > std::uint64_t(1) << 63 ? 1 : 0;
        const int shift = 64 - bits;
        const std::uint64_t r = mant >> shift;
        const std::uint64_t rem = mant & ((std::uint64_t(1) << shift) - 1);
        const std::uint64_t half = std::uint64_t(1) << (shift - 1);
        return r + std::uint64_t((rem > half) | ((rem == half) & bool(r & 1)));
    }

    // To an IEEE binary format with `digits` significand bits, rounded once as the x87 store does.
    template <typename F, typename Bits, int digits, int bias>
    F toBinary() const
    {
        constexpr int minExp = 1 - bias, maxExp = bias;
        switch (m_kind) {
        case Zero: return m_neg ? -F(0) : F(0);
        case Inf: return m_neg ? -std::numeric_limits<F>::infinity() : std::numeric_limits<F>::infinity();
        case NaN: return std::numeric_limits<F>::quiet_NaN();
        case Normal: break;
        }
        if (m_exp < minExp) { // a subnormal: fewer bits
            const int bits = digits - (minExp - m_exp);
            const F r = std::ldexp(F(roundTo(m_mant, bits)), m_exp - bits + 1);
            return m_neg ? -r : r;
        }
        std::uint64_t m = roundTo(m_mant, digits);
        int exp = m_exp;
        if (m >> digits) {
            m >>= 1;
            ++exp;
        }
        if (exp > maxExp)
            return m_neg ? -std::numeric_limits<F>::infinity() : std::numeric_limits<F>::infinity();
        constexpr int width = int(sizeof(Bits)) * 8;
        const Bits bits = Bits(Bits(m_neg) << (width - 1)) | Bits(Bits(exp + bias) << (digits - 1))
                          | Bits(m & ((std::uint64_t(1) << (digits - 1)) - 1));
        return std::bit_cast<F>(bits);
    }

    std::int64_t truncated() const
    {
        if (m_kind == Zero || (m_kind == Normal && m_exp < 0))
            return 0;
        if (m_kind != Normal || m_exp > 62)
            return std::numeric_limits<std::int64_t>::min(); // x86: the "integer indefinite"
        const std::uint64_t magnitude = m_mant >> (63 - m_exp);
        return m_neg ? std::int64_t(0 - magnitude) : std::int64_t(magnitude);
    }

    std::uint64_t truncatedUnsigned() const
    {
        if (m_kind == Normal && m_exp == 63)
            return m_neg ? 0 - m_mant : m_mant;
        return std::uint64_t(truncated());
    }

    // mant + rest / 2^64 (rest: the bits below mant's last, sticky in its lowest bit when more were
    // lost) rounded to nearest, ties to even.
    static Ext80 rounded(bool neg, std::uint64_t mant, int exp, std::uint64_t rest)
    {
        constexpr std::uint64_t half = std::uint64_t(1) << 63;
        // Without branches: the rounding of random data is unpredictable.
        mant += std::uint64_t((rest > half) | ((rest == half) & bool(mant & 1)));
        if (mant == 0) [[unlikely]] {
            mant = half;
            ++exp;
        }
        return make(Normal, neg, mant, exp);
    }

    // a + b with b's sign replaced by bNeg. 64-bit words: x is the larger magnitude, y is shifted right by
    // the difference of the exponents into y's part above x's last bit and the bits below it (`rest`).
    static Ext80 add(const Ext80 &a, const Ext80 &b, bool bNeg)
    {
        if (a.m_kind != Normal || b.m_kind != Normal) [[unlikely]] {
            if (a.m_kind == NaN || b.m_kind == NaN)
                return make(NaN, false);
            if (a.m_kind == Inf || b.m_kind == Inf) {
                if (a.m_kind == Inf && b.m_kind == Inf && a.m_neg != bNeg)
                    return make(NaN, true);
                return a.m_kind == Inf ? a : make(Inf, bNeg);
            }
            if (b.m_kind == Zero)
                return a.m_kind == Zero ? make(Zero, a.m_neg && bNeg) : a;
            return make(Normal, bNeg, b.m_mant, b.m_exp);
        }
        // Which is larger and the shift, without branches (they are unpredictable on real data).
        const bool aLarger = (a.m_exp > b.m_exp) | ((a.m_exp == b.m_exp) & (a.m_mant >= b.m_mant));
        const std::uint64_t mask = 0 - std::uint64_t(aLarger);
        const std::uint64_t xm = (a.m_mant & mask) | (b.m_mant & ~mask);
        const std::uint64_t ym = a.m_mant ^ b.m_mant ^ xm;
        const int emask = -int(aLarger);
        const int xe = (a.m_exp & emask) | (b.m_exp & ~emask);
        const auto d = unsigned(xe - (a.m_exp ^ b.m_exp ^ xe));
        const bool xNeg = aLarger ? a.m_neg : bNeg;
        const bool sameSign = a.m_neg == bNeg;
        std::uint64_t yhi, rest;
        if (d < 64) [[likely]] {
            yhi = ym >> d;
            rest = (ym << 1) << (63 - d); // ym << (64 - d), 0 for d = 0
        } else {
            if (sameSign && d > 64)
                return make(Normal, xNeg, xm, xe); // y is below half of x's last place
            yhi = 0;
            rest = d == 64 ? ym : d < 128 ? (ym >> (d - 64)) | std::uint64_t((ym << (128 - d)) != 0) : 1;
        }
        if (sameSign) {
            // A carry (c = 1) shifts the sum right by one; the bit shifted out of `rest` stays as sticky.
            const std::uint64_t sum = xm + yhi;
            const auto c = std::uint64_t(sum < xm);
            return rounded(xNeg, (sum >> c) | (c << 63), xe + int(c), (rest >> c) | (rest & c) | ((sum & c) << 63));
        }
        // x - y: a borrow from the bits below; at most one bit of normalization unless d <= 1, when
        // nothing was lost.
        std::uint64_t diff = xm - yhi - (rest != 0);
        std::uint64_t frac = 0 - rest;
        if (diff == 0) [[unlikely]] {
            if (frac == 0)
                return Ext80();
            const int lead = std::countl_zero(frac);
            return make(Normal, xNeg, frac << lead, xe - 64 - lead); // d <= 1: exact
        }
        const int lead = std::countl_zero(diff);
        diff = (diff << lead) | ((frac >> 1) >> (63 - lead)); // frac >> (64 - lead), 0 for lead = 0
        frac <<= lead;
        return rounded(xNeg, diff, xe - lead, frac);
    }

    static Ext80 multiply(const Ext80 &a, const Ext80 &b)
    {
        const bool neg = a.m_neg != b.m_neg;
        if (a.m_kind != Normal || b.m_kind != Normal) [[unlikely]] {
            if (a.m_kind == NaN || b.m_kind == NaN)
                return make(NaN, false);
            if (a.m_kind == Inf || b.m_kind == Inf)
                return a.m_kind == Zero || b.m_kind == Zero ? make(NaN, true) : make(Inf, neg);
            return make(Zero, neg);
        }
        // Both significands have the top bit: the product has 127 or 128 bits.
        const u128 p = u128(a.m_mant) * b.m_mant;
        const auto hi = std::uint64_t(p >> 64), lo = std::uint64_t(p);
        const bool top = hi >> 63;
        return rounded(neg, top ? hi : (hi << 1) | (lo >> 63), a.m_exp + b.m_exp + (top ? 1 : 0), top ? lo : lo << 1);
    }

    static Ext80 divide(const Ext80 &a, const Ext80 &b)
    {
        const bool neg = a.m_neg != b.m_neg;
        if (a.m_kind != Normal || b.m_kind != Normal) [[unlikely]] {
            if (a.m_kind == NaN || b.m_kind == NaN)
                return make(NaN, false);
            if (a.m_kind == Inf)
                return b.m_kind == Inf ? make(NaN, true) : make(Inf, neg);
            if (b.m_kind == Inf)
                return make(Zero, neg);
            if (b.m_kind == Zero)
                return a.m_kind == Zero ? make(NaN, true) : make(Inf, neg);
            return make(Zero, neg);
        }
        // a·2^64 / d has 64 or 65 bits; with the 65th taken out first, one 128-by-64 division whose
        // quotient fits 64 bits.
        const std::uint64_t d = b.m_mant;
        const bool wide = a.m_mant >= d;
        const u128 num = u128(wide ? a.m_mant - d : a.m_mant) << 64;
        const auto q = std::uint64_t(num / d);
        const auto r = std::uint64_t(num) - q * d; // the remainder, below d
        if (!wide) {
            // The next bit and the rest: 2r against d.
            const std::uint64_t rest = r >= d - r ? (std::uint64_t(1) << 63) | std::uint64_t(r != d - r) : std::uint64_t(r != 0);
            return rounded(neg, q, a.m_exp - b.m_exp - 1, rest);
        }
        return rounded(neg, (q >> 1) | (std::uint64_t(1) << 63), a.m_exp - b.m_exp, (q << 63) | std::uint64_t(r != 0));
    }

    static int compare(const Ext80 &a, const Ext80 &b) // -1, 0, 1; 2 when unordered
    {
        if (a.m_kind == NaN || b.m_kind == NaN) [[unlikely]]
            return 2;
        const int sa = a.m_kind == Zero ? 0 : a.m_neg ? -1 : 1;
        const int sb = b.m_kind == Zero ? 0 : b.m_neg ? -1 : 1;
        if (sa != sb)
            return sa < sb ? -1 : 1;
        if (sa == 0)
            return 0;
        int magnitude = 0;
        if (a.m_kind == Inf || b.m_kind == Inf)
            magnitude = a.m_kind == b.m_kind ? 0 : a.m_kind == Inf ? 1 : -1;
        else if (a.m_exp != b.m_exp)
            magnitude = a.m_exp > b.m_exp ? 1 : -1;
        else if (a.m_mant != b.m_mant)
            magnitude = a.m_mant > b.m_mant ? 1 : -1;
        return sa > 0 ? magnitude : -magnitude;
    }

    Ext80 floored() const
    {
        if (m_kind != Normal || m_exp >= 63)
            return *this;
        if (m_exp < 0)
            return m_neg ? Ext80(-1) : Ext80();
        const std::uint64_t mask = (std::uint64_t(1) << (63 - m_exp)) - 1;
        if ((m_mant & mask) == 0)
            return *this;
        const Ext80 whole = make(Normal, m_neg, m_mant & ~mask, m_exp);
        return m_neg ? whole - Ext80(1) : whole;
    }

    std::uint64_t m_mant = 0; // value = m_mant * 2^(m_exp - 63)
    std::int32_t m_exp = 0;
    Kind m_kind = Zero;
    bool m_neg = false;
};

#if LDBL_MANT_DIG == 64 && !defined(TS_SOFT_EXT80)
using Ext = long double;
#else
using Ext = Ext80;
#endif

// Constants as the original's 80-bit literals: the nearest values (a quotient rounds correctly).
inline const Ext kExtMilli = Ext(1) / Ext(1000); // 0.001L
inline const Ext kExtCenti = Ext(1) / Ext(100);  // 0.01L

inline Ext extFloor(const Ext &v)
{
    using std::floor;
    return floor(v);
}

inline Ext extFabs(const Ext &v)
{
    using std::fabs;
    return fabs(v);
}
