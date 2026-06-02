#ifndef NEWDIRECTORY_EQUIPMENT_H
#define NEWDIRECTORY_EQUIPMENT_H

#include <iostream>
#include <string>
#include <array>
#include <utility>
#include <initializer_list>

/**
 * 属性値を定義する列挙型です。
 *
 * 各列挙値は、装備品が軽減する属性に関連するものであり、
 * 耐性値や効果の計算などで利用されます。
 */
enum class Attribute {
    Fire,
    Ice,
    ThunderExplosion,
    Wind,
    Darkness,
    Earth,
    Light,
    AttributeCount // 属性の数
};

struct Equipment {
    /**
     * 装備品の名前を表します。
     *
     * @note この変数はconstとして定義されているため、インスタンス生成後に変更することはできません。
     * @note この値は装備品の一意性判定や識別に利用されることを想定しています。
     */
    const std::string_view name;
    /**
     * 装備品の各属性に対する耐性値を格納する配列です。
     * 配列の各インデックスは、Attribute列挙型の各属性に対応します。
     *
     * @note 初期化されていない場合、全ての耐性値は0になります。
     * @note 配列のサイズはAttribute::AttributeCountに従い、すべての属性について耐性値を保持できます。
     */
    std::array<int, static_cast<int>(Attribute::AttributeCount)> resistances;


    /**
     * 装備品の名前と初期耐性値を設定するコンストラクタです。
     *
     * @param inputName 装備品の名前
     * @param initList 初期耐性のリスト (属性と耐性値のペアのリスト)
     *
     * @return このコンストラクタはクラスインスタンスの生成時に呼び出され、装備名と対応する耐性値の配列を初期化します
     *
     * @note 初期化リスト内の属性値は列挙型Attributeに従い、それを元に耐性配列を初期化します。
     */
    constexpr Equipment(std::string_view inputName, std::initializer_list<std::pair<Attribute, int> > initList)
        : name(inputName), resistances{} {
        for (auto entry: initList) {
            resistances[static_cast<int>(entry.first)] = entry.second;
        }
    }
};

// 装備の初期化
//ダークシールド
// グローバル変数を inline 変数として定義
inline constexpr Equipment DarkShield("Dark shield", {
                                          {Attribute::Fire, 5},
                                          {Attribute::Ice, 5}
                                      });

//げんまのよろい
inline constexpr Equipment EtherealArmour("Ethereal armour", {
                                              {Attribute::Fire, 18},
                                              {Attribute::Ice, 18},
                                              {Attribute::Wind, 18},
                                              {Attribute::ThunderExplosion, 18},
                                              {Attribute::Darkness, 15},
                                          });

//せいれいのこて
inline constexpr Equipment EnchantedGloves("Enchanted gloves", {
                                               {Attribute::Fire, 7},
                                               {Attribute::Ice, 7},
                                               {Attribute::Wind, 7},
                                               {Attribute::ThunderExplosion, 7},
                                               {Attribute::Darkness, 7},
                                           });

//竜戦士のブーツ
inline constexpr Equipment DragonWarriorBoots("Dragon warrior boots", {
                                                  {Attribute::Fire, 5},
                                                  {Attribute::Darkness, 5}
                                              });

/**
 * すべての装備品を格納する定数配列です。
 *
 * 配列内には、DarkShield、EtherealArmour、EnchantedGlovesの3つの装備品オブジェクトが含まれます。
 * 各装備品はそれぞれの名前と初期耐性値を持ち、ゲーム内の特性や計算に使用されます。
 *
 * @note この配列はconstexprとして定義されているため、コンパイル時に初期化され、実行時に変更することはできません。
 * @note 配列内の順序は、装備品管理や計算処理で一貫性を保つために重要です。
 */
// 同様に、配列も inline 化
inline constexpr std::array<Equipment, 4> allEquipments = {
    DarkShield, EtherealArmour, EnchantedGloves, DragonWarriorBoots
};


/**
 * 指定された属性について装備全体の耐性倍率を計算します。
 *
 * @param attribute 耐性倍率を計算する対象の属性
 * @return 指定された属性に基づく総合的な耐性倍率を小数点以下2桁の精度で返します
 *
 * @note この関数はconstexprとして実行されるため、コンパイル時計算にも使用できます。
 * @attention 計算結果は1000倍された値で返され、後続の処理で任意精度計算に利用されます。
 */
constexpr double PrivateCalculateTotalResistance(Attribute attribute) {
    //指定された倍率の
    int totalResistance = 100; //100 = 0%軽減
    for (const auto &equipment: allEquipments) {
        //各装備について
        totalResistance -= equipment.resistances[static_cast<size_t>(attribute)]; //軽減率を引く
    }
    return totalResistance * 1000; //実行時に小数点以下2桁の任意精度計算処理をするので1000倍にする
}

class Equipments {
private:
    /**
     * 各属性に対する耐性倍率を事前に計算し、配列に格納します。
     *
     * @return 属性ごとの事前計算された耐性倍率の配列
     *
     * @note この配列はstd::arrayとして表現され、コンパイル時に評価されます。
     * @attention 耐性倍率には高精度な計算が行われ、結果は各属性に対応したインデックスに格納されます。
     */
    inline static constexpr std::array<double, static_cast<size_t>(Attribute::AttributeCount)> resistances = []() constexpr {
        std::array<double, static_cast<size_t>(Attribute::AttributeCount)> res{}; //exeに組み込めるようにstd::arrayを使う
        for (size_t i = 0; i < res.size(); ++i) {
            //各属性について
            res[i] = PrivateCalculateTotalResistance(static_cast<Attribute>(i)); //任意精度計算処理に使う軽減倍率を計算
        }
        return res; //軽減倍率をexeに埋め込む
    }();


    /**
     * 指定された属性に基づいて、事前に計算された耐性倍率を取得します。
     *
     * @param attribute 取得対象の属性
     * @return 指定された属性に対して事前に計算された耐性倍率
     *
     * @note 耐性倍率は小数点以下2桁の精度で保持されます。
     * @attention この関数は静的でconstexprであり、実行時でなくコンパイル時計算として使用できます。
     */
    inline static constexpr double calculateTotalResistance(const Attribute attribute) {
        return resistances[static_cast<size_t>(attribute)]; //事前に計算された、任意精度計算処理(小数点以下2桁精度)に使う属性の軽減率を取得
    }

public:
    /**
     * 与えられたダメージ値に基づき属性ごとのダメージ軽減を適用します。
     *
     * @param damage 元のダメージ値
     * @param attr 適用対象の属性
     * @return 属性耐性に基づき軽減された最終的なダメージ値
     *
     * @note 属性による耐性倍率は小数点以下の2桁精度で計算されます。
     * @attention この関数は静的かつconstexprであり、コンパイル時計算が可能ですが、
     * 属性に対応する耐性倍率の事前定義が必要です。
     */
    static constexpr double applyDamageReduction(const double damage, const Attribute attr) {
        const double multiplier = calculateTotalResistance(attr); //事前に計算されたdouble(ただし自然数)方式の倍率を取得
        return damage * multiplier / 100000.0; //小数点以下2桁の任意精度計算処理
    }
};


#endif //NEWDIRECTORY_EQUIPMENT_H
