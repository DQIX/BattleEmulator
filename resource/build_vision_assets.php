<?php

declare(strict_types=1);

const WHITE_THRESHOLD = 0.72;
const MATCH_CONTRAST = 1.28;
const MATCH_BIAS = 0.03;
const TEMPLATE_ALPHA_THRESHOLD = 0.05;
const NUMBER_WHITE_THRESHOLD = 140 / 255;

const TEMPLATE_GROUPS = [
    [
        'slot' => 'main',
        'directory' => 'message_v2',
        'files' => [
            'ano.png',
            'ayasii.png',
            'critical.png',
            'defense_champion.png',
            'elven.png',
            'erugio.png',
            'erugio2.png',
            'erugio4.png',
            'flee.png',
            'fullheal.png',
            'guard.png',
            'hadou.png',
            'ice.png',
            'kagayaku.png',
            'kuroi.png',
            'madannte.png',
            'meisou.png',
            'merazoma.png',
            'mikawasi.png',
            'mira-.png',
            'miss.png',
            'miss2.png',
            'more_heal.png',
            'mp2.png',
            'no_hadou.png',
            'Paralysis.png',
            'sage.png',
            'samidare.png',
            'samidare2.png',
            'seisui.png',
            'sippuu.png',
            'sleeping2.png',
            'song.png',
            'sukara.png',
            'sutemi.png',
            'tameru.png',
            'tokuyaku.png',
            'WakeUp.png',
            'WakeUp2.png',
            'WakeUp3.png',
            'yaketuku.png',
            'zigosupa.png',
            'zilyoukuu.png',
        ],
    ],
    [
        'slot' => 'sub',
        'directory' => 'submessage_v2',
        'files' => [
            'attack.png',
            'defense_champion2.png',
            'inori.png',
            'Paralysis2.png',
            'reset.png',
            'uhsc.png',
        ],
    ],
    [
        'slot' => 'ally',
        'directory' => 'sub2message_v2',
        'files' => ['a_attack.png', 'CareParalysis.png', 'dead.png', 'dead2.png'],
    ],
    [
        'slot' => 'target',
        'directory' => 'target',
        'files' => ['aha.png', 'erugio.png', 'erugio2.png', 'erugio4.png'],
    ],
];

const NUMBER_TEMPLATE_FILES = [
    '0.png',
    '0_2.png',
    '0_3.png',
    '0_4.png',
    '0_zep1.png',
    '0_zpe2.png',
    '1.png',
    '1_1.png',
    '1_10.png',
    '1_11.png',
    '1_12.png',
    '1_2.png',
    '1_3.png',
    '1_4.png',
    '1_5.png',
    '1_6.png',
    '1_7.png',
    '1_8.png',
    '1_9.png',
    '1_zep1.png',
    '1_zepp1.png',
    '1_zepp13.png',
    '1_zepp14.png',
    '1_zepp16.png',
    '1_zepp17.png',
    '2.png',
    '2_1.png',
    '2_2.png',
    '2_3.png',
    '2_4.png',
    '2_5.png',
    '2_zep1.png',
    '2_zep2.png',
    '2_zep4.png',
    '2_zepp2.png',
    '3.png',
    '3_1.png',
    '3_2.png',
    '3_3.png',
    '3_4.png',
    '3_5.png',
    '3_6.png',
    '3_7.png',
    '3_zep1.png',
    '3_zep3.png',
    '4.png',
    '4_1.png',
    '4_2.png',
    '4_3.png',
    '4_4.png',
    '4_5.png',
    '4_6.png',
    '4_zep1.png',
    '4_zep2.png',
    '4_zep4.png',
    '4_zep5.png',
    '4_zpp5.png',
    '5.png',
    '5_2.png',
    '5_zep1.png',
    '5_zep5.png',
    '5_zepp1.png',
    '6.png',
    '6_1.png',
    '6_12.png',
    '6_zep2.png',
    '6_zepp1.png',
    '6_zepppp.png',
    '7.png',
    '7_2.png',
    '7_3.png',
    '7_4.png',
    '7_zep1.png',
    '7_zeppp.png',
    '7_zpe2.png',
    '8.png',
    '8_1.png',
    '8_3.png',
    '8_4.png',
    '8_5.png',
    '8_6.png',
    '8_zep1.png',
    '8_zep7.png',
    '8_zepp.png',
    '9.png',
    '9_2.png',
    '9_zep1.png',
    '9_zep2.png',
    '9_zepp.png',
];

