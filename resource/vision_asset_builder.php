<?php

declare(strict_types=1);

const WHITE_THRESHOLD = 0.72;
const MATCH_CONTRAST = 1.28;
const MATCH_BIAS = 0.03;
const TEMPLATE_ALPHA_THRESHOLD = 0.05;
const NUMBER_WHITE_THRESHOLD = 140 / 255;
const DEFAULT_VISION_TIMEOUT_MS = 240000;
const VISION_ASSET_HASH_ALGO = 'xxh128';

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

function buildVisionMaskAssetId(string $mask, int $width, int $height): string
{
    if (!in_array(VISION_ASSET_HASH_ALGO, hash_algos(), true)) {
        throw new RuntimeException('Hash algorithm is unavailable: ' . VISION_ASSET_HASH_ALGO);
    }

    return 'm' . $width . 'x' . $height . '_' . hash(
        VISION_ASSET_HASH_ALGO,
        pack('N2', $width, $height) . $mask
    );
}

function registerVisionMaskAsset(array &$assets, string $mask, int $width, int $height): string
{
    $id = buildVisionMaskAssetId($mask, $width, $height);
    $encoded = base64_encode($mask);

    if (isset($assets[$id]) && $assets[$id] !== $encoded) {
        throw new RuntimeException("Vision asset hash collision: {$id}");
    }

    $assets[$id] = $encoded;

    return $id;
}

function discoverPngFiles(string $directory): array
{
    if (!is_dir($directory)) {
        return [];
    }

    $files = [];
    foreach (new FilesystemIterator($directory, FilesystemIterator::SKIP_DOTS) as $file) {
        if (!$file->isFile()) {
            continue;
        }
        if (strtolower($file->getExtension()) !== 'png') {
            continue;
        }
        $files[] = $file->getFilename();
    }

    sort($files, SORT_NATURAL | SORT_FLAG_CASE);

    return $files;
}

function resolveVisionDirectory(string $resourceRoot, string $definitionRoot, array $entry): string
{
    $baseRoot = $definitionRoot;
    if (!empty($entry['sourceBoss'])) {
        $baseRoot = $resourceRoot . DIRECTORY_SEPARATOR . $entry['sourceBoss'];
    }

    return $baseRoot . DIRECTORY_SEPARATOR . $entry['directory'];
}

function loadVisionDefinitions(string $resourceRoot): array
{
    $definitions = [];

    foreach (new FilesystemIterator($resourceRoot, FilesystemIterator::SKIP_DOTS) as $entry) {
        if (!$entry->isDir()) {
            continue;
        }

        $definitionPath = $entry->getPathname() . DIRECTORY_SEPARATOR . 'vision_definition.php';
        if (!is_file($definitionPath)) {
            continue;
        }

        $definition = require $definitionPath;
        if (!is_array($definition) || empty($definition['id'])) {
            throw new RuntimeException("Invalid vision definition: {$definitionPath}");
        }

        $definition['root'] = $entry->getPathname();
        $definitions[$definition['id']] = $definition;
    }

    ksort($definitions, SORT_NATURAL | SORT_FLAG_CASE);

    return $definitions;
}

function buildTemplateEntriesForMode(string $resourceRoot, array $definition, array &$maskAssets): array
{
    $entries = [];

    foreach ($definition['templateGroups'] ?? [] as $group) {
        $directory = resolveVisionDirectory($resourceRoot, $definition['root'], $group);
        foreach (discoverPngFiles($directory) as $file) {
            $path = $directory . DIRECTORY_SEPARATOR . $file;
            $png = loadPngRgba($path);
            $entries[] = [
                'slot' => $group['slot'],
                'file' => $file,
                'width' => $png['width'],
                'height' => $png['height'],
                'maskId' => registerVisionMaskAsset(
                    $maskAssets,
                    buildTemplateMask($png['rgba'], $png['width'], $png['height']),
                    $png['width'],
                    $png['height']
                ),
            ];
        }
    }

    return $entries;
}

function buildIdentifyModeEntries(string $resourceRoot, array $definitions, array &$maskAssets): array
{
    $entries = [];

    foreach ($definitions as $definition) {
        foreach (($definition['identify']['templates'] ?? []) as $template) {
            $path = resolveVisionDirectory($resourceRoot, $definition['root'], $template)
                . DIRECTORY_SEPARATOR
                . $template['file'];
            if (!is_file($path)) {
                continue;
            }
            $png = loadPngRgba($path);
            $entries[] = [
                'slot' => $template['slot'],
                'file' => $template['file'],
                'width' => $png['width'],
                'height' => $png['height'],
                'maskId' => registerVisionMaskAsset(
                    $maskAssets,
                    buildTemplateMask($png['rgba'], $png['width'], $png['height']),
                    $png['width'],
                    $png['height']
                ),
            ];
        }
    }

    return $entries;
}

