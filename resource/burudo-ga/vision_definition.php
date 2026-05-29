<?php

declare(strict_types=1);

return [
	'id' => 'hexagoon',
	'thresholds' => [
		'whiteSaturationMaxDark' => 0.17,//こっちのほうを小さくないといけない
		'whiteSaturationMaxBright' => 0.14,  //こっちが大きい
        "matchWhiteThresholdBright" => 0.75,
		"matchWhiteThresholdDark" => 0.75,
		'numberWhiteSaturationMaxDark' => 0.22,
		'numberWhiteSaturationMaxBright' => 0.18,
		'numberWhiteThresholdBright' => 0.80,  // 0.91から下げる
		'numberWhiteThresholdDark' => 0.70,    // 0.82から下げる
	],
	'names' => [
		'ja' => 'ブルドーガモード',
		'en' => 'hexagoon Mode',
	],
	'picker' => 'hexagoon',
	'battleEmulator' => [
		 'id' => 'burudoga_v6',
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
		[
            'directory' => 'numbers',
        ],
	],
	'identify' => [
		'templates' => [
			[
				'slot' => 'main',
				'directory' => 'message_v2',
				'file' => 'burudo1.png',
			]
		],
	],
	'rules' => [
	    'Main' => [
			'burudo1.png'
		],
		'resetSub' => [
			'reset.png'
		],
		'directMainActions' => [
			'ano2.png' => 25,
			'flee.png' => 53,
			'gareki.png' => 150,
			'heal.png' => 26,
			'seisui.png' => 151,
			'seisui2.png' => 151,
			'yakusou.png' => 23,
		],
	],
];