function preprocessLuminance(int $r, int $g, int $b): float
{
    $luminance = ($r * 0.2126 + $g * 0.7152 + $b * 0.0722) / 255;
    return max(0.0, min(1.0, ($luminance - 0.5) * MATCH_CONTRAST + 0.5 + MATCH_BIAS));
}

function isWhitePixel(int $r, int $g, int $b, int $a, float $threshold, float $alphaThreshold): bool
{
    if (($a / 255) < $alphaThreshold) {
        return false;
    }

    return preprocessLuminance($r, $g, $b) >= $threshold;
}

function loadPngRgba(string $path): array
{
    $image = @imagecreatefrompng($path);
    if (!$image) {
        throw new RuntimeException("PNG load failed: {$path}");
    }

    $width = imagesx($image);
    $height = imagesy($image);
    $rgba = '';

    for ($y = 0; $y < $height; $y++) {
        for ($x = 0; $x < $width; $x++) {
            $colorIndex = imagecolorat($image, $x, $y);
            $color = imagecolorsforindex($image, $colorIndex);
            $r = $color['red'];
            $g = $color['green'];
            $b = $color['blue'];
            $alpha127 = $color['alpha'];
            $a = (int) round((127 - $alpha127) * 255 / 127);
            $rgba .= chr($r) . chr($g) . chr($b) . chr(max(0, min(255, $a)));
        }
    }

    imagedestroy($image);

    return [
        'width' => $width,
        'height' => $height,
        'rgba' => $rgba,
    ];
}

function buildTemplateMask(string $rgba, int $width, int $height): string
{
    $mask = '';
    $pixelCount = $width * $height;

    for ($index = 0; $index < $pixelCount; $index++) {
        $offset = $index * 4;
        $mask .= chr(isWhitePixel(
            ord($rgba[$offset]),
            ord($rgba[$offset + 1]),
            ord($rgba[$offset + 2]),
            ord($rgba[$offset + 3]),
            WHITE_THRESHOLD,
            TEMPLATE_ALPHA_THRESHOLD
        ) ? 1 : 0);
    }

    return $mask;
}

function buildNumberMask(string $rgba, int $width, int $height): string
{
    $mask = '';
    $pixelCount = $width * $height;

    for ($index = 0; $index < $pixelCount; $index++) {
        $offset = $index * 4;
        $luminance = (
            ord($rgba[$offset]) * 0.2126
            + ord($rgba[$offset + 1]) * 0.7152
            + ord($rgba[$offset + 2]) * 0.0722
        ) / 255;
        $mask .= chr($luminance >= NUMBER_WHITE_THRESHOLD ? 1 : 0);
    }

    return $mask;
}

function normalizeDigitFileName(string $file): int
{
    return (int) explode('_', $file, 2)[0];
}

function buildTemplateEntries(string $resourceRoot): array
{
    $entries = [];

    foreach (TEMPLATE_GROUPS as $group) {
        foreach ($group['files'] as $file) {
            $path = $resourceRoot . DIRECTORY_SEPARATOR . $group['directory'] . DIRECTORY_SEPARATOR . $file;
            $png = loadPngRgba($path);
            $entries[] = [
                'slot' => $group['slot'],
                'file' => $file,
                'width' => $png['width'],
                'height' => $png['height'],
                'mask' => base64_encode(buildTemplateMask($png['rgba'], $png['width'], $png['height'])),
            ];
        }
    }

    return $entries;
}

function buildNumberEntries(string $resourceRoot): array
{
    $entries = [];

    foreach (NUMBER_TEMPLATE_FILES as $file) {
        $path = $resourceRoot . DIRECTORY_SEPARATOR . 'numbers' . DIRECTORY_SEPARATOR . $file;
        $png = loadPngRgba($path);
        $entries[] = [
            'file' => $file,
            'digit' => normalizeDigitFileName($file),
            'width' => $png['width'],
            'height' => $png['height'],
            'mask' => base64_encode(buildNumberMask($png['rgba'], $png['width'], $png['height'])),
        ];
    }

    return $entries;
}

$resourceRoot = __DIR__;
$outputPath = dirname(__DIR__) . DIRECTORY_SEPARATOR . 'public' . DIRECTORY_SEPARATOR . 'vision-assets.json';

$payload = [
    'version' => 1,
    'generatedAt' => gmdate(DATE_ATOM),
    'templates' => buildTemplateEntries($resourceRoot),
    'numberTemplates' => buildNumberEntries($resourceRoot),
];

$json = json_encode($payload, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
file_put_contents($outputPath, $json);

fwrite(STDOUT, "Wrote {$outputPath} (" . strlen($json) . " bytes)\n");