function buildNumberEntries(string $resourceRoot, array $definitions, array &$maskAssets): array
{
    $entries = [];
    $seen = [];

    foreach ($definitions as $definition) {
        foreach (($definition['numberGroups'] ?? []) as $group) {
            $directory = resolveVisionDirectory($resourceRoot, $definition['root'], $group);
            foreach (discoverPngFiles($directory) as $file) {
                if (isset($seen[$file])) {
                    continue;
                }
                $seen[$file] = true;
                $path = $directory . DIRECTORY_SEPARATOR . $file;
                $png = loadPngRgba($path);
                $entries[] = [
                    'file' => $file,
                'digit' => normalizeDigitFileName($file),
                'width' => $png['width'],
                'height' => $png['height'],
                'maskId' => registerVisionMaskAsset(
                    $maskAssets,
                    buildNumberMask($png['rgba'], $png['width'], $png['height']),
                    $png['width'],
                    $png['height']
                ),
            ];
        }
    }
    }

    usort($entries, static function (array $left, array $right): int {
        return strnatcasecmp($left['file'], $right['file']);
    });

    return $entries;
}

function normalizeModeDefinitionForPack(array $definition, array $templates): array
{
    return [
        'id' => $definition['id'],
        'names' => $definition['names'] ?? ['ja' => $definition['id'], 'en' => $definition['id']],
        'picker' => $definition['picker'] ?? $definition['id'],
        'timeoutMs' => $definition['timeoutMs'] ?? DEFAULT_VISION_TIMEOUT_MS,
        'battleEmulator' => $definition['battleEmulator'] ?? null,
        'rules' => $definition['rules'] ?? new stdClass(),
        'identify' => $definition['identify'] ?? ['templates' => []],
        'thresholds' => $definition['thresholds'] ?? null,  // ← 追加
        'matchRois' => $definition['matchRois'] ?? null,
        'recognizedCrops' => $definition['recognizedCrops'] ?? null,
        'templates' => $templates,
    ];
}
function buildVisionAssetPack(string $resourceRoot): array
{
    $definitions = loadVisionDefinitions($resourceRoot);
    if (!$definitions) {
        throw new RuntimeException("No vision definitions found in {$resourceRoot}");
    }

    $maskAssets = [];
    $modes = [];
    foreach ($definitions as $definition) {
        $templates = buildTemplateEntriesForMode($resourceRoot, $definition, $maskAssets);
        $modes[] = normalizeModeDefinitionForPack($definition, $templates);
    }

    array_unshift($modes, [
        'id' => 'identify',
        'names' => ['ja' => '識別モード', 'en' => 'Identify Mode'],
        'picker' => 'identify',
        'timeoutMs' => 0,
        'battleEmulator' => null,
        'rules' => [
            'detections' => array_values(array_filter(array_map(
                static function (array $definition): ?array {
                    if (empty($definition['identify']['templates'])) {
                        return null;
                    }
                    return [
                        'modeId' => $definition['id'],
                        'templates' => $definition['identify']['templates'],
                    ];
                },
                $definitions
            ))),
        ],
        'identify' => ['templates' => []],
        'templates' => buildIdentifyModeEntries($resourceRoot, $definitions, $maskAssets),
    ]);

    $numberTemplates = buildNumberEntries($resourceRoot, $definitions, $maskAssets);
    ksort($maskAssets, SORT_NATURAL | SORT_FLAG_CASE);

    return [
        'version' => 3,
        'generatedAt' => gmdate(DATE_ATOM),
        'assets' => $maskAssets,
        'modes' => $modes,
        'numberTemplates' => $numberTemplates,
    ];
}

function writeVisionAssetOutputs(string $resourceRoot, string $publicRoot): array
{
    $payload = buildVisionAssetPack($resourceRoot);
    $json = json_encode($payload, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
    $js = "window.__VISION_ASSET_PACK__ = {$json};\n";

    $jsonPath = $publicRoot . DIRECTORY_SEPARATOR . 'vision-assets.json';
    $jsPath = $publicRoot . DIRECTORY_SEPARATOR . 'vision-assets.js';

    file_put_contents($jsonPath, $json);
    file_put_contents($jsPath, $js);

    return [
        'jsonPath' => $jsonPath,
        'jsPath' => $jsPath,
        'jsonBytes' => strlen($json),
        'jsBytes' => strlen($js),
        'maskAssetCount' => count($payload['assets']),
    ];
}
