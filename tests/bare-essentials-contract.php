<?php
$root = dirname(__DIR__);
$failures = [];
$assert = static function (bool $ok, string $message) use (&$failures): void {
    if (!$ok) $failures[] = $message;
};
$read = static fn(string $path): string => (string) file_get_contents($path);

$expectedIncludes = [
    'elementize-bootstrap.inc',
    'elementize-content.inc',
    'elementize-core.inc',
    'elementize-media-import.inc',
    'elementize-media-library.inc',
    'elementize-onboarding.inc',
    'elementize-pixfort-icons.inc',
    'elementize-pixfort-theme-tokens.inc',
];
$actualIncludes = array_map('basename', glob($root . '/includes/*.inc') ?: []);
sort($expectedIncludes); sort($actualIncludes);
$assert($actualIncludes === $expectedIncludes, 'Runtime includes must stay at the bare-essential set.');

foreach (['frontend', 'assets'] as $removedDir) {
    $assert(!is_dir($root . '/' . $removedDir), "$removedDir must not return to the runtime.");
}

$schema = $read($root . '/config/gpt/actions.openapi.yaml');
preg_match_all('/^\s*operationId:\s*([A-Za-z0-9_]+)/m', $schema, $matches);
$actualOps = $matches[1] ?? [];
$expectedOps = [
    'getElementizeStatus',
    'listElementizePages',
    'getElementizePageContent',
    'updateElementizePageContent',
    'searchElementizeMediaImages',
    'importElementizeConversationImage',
    'searchElementizePixfortIcons',
];
$assert($actualOps === $expectedOps, 'GPT Action surface must remain exactly seven operations.');

foreach (['Creative', 'VisualQA', 'Designer', 'ComponentCandidates', 'Template', 'Activity', 'Undo'] as $forbidden) {
    $assert(stripos(implode('|', $actualOps), $forbidden) === false, "Removed operation family returned: $forbidden");
}

$content = $read($root . '/includes/elementize-content.inc');
$assert(str_contains($content, "private const KINDS = [ 'text', 'color', 'media', 'pixfort_icon' ];"), 'Only text/color/media/icon kinds may be writable.');
$assert(!str_contains($content, "'link' =>"), 'Link editing must not return as a writable kind.');

$core = $read($root . '/includes/elementize-core.inc');
$assert(str_contains($core, "'design_intelligence' => false"), 'Status must explicitly report design intelligence disabled.');
$assert(str_contains($core, "'visual_qa_agent' => false"), 'Status must explicitly report visual QA disabled.');

$onboarding = $read($root . '/includes/elementize-onboarding.inc');
$assert(str_contains($onboarding, 'elementize-public-origin.txt'), 'Persistent relay origin file must be supported.');
$assert(str_contains($onboarding, 'install-free-cloudflare-relay.ps1'), 'Setup must point at the free Cloudflare relay installer.');
$assert(str_contains($onboarding, 'self::is_quick_tunnel( $stored )'), 'A stale saved Quick Tunnel must be detected so a stable relay can take precedence.');

$worker = $read($root . '/tools/cloudflare-relay/worker.js');
$assert(str_contains($worker, "startsWith('/wp-json/elementize/v1/')"), 'Cloudflare relay must be restricted to the Elementize REST namespace.');

$installer = $read($root . '/tools/windows/install-free-cloudflare-relay.ps1');
$starter = $read($root . '/tools/windows/start-free-cloudflare-relay.ps1');
$assert(str_contains($installer, 'C:\\Program Files\\nodejs\\node.exe'), 'Windows installer must prefer the permanent system Node.js runtime.');
$assert(str_contains($installer, 'nodeDir = $nodeDir'), 'Windows installer must persist the Node.js directory for restart-safe startup.');
$assert(str_contains($installer, 'workersDevSubdomain = $workersDevSubdomain'), 'Windows installer must persist an explicit first-run workers.dev subdomain.');
$assert(str_contains($installer, 'Subdomain -> type EXACTLY:'), 'Windows installer must explain the exact workers.dev answer before Wrangler prompts.');
$assert(str_contains($starter, '$nodeDir = [string]$settings.nodeDir'), 'Relay startup must restore the persisted Node.js directory.');
$assert(str_contains($starter, 'Subdomain -> $workersDevSubdomain'), 'Relay startup must repeat the exact workers.dev answer at first publish.');

$plugin = $read($root . '/elementize.php');
preg_match('/Version:\s*([0-9.]+)/', $plugin, $pluginVersion);
preg_match('/define\(\s*\'ELEMENTIZE_VERSION\',\s*\'([0-9.]+)\'/', $plugin, $runtimeVersion);
preg_match('/^\s*version:\s*([0-9.]+)/m', $schema, $schemaVersion);
$versions = [$pluginVersion[1] ?? '', $runtimeVersion[1] ?? '', $schemaVersion[1] ?? ''];
$assert(count(array_unique($versions)) === 1 && '' !== $versions[0], 'Plugin and GPT schema versions must stay synchronized.');

if ($failures) {
    fwrite(STDERR, "Bare essentials contract failed:\n- " . implode("\n- ", $failures) . "\n");
    exit(1);
}
echo "Bare essentials contract OK\n";
