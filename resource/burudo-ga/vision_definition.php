<?php

declare(strict_types=1);

return [
	'id' => 'hexagoon',
	'thresholds' => [
		'whiteSaturationMaxDark' => 0.20,//こっちのほうを小さくないといけない
		'whiteSaturationMaxBright' => 0.25,  //こっちが大きい
		'whiteThreshold' => 0.80,            // ← 少し戻す
        'numberWhiteSaturationMaxDark' => 0.21,
        'numberWhiteSaturationMaxBright' => 0.22,
		'numberThreshold' => 0.80,
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
			'ano2.png' => 25,
			'flee.png' => 53,
		],
	],
];
