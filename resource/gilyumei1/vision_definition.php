<?php

declare(strict_types=1);

return [
    'id' => 'gilyumei1',
    'names' => [
        'ja' => 'ギュメイ1モード',
        'en' => 'Gilyumei 1 Mode',
    ],
    'picker' => 'gilyumei1',
    'battleEmulator' => [
        'branch' => 'gilyumei1',
    ],
    'templateGroups' => [
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
            'sourceBoss' => 'erugiosu',
        ],
    ],
    'identify' => [
        'templates' => [],
    ],
    'rules' => [],
];
