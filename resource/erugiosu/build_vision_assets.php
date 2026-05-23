<?php

declare(strict_types=1);

require dirname(__DIR__) . DIRECTORY_SEPARATOR . 'vision_asset_builder.php';

$resourceRoot = dirname(__DIR__);
$publicRoot = dirname($resourceRoot) . DIRECTORY_SEPARATOR . 'public';
$result = writeVisionAssetOutputs($resourceRoot, $publicRoot);

fwrite(
    STDOUT,
    "Wrote {$result['jsonPath']} ({$result['jsonBytes']} bytes) and {$result['jsPath']} ({$result['jsBytes']} bytes)\n"
);
