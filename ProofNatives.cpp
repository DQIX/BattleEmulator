#include "ProofProgram.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include <cmath>

namespace proof {
// Only these resource-independent functions cross the native boundary.
// No Player, Frame, command dispatcher, rage or charge routine is passed in.
struct NativeAccess {
    static int attack(int& p,int atk,int def) { return BattleEmulator::FUN_0207564c(&p,atk,def); }
    static int herb(int& p) { return BattleEmulator::FUN_021e8458_typeC(&p,35.0,35.0,5.0); }
    static int spell(int& p,int difference,int base) {
        return BattleEmulator::FUN_021e8458_typeD(&p,difference,base);
    }
};
unsigned nativeArity(Native n) {
    switch(n) {
        case Native::Initiative:case Native::HerbDamage:return 0;
        case Native::Percent:case Native::CriticalSpell:case Native::VictimDamage:
        case Native::ParalysisRelease:case Native::ChargeChance:return 1;
        case Native::AttackDamage:case Native::SpellDamage:case Native::CriticalAttack:
        case Native::HalfDamage:return 2;
    }
    throw std::invalid_argument("unregistered native");
}
unsigned nativeDraws(Native n) {
    switch(n) {
        case Native::Initiative:case Native::HerbDamage:case Native::AttackDamage:return 2;
        case Native::Percent:case Native::SpellDamage:case Native::CriticalAttack:
        case Native::CriticalSpell:case Native::ParalysisRelease:return 1;
        case Native::HalfDamage:case Native::VictimDamage:case Native::ChargeChance:return 0;
    }
    throw std::invalid_argument("unregistered native");
}
Int evaluateNative(Native n,std::span<const Int> a,int& p) {
    if(a.size()!=nativeArity(n) || p<1 || p>4999-int(nativeDraws(n)))
        throw std::logic_error("native argument/cursor contract");
    for(auto x:a) if(x < -3 || x > 10000) throw std::logic_error("native argument range");
    switch(n) {
        case Native::Percent:
            if(a[0]<0) throw std::logic_error("negative RNG maximum");
            return lcg::getPercent(&p,int(a[0]));
        case Native::Initiative: {
            double speed0=40*lcg::floatRand(&p,0.51,1.0);
            double speed1=54*lcg::floatRand(&p,0.51,1.0);
            return speed0>speed1;
        }
        case Native::AttackDamage:
            if(!((a[0]==61 && (a[1]==58 || a[1]==66)) || (a[0]==56 && a[1]==66)))
                throw std::logic_error("unregistered attack profile");
            return NativeAccess::attack(p,int(a[0]),int(a[1]));
        case Native::HerbDamage:return NativeAccess::herb(p);
        case Native::SpellDamage:
            if(!((a[0]==5 && (a[1]==17 || a[1]==30 || a[1]==35)) || (a[0]==4 && a[1]==12)))
                throw std::logic_error("unregistered spell profile");
            return NativeAccess::spell(p,int(a[0]),int(a[1]));
        case Native::CriticalAttack:
            if(a[0]!=0 && a[0]!=1) throw std::logic_error("critical attack kind");
            if(a[1]<0 || a[1]>64) throw std::logic_error("critical base damage");
            if(a[0]==0) return static_cast<int>(61*lcg::floatRand(&p,0.95,1.05));
            return static_cast<int>(double(a[1])*lcg::floatRand(&p,1.5,2.0));
        case Native::CriticalSpell:
            if(a[0]<0 || a[0]>64) throw std::logic_error("critical spell base");
            return static_cast<int>(a[0]*lcg::floatRand(&p,1.5,2.0));
        case Native::HalfDamage:
            if(a[0]<0 || (a[1]!=0 && a[1]!=1)) throw std::logic_error("defence multiplier");
            return static_cast<int>(a[0]*(a[1]?0.5:1.0));
        case Native::VictimDamage:
            if(a[0]<0) throw std::logic_error("negative damage");
            return static_cast<int>(a[0]*1.5);
        case Native::ParalysisRelease: {
            if(a[0]>0) throw std::logic_error("paralysis native timer");
            const double table[4]={0.6250,0.7500,0.8750,1.0000};
            const double probability1=table[std::abs(int(a[0]))];
            const double probability2=lcg::getPercent(&p,100)*0.01;
            return probability1>=probability2;
        }
        case Native::ChargeChance: {
            if(a[0]<0) throw std::logic_error("negative charge damage");
            constexpr int chance[9]={90,90,64,32,16,8,4,2,1};
            for(int i=9;i>=1;--i) {
                double multiplier=static_cast<double>(i)/10.0;
                int threshold=static_cast<int>(65*multiplier)+1;
                if(a[0]>=threshold) return chance[9-i];
            }
            return 0;
        }
    }
    throw std::logic_error("unregistered native");
}
}
