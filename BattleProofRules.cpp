#include "ProofProgram.h"
#include "BattleEmulator.h"

namespace proof {
namespace {
using B=BattleEmulator;
enum Slot {
    E,A,M,I,Charge,Para,Acro,Rage,Inactive,Camera,Command,Prepared,Action,Damage,
    Initiative,Defence,Action0,Action1,ActionCount,EnemyCommand,Critical,Dodge,Shield,
    Random,Scratch,Temporary,Before,After,Arg0,Arg1,CameraFirst
};
class Builder {
public:
    Assembler a;
    unsigned serial=0;
    using Body=std::function<void()>;
    void branch(int x,int y,Body yes,Body no={}) {
        auto name="b"+std::to_string(serial++);
        a.branch(x,y,name+"t",name+"f");
        a.label(name+"t");yes();a.jump(name+"end");
        a.label(name+"f");if(no) no();a.label(name+"end");
    }
    void le(int x,Int k,Body yes,Body no={}) {
        a.set(63,k);branch(x,63,std::move(yes),std::move(no));
    }
    void eq(int x,Int k,Body yes,Body no={}) {
        le(x,k,[&]{le(x,k-1,no?no:[]{},yes);},no);
    }
    void percent(int maximum) {a.set(Arg0,maximum);a.native(Random,Native::Percent,{Arg0});}
    void attack(int atk,int def) {
        a.set(Arg0,atk);a.set(Arg1,def);a.native(Damage,Native::AttackDamage,{Arg0,Arg1});
    }
    void spell(int difference,int base) {
        a.set(Arg0,difference);a.set(Arg1,base);a.native(Damage,Native::SpellDamage,{Arg0,Arg1});
    }
    void critical(bool dragon=false) {
        a.set(Arg0,dragon?1:0);a.native(Damage,Native::CriticalAttack,{Arg0,Damage});
    }
    void reduce(int resource) {
        a.add(resource,resource,Damage,-1);le(resource,0,[&]{a.set(resource,0);});
    }
    void heal(int resource,int gain,int maximum) {
        a.add(resource,resource,gain);le(resource,maximum,[]{},[&]{a.set(resource,maximum);});
    }
    void mobile(Body yes,Body no={}) {
        le(Para,4,no?no:[]{},[&]{eq(Inactive,0,yes,no);});
    }
    void chargeOnePercent() {
        le(Charge,-1,[&]{percent(100);le(Random,0,[&]{a.set(Charge,6);});});
    }
    void define(int id,Body body) {a.begin(id);body();a.ret();a.end();}
    void dispatch(int slot,const std::vector<std::pair<int,int>>& cases) {
        for(auto [value,routine]:cases) eq(slot,value,[&]{a.call(routine);a.ret();});
        a.reject();
    }
};
RuleProgram assembleBattle() {
    Builder b;auto& a=b.a;
    const int turn=a.declare("Main/TurnFlow");
    const int legal=a.declare("selection/legal-eight-commands");
    const int ally=a.declare("Main/ally-slot");
    const int enemy=a.declare("Main/enemy-slot");
    const int selectEnemy=a.declare("Main/enemy-selection-and-replacement");
    const int record=a.declare("Main/actions-array");
    const int allyCases=a.declare("callAttackFun/ally-switch");
    const int enemyCases=a.declare("callAttackFun/enemy-switch");
    const int attack=a.declare("callAttackFun/ATTACK_ALLY-DRAGON_SLASH");
    const int crack=a.declare("callAttackFun/CRACK_ALLY");
    const int heal=a.declare("callAttackFun/HEAL");
    const int herb=a.declare("callAttackFun/MEDICINAL_HERBS");
    const int acro=a.declare("callAttackFun/ACROBATIC_STAR");
    const int defend=a.declare("callAttackFun/DEFENCE-CURE_PARALYSIS");
    const int blocked=a.declare("callAttackFun/PARALYSIS-INACTIVE_ALLY");
    const int physical=a.declare("callAttackFun/ATTACK_ENEMY-HP_HOOVER");
    const int victim=a.declare("callAttackFun/VICTIMISER");
    const int enemyCrack=a.declare("callAttackFun/CRACK_ENEMY");
    const int gaze=a.declare("callAttackFun/MANAZASHI");
    const int puff=a.declare("callAttackFun/PUFF_PUFF");
    const int evade=a.declare("callAttackFun/ACROBATSTAR_KAIHI");
    const int counter=a.declare("callAttackFun/COUNTER");
    const int rage=a.declare("ProcessRage");
    const int charge=a.declare("process7A8");
    const int camera=a.declare("camera::Main/two-slots");
    const int freeCamera=a.declare("camera::onFreeCameraMove");

    b.define(legal,[&] {
        b.eq(Command,B::HEAL,[&]{b.le(M,1,[&]{a.reject();});a.ret();});
        b.eq(Command,B::CRACK_ALLY,[&]{b.le(M,2,[&]{a.reject();});a.ret();});
        b.eq(Command,B::MEDICINAL_HERBS,[&]{b.le(I,0,[&]{a.reject();});a.ret();});
        b.eq(Command,B::ACROBATIC_STAR,[&]{
            b.le(Charge,0,[&]{a.reject();});b.le(Acro,0,[]{},[&]{a.reject();});a.ret();
        });
        b.eq(Command,B::FLEE_ALLY,[&]{b.mobile([&]{a.ret();},[&]{a.reject();});});
        for(int c:{B::ATTACK_ALLY,B::DRAGON_SLASH,B::DEFENCE}) b.eq(Command,c,[&]{a.ret();});
        a.reject();
    });
    b.define(record,[&] {
        b.eq(ActionCount,0,[&]{a.copy(Action0,Action);},[&]{a.copy(Action1,Action);});
        a.addConstant(ActionCount,ActionCount,1);
    });
    b.define(rage,[&] {
        a.copy(Before,E);a.add(After,Before,Damage,-1);
        b.le(After,-1,[&]{a.set(After,0);});
        b.le(After,227,[&]{
            b.le(Before,227,[&]{
                b.le(After,113,[&]{b.le(Before,113,[]{},[&]{
                    b.eq(Rage,0,[&]{a.skip(2);},[&]{a.skip(1);});
                });});
            },[&]{
                b.eq(Rage,0,[&]{a.skip(1);b.percent(3);a.addConstant(Rage,Random,2);},[&]{a.skip(1);});
            });
        });
    });
    b.define(charge,[&] {
        b.mobile([&]{
            b.branch(A,Damage,[]{},[&]{
                b.le(Charge,-1,[&]{
                    b.eq(Damage,0,[&]{a.skip(1);a.ret();});
                    b.percent(100);
                    a.native(Temporary,Native::ChargeChance,{Damage});a.addConstant(Temporary,Temporary,-1);
                    b.branch(Random,Temporary,[&]{a.set(Charge,6);});
                });
            });
        });
    });
    b.define(blocked,[&]{a.skip(5);b.attack(61,66);a.skip(1);a.set(Damage,0);});
    b.define(defend,[&]{
        a.skip(5);b.attack(61,66);a.skip(1);
        b.le(Charge,-1,[&]{a.skip(1);b.chargeOnePercent();});a.set(Damage,0);
    });
    b.define(acro,[&]{
        a.set(Acro,6);a.set(Charge,-1);a.skip(5);b.attack(61,66);a.skip(1);a.set(Damage,0);
    });
    b.define(herb,[&]{
        a.addConstant(I,I,-1);a.skip(5);a.native(Damage,Native::HerbDamage,{});a.skip(1);
        b.le(Charge,-1,[&]{a.skip(1);b.chargeOnePercent();});
    });
    b.define(heal,[&]{
        a.set(Critical,0);a.skip(3);b.percent(10000);b.le(Random,99,[&]{a.set(Critical,1);});
        a.skip(1);b.spell(5,35);
        b.eq(Critical,1,[&]{a.native(Damage,Native::CriticalSpell,{Damage});});
        a.skip(1);b.le(Charge,-1,[&]{a.skip(1);});b.eq(Rage,0,[&]{a.skip(1);});a.skip(1);
        b.eq(Critical,1,[&]{b.eq(Rage,0,[&]{a.skip(2);},[&]{a.skip(1);});});
        b.mobile([&]{b.chargeOnePercent();});a.addConstant(M,M,-2);
    });
    b.define(crack,[&]{
        a.set(Critical,0);a.addConstant(M,M,-3);a.skip(3);b.percent(10000);
        b.le(Random,99,[&]{a.set(Critical,1);});a.skip(2);b.spell(5,30);
        b.eq(Critical,1,[&]{a.native(Damage,Native::CriticalSpell,{Damage});});
        a.set(Arg0,1);a.native(Damage,Native::HalfDamage,{Damage,Arg0});a.call(rage);
        b.eq(Critical,1,[&]{b.eq(Rage,0,[&]{a.skip(2);},[&]{a.skip(1);});});
        a.skip(1);b.chargeOnePercent();
    });
    b.define(attack,[&]{
        a.set(Critical,0);a.set(Dodge,0);a.skip(3);b.percent(10000);
        b.eq(Action,B::DRAGON_SLASH,[&]{b.le(Random,99,[&]{a.set(Critical,1);});},
            [&]{b.le(Random,199,[&]{a.set(Critical,1);});});
        b.mobile([&]{b.percent(100);b.le(Random,1,[&]{a.set(Dodge,1);});b.eq(Dodge,0,[&]{a.skip(1);});});
        a.skip(1);b.attack(61,58);
        b.eq(Critical,1,[&]{b.eq(Action,B::DRAGON_SLASH,[&]{b.critical(true);},[&]{b.critical();});});
        b.eq(Dodge,0,[&]{a.call(rage);a.skip(2);},[&]{a.set(Damage,0);});
        b.eq(Critical,1,[&]{b.eq(Rage,0,[&]{a.skip(2);},[&]{a.skip(1);});});
        a.addConstant(Scratch,Damage,-1);
        b.branch(E,Scratch,[]{},[&]{b.chargeOnePercent();});
    });
    b.define(evade,[&]{
        b.percent(10000);a.skip(1);b.attack(56,66);
        b.le(Charge,-1,[&]{a.skip(1);});a.set(Damage,0);
    });
    b.define(counter,[&]{
        a.set(Critical,0);a.set(Dodge,0);a.skip(1);b.percent(10000);
        b.le(Random,199,[&]{a.set(Critical,1);});b.percent(100);b.le(Random,1,[&]{a.set(Dodge,1);});
        b.eq(Dodge,0,[&]{a.skip(1);});a.skip(1);b.attack(61,58);
        b.eq(Critical,1,[&]{b.critical();});b.eq(Dodge,0,[&]{a.call(rage);b.reduce(E);});
        a.set(Damage,0);
    });
    b.define(physical,[&]{
        a.set(Dodge,0);a.set(Shield,0);a.skip(2);
        b.le(Acro,0,[&]{a.skip(1);},[&]{b.mobile([&]{
            b.percent(100);b.le(Random,49,[&]{a.call(evade);a.ret();});
            b.le(Random,74,[&]{a.call(counter);a.ret();});
        },[&]{a.skip(1);});});
        a.skip(1);
        b.mobile([&]{
            b.eq(Acro,0,[&]{b.percent(100);b.le(Random,1,[&]{a.set(Dodge,1);});});
            b.eq(Dodge,0,[&]{b.percent(100);b.le(Random,0,[&]{a.set(Shield,1);});});
        });
        a.skip(1);b.attack(56,66);
        b.eq(Dodge,1,[&]{a.set(Damage,0);});b.eq(Shield,1,[&]{a.set(Damage,0);});
        // The source distinguishes evasion/guard from zero natural damage.
        b.eq(Dodge,0,[&]{b.eq(Shield,0,[&]{
            a.skip(2);
            b.eq(EnemyCommand,B::HP_HOOVER,[&]{
                a.native(Damage,Native::HalfDamage,{Damage,Defence});
                a.set(Arg0,1);a.native(Temporary,Native::HalfDamage,{Damage,Arg0});
                a.native(Temporary,Native::HalfDamage,{Temporary,Arg0});b.heal(E,Temporary,456);
            });
        });});
        b.eq(EnemyCommand,B::ATTACK_ENEMY,[&]{a.native(Damage,Native::HalfDamage,{Damage,Defence});});
        a.call(charge);
    });
    b.define(victim,[&]{
        a.set(Dodge,0);a.set(Shield,0);a.skip(4);
        b.mobile([&]{
            b.eq(Acro,0,[&]{b.percent(100);b.le(Random,1,[&]{a.set(Dodge,1);});});
            b.eq(Dodge,0,[&]{b.percent(100);b.le(Random,0,[&]{a.set(Shield,1);});});
        });
        a.skip(1);b.attack(56,66);a.native(Damage,Native::VictimDamage,{Damage});
        b.eq(Dodge,1,[&]{a.set(Damage,0);});b.eq(Shield,1,[&]{a.set(Damage,0);});
        b.eq(Dodge,0,[&]{b.eq(Shield,0,[&]{a.skip(2);});});
        a.native(Damage,Native::HalfDamage,{Damage,Defence});a.call(charge);
    });
    b.define(enemyCrack,[&]{
        a.set(Shield,0);a.skip(4);
        b.mobile([&]{b.percent(100);b.le(Random,0,[&]{a.set(Shield,1);});});
        a.skip(1);b.spell(5,17);b.eq(Shield,1,[&]{a.set(Damage,0);},[&]{a.skip(1);});
        a.native(Damage,Native::HalfDamage,{Damage,Defence});a.call(charge);
    });
    b.define(gaze,[&]{
        a.skip(5);b.spell(4,12);b.percent(100);b.le(Random,24,[&]{a.set(Para,4);});
        a.skip(1);a.native(Damage,Native::HalfDamage,{Damage,Defence});a.call(charge);
    });
    b.define(puff,[&]{
        a.skip(4);b.percent(100);b.le(Random,49,[&]{a.set(Inactive,1);});
        b.eq(Inactive,0,[&]{b.le(Para,4,[&]{a.set(Damage,0);a.ret();});});
        b.eq(Inactive,1,[&]{b.attack(56,66);a.skip(1);},[&]{b.le(Charge,-1,[&]{a.skip(1);});});
        a.set(Damage,0);
    });
    b.define(allyCases,[&]{b.dispatch(Action,{
        {B::ATTACK_ALLY,attack},{B::DRAGON_SLASH,attack},{B::CRACK_ALLY,crack},
        {B::HEAL,heal},{B::MEDICINAL_HERBS,herb},{B::ACROBATIC_STAR,acro},
        {B::DEFENCE,defend},{B::CURE_PARALYSIS,defend},{B::PARALYSIS,blocked},{B::INACTIVE_ALLY,blocked}});});
    b.define(enemyCases,[&]{b.dispatch(EnemyCommand,{
        {B::ATTACK_ENEMY,physical},{B::HP_HOOVER,physical},{B::VICTIMISER,victim},
        {B::CRACK_ENEMY,enemyCrack},{B::MANAZASHI,gaze},{B::PUFF_PUFF,puff}});});
    b.define(selectEnemy,[&]{
        // getPercent < -0.0330 is impossible, but the read is not omitted.
        b.percent(100);b.percent(256);a.addConstant(Random,Random,1);
        b.le(Random,43,[&]{a.set(EnemyCommand,B::VICTIMISER);},[&]{
            b.le(Random,85,[&]{a.set(EnemyCommand,B::HP_HOOVER);},[&]{
                b.le(Random,128,[&]{a.set(EnemyCommand,B::CRACK_ENEMY);},[&]{
                    b.le(Random,171,[&]{a.set(EnemyCommand,B::ATTACK_ENEMY);},[&]{
                        b.le(Random,213,[&]{a.set(EnemyCommand,B::MANAZASHI);},[&]{a.set(EnemyCommand,B::PUFF_PUFF);});
                    });
                });
            });
        });
        b.eq(EnemyCommand,B::PUFF_PUFF,[&]{b.eq(Inactive,1,[&]{a.set(EnemyCommand,B::MANAZASHI);});});
        b.eq(EnemyCommand,B::MANAZASHI,[&]{a.skip(2);},[&]{b.eq(Rage,0,[&]{a.skip(1);});});
        b.eq(EnemyCommand,B::VICTIMISER,[&]{b.eq(Para,5,[&]{a.set(EnemyCommand,B::HP_HOOVER);});});
        a.skip(1);
    });
    b.define(enemy,[&]{
        b.le(E,0,[&]{a.ret();});b.le(A,0,[&]{a.ret();});
        a.call(selectEnemy);a.set(Damage,0);a.call(enemyCases);a.copy(Action,EnemyCommand);a.call(record);b.reduce(A);
        b.le(A,0,[&]{a.ret();});b.le(E,0,[&]{a.ret();});a.skip(1);
        b.le(Rage,0,[]{},[&]{a.addConstant(Rage,Rage,-1);});
    });
    b.define(ally,[&]{
        b.le(E,0,[&]{a.ret();});b.le(A,0,[&]{a.ret();});a.copy(Action,Prepared);
        b.eq(Action,B::FLEE_ALLY,[&]{b.mobile([&]{a.ret();},[&]{a.reject();});});
        b.eq(Para,5,[&]{a.skip(1);},[&]{
            a.set(Action,B::PARALYSIS);a.addConstant(Para,Para,-1);
            b.le(Para,0,[&]{
                a.native(Random,Native::ParalysisRelease,{Para});
                b.eq(Random,1,[&]{a.set(Para,5);a.set(Action,B::CURE_PARALYSIS);});
            },[&]{a.skip(1);});
        });
        b.eq(Inactive,1,[&]{
            a.set(Inactive,0);b.eq(Action,B::CURE_PARALYSIS,[]{},[&]{
                b.eq(Action,B::PARALYSIS,[]{},[&]{a.set(Action,B::INACTIVE_ALLY);});
            });
        });
        a.set(Damage,0);a.call(allyCases);a.call(record);
        b.eq(Action,B::HEAL,[&]{b.heal(A,Damage,65);},[&]{
            b.eq(Action,B::MEDICINAL_HERBS,[&]{b.heal(A,Damage,65);},[&]{b.reduce(E);});
        });
        b.le(A,0,[&]{a.ret();});b.le(E,0,[&]{a.ret();});a.skip(1);
        b.le(Acro,0,[]{},[&]{a.addConstant(Acro,Acro,-1);b.eq(Acro,0,[&]{a.skip(1);});});
    });
    b.define(freeCamera,[&]{
        b.eq(CameraFirst,0,[&]{
            a.skip(1);b.eq(Camera,0,[&]{a.set(Camera,1);a.ret();});
            a.set(Arg0,5);a.add(Arg0,Arg0,Camera,-1);a.native(Random,Native::Percent,{Arg0});
            b.eq(Random,0,[&]{a.set(Camera,0);a.skip(1);a.ret();});
            b.eq(Camera,5,[&]{a.set(Camera,0);a.skip(1);a.ret();});a.addConstant(Camera,Camera,1);
        },[&]{
            a.skip(1);b.eq(Camera,0,[&]{a.skip(1);a.set(Camera,0);a.ret();});a.skip(2);a.set(Camera,0);
        });
    });
    b.define(camera,[&]{
        a.set(CameraFirst,1);
        for(int s:{Action0,Action1}) {
            b.eq(s,B::ATTACK_ALLY,[&]{a.call(freeCamera);},[&]{
                b.eq(s,B::ATTACK_ENEMY,[&]{a.skip(1);},[&]{b.eq(s,B::DRAGON_SLASH,[&]{a.skip(1);});});
            });
            b.eq(s,B::ATTACK_ALLY,[]{},[&]{a.set(CameraFirst,0);});
        }
    });
    a.begin(turn);
    a.call(legal);
    b.le(Charge,-1,[]{},[&]{a.addConstant(Charge,Charge,-1);});
    a.set(Defence,0);a.set(Action0,0);a.set(Action1,0);a.set(ActionCount,0);
    a.native(Initiative,Native::Initiative,{});a.skip(1);a.copy(Prepared,Command);
    b.eq(Prepared,B::DEFENCE,[&]{b.mobile([]{},[&]{a.set(Prepared,B::ATTACK_ALLY);});});
    b.eq(Prepared,B::DEFENCE,[&]{a.set(Defence,1);});
    b.eq(Initiative,1,[&]{a.call(ally);a.call(enemy);},[&]{a.call(enemy);a.call(ally);});
    b.le(A,0,[]{},[&]{b.le(E,0,[]{},[&]{a.skip(1);});});
    a.call(camera);a.finish();a.end();
    return a.take();
}
}
std::shared_ptr<const RegisteredRules> battleRules() {
    static const auto rules=registerRules(assembleBattle());
    return rules;
}
}
