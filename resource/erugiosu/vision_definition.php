<?php

declare(strict_types=1);

return [
    'id' => 'erugiosu',
    'names' => [
        'ja' => 'エルギオスモード',
        'en' => 'Erugiosu Mode',
    ],
    'picker' => 'erugiosu',
    'battleEmulator' => [
        'id' => 'erugiosu_gouketu',
    ],
    'thresholds' => [
        'templateThreshold' => 0.45,
        'resetLatchClearScore' => 0.6,
        'whiteThreshold' => 0.72,
        'matchWhiteThresholdDark' => 0.59,
        'matchWhiteThresholdBright' => 0.7,
        'whiteSaturationMaxDark' => 0.2,
        'whiteSaturationMaxBright' => 0.09,
        'whiteSaturationDarkValue' => 0.1,
        'numberWhiteSaturationMaxDark' => 0.2,
        'numberWhiteSaturationMaxBright' => 0.28,
        'numberWhiteThresholdBright' => 0.7,
        'whiteSaturationBrightValue' => 0.9,
        'numberWhiteThresholdDark' => 0.59,
        'actionThreshold' => 0.45,
        'numberThreshold' => 0.65,
        'matchPenaltyWeight' => 0.0,
        'matchWhiteWeight' => 1.0,
        'templateAlphaThreshold' => 0.05,
    ],
    'templateGroups' => [
        [
            'slot' => 'main',
            'directory' => 'message_v2',
            'sourceBoss' => 'system',
        ],
        [
            'slot' => 'main',
            'directory' => 'message_v2',
            'sourceBoss' => 'ally',
        ],
        [
            'slot' => 'main',
            'directory' => 'message_v2',
        ],
        [
            'slot' => 'sub',
            'directory' => 'submessage_v2',
        ],
        [
            'slot' => 'ally',
            'directory' => 'sub2message_v2',
        ],
        [
            'slot' => 'target',
            'directory' => 'target',
        ],
    ],
    'numberGroups' => [
        [
            'directory' => 'numbers',
        ],
    ],
    'identify' => [
        'templates' => [
        ],
    ],
    'rules' => [
        'erugioMain' => ['erugio.png', 'erugio2.png', 'erugio4.png'],
        'resetSub' => ['reset.png'],
        'enemyAttackSub' => ['attack.png'],
        'uhscSub' => ['uhsc.png'],
        'allyAttack' => ['a_attack.png'],
        'dead' => ['dead.png', 'dead2.png'],
        'wakeUp' => ['WakeUp.png', 'WakeUp2.png', 'WakeUp3.png'],
        'psycheUpTarget' => ['aha.png'],
        'directMainActions' => [
            'sukara.png' => 30,
            'hadou.png' => 16,
            'yaketuku.png' => 17,
            'zilyoukuu.png' => 8,
            'merazoma.png' => 9,
            'mira-.png' => 31,
            'samidare.png' => 34,
            'samidare2.png' => 34,
            'no_hadou.png' => 15,
            'zigosupa.png' => 5,
            'kuroi.png' => 18,
            'sutemi.png' => 33,
            'seisui.png' => 49,
            'meisou.png' => 41,
            'madannte.png' => 42,
            'ice.png' => 10,
            'fullheal.png' => 37,
            'more_heal.png' => 32,
            'ayasii.png' => 12,
            'mp2.png' => 43,
            'song.png' => 52,
            'sippuu.png' => 44,
            'sage.png' => 47,
            'elven.png' => 48,
            'flee.png' => 53,
            'tokuyaku.png' => 50,
            'lightning.png' => 162,
        ],
    ],
];
