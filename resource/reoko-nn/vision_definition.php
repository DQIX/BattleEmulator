<?php

declare(strict_types=1);

return [
	'id' => 'WightKnight',
	'thresholds' => [
		'templateThreshold' => 0.45,
		'resetLatchClearScore' => 0.6,
		'whiteThreshold' => 0.72,
		'matchWhiteThresholdDark' => 0.50,
		'matchWhiteThresholdBright' => 0.61,
		'whiteSaturationMaxDark' => 0.25,
		'whiteSaturationMaxBright' => 0.40,
		'whiteSaturationDarkValue' => 0.13,
		'numberWhiteSaturationMaxDark' => 0.44,
		'numberWhiteSaturationMaxBright' => 0.43,
		'numberWhiteThresholdBright' => 0.7,
		'whiteSaturationBrightValue' => 0.9,
		'numberWhiteThresholdDark' => 0.59,
		'actionThreshold' => 0.58,
		'numberThreshold' => 0.65,
		'matchPenaltyWeight' => 0.0,
		'matchWhiteWeight' => 1.0,
		'templateAlphaThreshold' => 0.05,
	],
	'names' => [
		'ja' => 'レオコーンモード',
		'en' => 'Wight Knight Mode',
	],
	'picker' => 'WightKnight',
	'battleEmulator' => [
		 'id' => 'reokonn_v6',
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
		[
			'slot' => 'main',
			'directory' => 'message_v2',
			'sourceBoss' => 'star',
		],
		[
			'slot' => 'target',
			'directory' => 'target',
			'sourceBoss' => 'star',
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
				'file' => 'nazono.png',
			]
		],
	],
	'matchRois' => [
		'sourceWidth' => 958,
		'sourceHeight' => 718,
		'slots' => [
			'main' => ['x' => 78, 'y' => 648, 'sz' => 160, 'zy' => 70],
			'sub' => ['x' => 430, 'y' => 645, 'sz' => 170, 'zy' => 55],
		],
	],
	'recognizedCrops' => [
		'sourceWidth' => 958,
		'sourceHeight' => 718,
		'slots' => [
			'sub' => ['sz' => 130, 'zy' => 45],
		],
	],
	'rules' => [
	    'Main' => [
			'nazono.png'
		],
		'resetSub' => [
			'reset.png'
		],
		'attack' => [
			'attack.png'
		],
		'star' => [
			'star.png'
		],
		'ana' => [
			'aha.png'
		],
		'ano' => [
			'ano.png'
		],
		'directMainActions' => [
			'ano_v2.png' => 25,
			'ano.png' => 25,
			'flee.png' => 53,
			'gareki.png' => 150,
			'heal.png' => 26,
			'seisui.png' => 151,
			'seisui2.png' => 151,
			'yakusou.png' => 23,
			'bougilyo.png' => 27,
			'samida4.png' => 80,
			'inazu.png' => 78,
		],
	],
];
