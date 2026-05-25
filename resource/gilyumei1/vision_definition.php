<?php

declare(strict_types=1);

return [
	'id' => 'gilyumei1',
	'thresholds' => [
		'whiteSaturationMaxDark' => 0.20,
		'whiteSaturationMaxBright' => 0.25,
		'numberWhiteThresholdBright' => 0.65,
		'numberWhiteThresholdDark' => 0.50,
		'whiteThreshold' => 0.60,
	],
	'names' => [
		'ja' => 'ギュメイ1モード',
		'en' => 'Gilyumei 1 Mode',
	],
	'picker' => 'gilyumei1',
	'battleEmulator' => [
		 'id' => 'gilyumei1_v6',
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
			'sourceBoss' => 'erugiosu',
		],
	],
	'identify' => [
		'templates' => [
			[
				'slot' => 'main',
				'directory' => 'message_v2',
				'file' => 'gilyumei.png',
			]
		],
	],
	'rules' => [
		'gilyumeiMain' => ['gilyumei.png'],
		'psycheUpTarget' => ['aha.png'],
		'resetSub' => ['reset.png'],
		'kiriage' => ['kiriage.png'],
		'samidare' => ['samidare.png', 'samidare2.png'],
		'inactive' => ['mada.png'],
		'directMainActions' => [
			'sukara.png' => 30,
			'mira-.png' => 31,
			'sutemi.png' => 33,
			'seisui.png' => 49,
			'meisou.png' => 41,
			'fullheal.png' => 37,
			'more_heal.png' => 32,
			'song.png' => 52,
			'sippuu.png' => 44,
			'sage.png' => 47,
			'elven.png' => 48,
			'flee.png' => 53,
			'tokuyaku.png' => 50,
			'mazinngiri.png' => 66,
			'kaenn.png' => 64,
			'mahilya.png' => 65,
		],
	],
];
