#ifndef YO2_PROOF_DOMAIN_H
#define YO2_PROOF_DOMAIN_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace proof {
using Int = std::int64_t;
inline Int add(Int a, Int b) {
    if ((b > 0 && a > std::numeric_limits<Int>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Int>::min() - b))
        throw std::overflow_error("proof integer addition");
    return a + b;
}
inline Int neg(Int x) {
    if (x == std::numeric_limits<Int>::min()) throw std::overflow_error("proof integer negation");
    return -x;
}
inline Int mul(Int a, Int b) {
    if (a == 0 || b == 0) return 0;
    if (a == -1) return neg(b);
    if (b == -1) return neg(a);
    if ((a > 0 && b > 0 && a > std::numeric_limits<Int>::max() / b) ||
        (a > 0 && b < 0 && b < std::numeric_limits<Int>::min() / a) ||
        (a < 0 && b > 0 && a < std::numeric_limits<Int>::min() / b) ||
        (a < 0 && b < 0 && a < std::numeric_limits<Int>::max() / b))
        throw std::overflow_error("proof integer multiplication");
    return a * b;
}
struct Interval {
    Int lo = 0, hi = 0;
    bool operator==(const Interval&) const = default;
};
inline constexpr std::array<unsigned, 6> modeSizes{8,8,7,5,2,6};
// Physical values of Charge, Paralysis, Acro, Rage, Inactive, Camera.
// Charge OFF=-1, Paralysis CLEAR=5; boundary enums remain distinct.
inline constexpr std::array<std::array<Int,8>,6> modeValues{{
    {-1,0,1,2,3,4,5,6}, {5,-2,-1,0,1,2,3,4}, {0,1,2,3,4,5,6,0},
    {0,1,2,3,4,0,0,0}, {0,1,0,0,0,0,0,0}, {0,1,2,3,4,5,0,0}}};
struct Box {
    std::array<Interval,4> resource{};
    std::array<std::uint8_t,6> mode{};
    bool operator==(const Box&) const = default;
    bool empty() const {
        for (auto x : resource) if (x.lo > x.hi) return true;
        for (auto x : mode) if (!x) return true;
        return false;
    }
    bool contains(const Box& b) const {
        for (int j=0;j<4;++j)
            if (b.resource[j].lo < resource[j].lo || b.resource[j].hi > resource[j].hi) return false;
        for (int j=0;j<6;++j) if (b.mode[j] & ~mode[j]) return false;
        return true;
    }
    Box intersect(const Box& b) const {
        Box r=*this;
        for (int j=0;j<4;++j) r.resource[j]={std::max(resource[j].lo,b.resource[j].lo),
                                            std::min(resource[j].hi,b.resource[j].hi)};
        for (int j=0;j<6;++j) r.mode[j] &= b.mode[j];
        return r;
    }
};
struct Predicate {
    // axis 0..3: resource <= threshold; axis 4..9: input mode in mask.
    int axis = 0;
    Int threshold = 0;
    std::uint8_t mask = 0;
    bool operator==(const Predicate&) const = default;
};
inline Box restrictBox(Box b, Predicate p, bool yes) {
    if (p.axis < 0 || p.axis >= 10) throw std::logic_error("predicate axis");
    if (p.axis < 4) {
        if (yes) b.resource[p.axis].hi=std::min(b.resource[p.axis].hi,p.threshold);
        else if (p.threshold == std::numeric_limits<Int>::max()) b.resource[p.axis]={1,0};
        else b.resource[p.axis].lo=std::max(b.resource[p.axis].lo,p.threshold+1);
    } else {
        b.mode[p.axis-4] &= yes ? p.mask : std::uint8_t(~p.mask);
    }
    return b;
}
struct Value {
    enum Kind { Constant, Resource, Mode } kind = Constant;
    int axis = 0;
    Int offset = 0;
    std::array<Int,8> table{};
    bool operator==(const Value&) const = default;
    static Value constant(Int x) { Value v;v.offset=x;return v; }
    static Value resource(int j) { Value v;v.kind=Resource;v.axis=j;return v; }
    static Value mode(int j) { Value v;v.kind=Mode;v.axis=j;v.table=modeValues[j];return v; }
    Value translated(Int x) const {
        auto v=*this;
        if (kind==Mode) { for (auto& n:v.table) n=add(n,x); }
        else v.offset=add(v.offset,x);
        return v;
    }
    std::vector<Int> values(const Box& b) const {
        if (kind==Constant) return {offset};
        if (kind==Resource) throw std::logic_error("resource passed to concrete native");
        std::vector<Int> out;
        for (unsigned k=0;k<modeSizes[axis];++k) if (b.mode[axis] & (1u<<k))
            if (std::find(out.begin(),out.end(),table[k])==out.end()) out.push_back(table[k]);
        return out;
    }
};
inline Box restrictValue(Box b,const Value& v,Int k,bool yes) {
    if (v.kind==Value::Constant) {
        if ((v.offset<=k)!=yes) b.resource[0]={1,0};
        return b;
    }
    if (v.kind==Value::Resource) return restrictBox(b,{v.axis,add(k,neg(v.offset)),0},yes);
    std::uint8_t mask=0;
    for (unsigned i=0;i<modeSizes[v.axis];++i) if (v.table[i]<=k) mask|=1u<<i;
    return restrictBox(b,{v.axis+4,0,mask},yes);
}
inline Box image(const Box& b,const std::array<Value,10>& values) {
    Box out;
    for (int j=0;j<4;++j) {
        auto v=values[j];
        if (v.kind==Value::Constant) out.resource[j]={v.offset,v.offset};
        else if (v.kind==Value::Resource && v.axis==j)
            out.resource[j]={add(b.resource[j].lo,v.offset),add(b.resource[j].hi,v.offset)};
        else throw std::logic_error("non-self resource output");
    }
    for (int j=0;j<6;++j) {
        auto v=values[j+4];
        if (v.kind==Value::Mode && v.axis!=j) throw std::logic_error("non-self mode output");
        for (auto x:v.values(b)) {
            auto first=modeValues[j].begin(),last=first+modeSizes[j];
            auto i=std::find(first,last,x);
            if (i==last) throw std::logic_error("output outside boundary mode enum");
            out.mode[j]|=1u<<std::distance(first,i);
        }
    }
    return out;
}
inline Box preimage(Box b,const std::array<Value,10>& out,const Box& target) {
    for (int j=0;j<4;++j) {
        b=restrictValue(b,out[j],target.resource[j].hi,true);
        b=restrictValue(b,out[j],add(target.resource[j].lo,-1),false);
    }
    for (int j=0;j<6;++j) {
        const auto& v=out[j+4];
        auto allowed=[&](Int x) {
            for (unsigned k=0;k<modeSizes[j];++k)
                if ((target.mode[j]&(1u<<k)) && modeValues[j][k]==x) return true;
            return false;
        };
        if (v.kind==Value::Constant) { if (!allowed(v.offset)) b.mode[j]=0; }
        else if (v.kind==Value::Mode && v.axis==j) {
            std::uint8_t mask=0;
            for (unsigned k=0;k<modeSizes[j];++k) if (allowed(v.table[k])) mask|=1u<<k;
            b.mode[j]&=mask;
        } else throw std::logic_error("mode preimage type");
    }
    return b;
}
}
#endif
