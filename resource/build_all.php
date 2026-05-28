<?php

declare(strict_types=1);

require __DIR__ . DIRECTORY_SEPARATOR . 'vision_asset_builder.php';

$resourceRoot = __DIR__;
$publicRoot = dirname(__DIR__) . DIRECTORY_SEPARATOR . 'public';
$result = writeVisionAssetOutputs($resourceRoot, $publicRoot);

fwrite(
    STDOUT,
    "Wrote {$result['jsonPath']} ({$result['jsonBytes']} bytes), "
    . "{$result['jsPath']} ({$result['jsBytes']} bytes), "
    . "{$result['maskAssetCount']} unique masks\n"
);
