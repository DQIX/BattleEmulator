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
