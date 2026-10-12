<?php

declare(strict_types=1);

return [
	'id' => 'hexagoon',
	'thresholds' => [
		'templateThreshold' => 0.45,
		'resetLatchClearScore' => 0.6,
		'whiteThreshold' => 0.72,
		'matchWhiteThresholdDark' => 0.59,
		'matchWhiteThresholdBright' => 0.78,
		'whiteSaturationMaxDark' => 0.85,
		'whiteSaturationMaxBright' => 0.12,
		'whiteSaturationDarkValue' => 0.0,
		'numberWhiteSaturationMaxDark' => 0.22,
		'numberWhiteSaturationMaxBright' => 0.15,
		'numberWhiteThresholdBright' => 0.7,
		'whiteSaturationBrightValue' => 0.9,
		'numberWhiteThresholdDark' => 0.6,
		'actionThreshold' => 0.58,
		'numberThreshold' => 0.57,
		'matchPenaltyWeight' => 0.0,
		'matchWhiteWeight' => 1.0,
		'templateAlphaThreshold' => 0.05,
	],
	'names' => [
		'ja' => 'ブルドーガモード',
		'en' => 'hexagoon Mode',
	],
	'picker' => 'hexagoon',
	'battleEmulator' => [
		 'id' => 'burudoga_v6',
	],
	'turnActions' => [
		'enemy' => [
			'odd' => 1,
			'even' => 1,
		],
		'ally' => 1,
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
