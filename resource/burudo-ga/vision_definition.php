<?php

declare(strict_types=1);

return [
	'id' => 'hexagoon',
	'thresholds' => [
		'whiteSaturationMaxDark' => 0.20,//こっちのほうを小さくないといけない
		'whiteSaturationMaxBright' => 0.25,  //こっちが大きい
		'whiteThreshold' => 0.60,            // ← 少し戻す
		'numberWhiteThresholdDark' => 0.23,
		'numberWhiteThresholdBright' => 0.27,
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
