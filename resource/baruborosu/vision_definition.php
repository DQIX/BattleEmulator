<?php

declare(strict_types=1);

return [
	'id' => 'baruborosu',
	'thresholds' => [
		'whiteSaturationMaxDark' => 0.13,//こっちのほうを小さくないといけない
		'whiteSaturationMaxBright' => 0.20,  //こっちが大きい
        "matchWhiteThresholdBright" => 0.70,
		"matchWhiteThresholdDark" => 0.50,
		'whiteThreshold' => 0.60,
        'numberWhiteSaturationMaxDark' => 0.22,
        'numberWhiteSaturationMaxBright' => 0.15,
        'numberWhiteThresholdBright' => 0.70,  // 0.91から下げる
        'numberWhiteThresholdDark' => 0.60,    // 0.82から下げる
	],
	'names' => [
		'ja' => 'バルボロスモード',
		'en' => 'barbarus Mode',
	],
	'picker' => 'baruborosu',
	'battleEmulator' => [
		 'id' => 'baruborosu_gouketu',
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
				'file' => 'baruborosu.png',
			]
		],
	],
	'rules' => [
		'Main' => ['baruborosu.png'],
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
			'dorumo.png' => 153,
			'surudo.png' => 85,
			'yamino.png' => 154,
		],
	],
];
