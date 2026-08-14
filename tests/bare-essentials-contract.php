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

$plugin = $read($root . '/elementize.php');
$bootstrap = $read($root . '/includes/elementize-bootstrap.inc');
$content = $read($root . '/includes/elementize-content.inc');
$assert(str_contains($content, "private const KINDS = [ 'text', 'color', 'media', 'pixfort_icon' ];"), 'Only text/color/media/icon kinds may be writable.');
$assert(!str_contains($content, "'link' =>"), 'Link editing must not return as a writable kind.');
$assert(str_contains($plugin, 'Elementize_Bootstrap::init();'), 'Plugin entrypoint must use the lightweight bootstrap.');
$assert(str_contains($plugin, '[ Elementize_Bootstrap::class, \'activate\' ]'), 'Activation must lazy-load through the bootstrap.');
$assert(str_contains($bootstrap, "add_action( 'rest_api_init', [ self::class, 'register_rest_routes' ], 1 )"), 'REST classes must be lazy-loaded only when REST initializes.');
$assert(str_contains($bootstrap, 'if ( is_admin() ) self::load_admin();'), 'Admin setup must load only in wp-admin.');
$assert(!str_contains($content, "add_action( 'rest_api_init'"), 'Content module must not register redundant REST hooks.');

$core = $read($root . '/includes/elementize-core.inc');
$mediaImport = $read($root . '/includes/elementize-media-import.inc');
$icons = $read($root . '/includes/elementize-pixfort-icons.inc');
$themeTokens = $read($root . '/includes/elementize-pixfort-theme-tokens.inc');
$assert(str_contains($core, "'design_intelligence' => false"), 'Status must explicitly report design intelligence disabled.');
$assert(str_contains($core, "'visual_qa_agent' => false"), 'Status must explicitly report visual QA disabled.');
$assert(str_contains($core, 'Elementize_Media_Import::status_capabilities()'), 'Status must compose media capabilities directly.');
$assert(str_contains($core, 'Elementize_Pixfort_Icons::status_capabilities()'), 'Status must compose icon capabilities directly.');
$assert(!str_contains($mediaImport, 'rest_request_after_callbacks'), 'Media import must not hook every REST response just to annotate status.');
$assert(!str_contains($icons, 'rest_request_after_callbacks'), 'Icon search must not hook every REST response just to annotate status.');
$assert(!str_contains($themeTokens, '$diagnostics'), 'Removed Pixfort diagnostic state must not return.');
$assert(!str_contains($themeTokens, 'validate_any_transition'), 'Unused cross-widget semantic transition helper must stay removed.');
$assert(!str_contains($themeTokens, 'is_any_control'), 'Unused cross-widget semantic-control scan must stay removed.');

$onboarding = $read($root . '/includes/elementize-onboarding.inc');
$assert(str_contains($onboarding, 'elementize-public-origin.txt'), 'Persistent relay origin file must be supported.');
$assert(str_contains($onboarding, 'install-free-cloudflare-relay.ps1'), 'Setup must point at the free Cloudflare relay installer.');
$assert(str_contains($onboarding, 'self::is_quick_tunnel( $stored )'), 'A stale saved Quick Tunnel must be detected so a stable relay can take precedence.');
$assert(str_contains($onboarding, 'admin_post_elementize_relay_control'), 'Admin relay controls must stay available after persistent setup.');
$assert(str_contains($onboarding, 'Turn off completely'), 'Admin relay controls must provide one-click full shutdown.');
$assert(str_contains($onboarding, 'Pause until next Windows sign-in'), 'Admin relay controls must support a temporary pause.');
$assert(str_contains($onboarding, 'run_relay_controller'), 'Admin relay controls must use the guarded Windows relay controller.');
$assert(str_contains($onboarding, 'elementize-relay-runtime.json') && str_contains($onboarding, 'relay_runtime_pointer'), 'Admin relay controls must consume the installer-written runtime pointer.');

$worker = $read($root . '/tools/cloudflare-relay/worker.js');
$assert(str_contains($worker, "startsWith('/wp-json/elementize/v1/')"), 'Cloudflare relay must be restricted to the Elementize REST namespace.');

$installer = $read($root . '/tools/windows/install-free-cloudflare-relay.ps1');
$starter = $read($root . '/tools/windows/start-free-cloudflare-relay.ps1');
$controller = $read($root . '/tools/windows/control-free-cloudflare-relay.ps1');
$assert(str_contains($installer, 'C:\\Program Files\\nodejs\\node.exe'), 'Windows installer must prefer the permanent system Node.js runtime.');
$assert(str_contains($installer, 'nodeDir = $nodeDir'), 'Windows installer must persist the Node.js directory for restart-safe startup.');
$assert(str_contains($installer, "Substring(0, 12)"), 'workers.dev account subdomain must use at least 12 account-ID hex characters.');
$assert(str_contains($installer, '$siteHash = Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8'), 'Worker identity must include a deterministic site-origin hash.');
$assert(str_contains($installer, '$WorkerName = "elementize-relay-$siteKey"'), 'Default Worker name must be unique per site.');
$assert(str_contains($installer, 'Join-Path (Join-Path $elementizeRoot \'sites\') $siteKey'), 'Each site must have its own local relay runtime directory.');
$assert(str_contains($installer, '$projectDir = Join-Path $runtimeRoot \'worker\''), 'Worker deploy source must live outside the Wrangler toolchain.');
$assert(str_contains($installer, '$wranglerDir = Join-Path $runtimeRoot \'wrangler\''), 'Wrangler must have a separate tooling directory.');
$assert(str_contains($installer, 'Wrangler: reuse'), 'A healthy local Wrangler install must be reused instead of reinstalled on every setup run.');
$assert(str_contains($installer, 'existingStableOrigin'), 'Reinstall must preserve an already-known stable workers.dev URL.');
$assert(str_contains($installer, 'projectDir = $projectDir') && str_contains($installer, 'wranglerDir = $wranglerDir'), 'Relay settings must persist separate Worker and Wrangler directories.');
$assert(str_contains($installer, 'siteKey = $siteKey'), 'Per-site runtime settings must persist the site key.');
$assert(str_contains($installer, '("Elementize-$siteKey.cmd")'), 'Windows startup entry must be unique per site.');
$assert(str_contains($installer, 'workersDevSubdomain = $workersDevSubdomain'), 'Windows installer must persist an explicit first-run workers.dev subdomain.');
$assert(str_contains($installer, 'Subdomain -> type EXACTLY:'), 'Windows installer must explain the exact workers.dev answer before Wrangler prompts.');
$assert(str_contains($installer, 'control-free-cloudflare-relay.ps1') && str_contains($installer, '-Action start'), 'Windows installer must hand runtime startup to the shared relay controller.');
$assert(str_contains($installer, 'elementize-relay-runtime.json') && str_contains($installer, 'runtimeRoot = $runtimeRoot') && str_contains($installer, 'startupCmd = $startupCmd'), 'Installer must persist a web-context-safe relay runtime pointer.');
$assert(str_contains($starter, '$runtimeRoot = Split-Path -Parent $PSCommandPath'), 'Relay startup must bind itself to its per-site runtime directory.');
$assert(str_contains($starter, '$mutexName = \'Local\\ElementizeCloudflareRelay_\' + $siteKey'), 'Relay monitor mutex must be unique per site.');
$assert(str_contains($starter, '$projectDir = [string]$settings.projectDir'), 'Relay startup must deploy from the isolated Worker project directory.');
$assert(str_contains($starter, '$wranglerDir = [string]$settings.wranglerDir'), 'Relay startup must execute Wrangler from the isolated tooling directory.');
$assert(str_contains($starter, 'Push-Location $projectDir'), 'Wrangler deploy must run from the Worker-only source directory.');
$assert(str_contains($starter, '$nodeDir = [string]$settings.nodeDir'), 'Relay startup must restore the persisted Node.js directory.');
$assert(str_contains($starter, 'CommandLine.Contains($quickLog)'), 'Relay startup must only stop its own site-specific cloudflared process.');
$assert(str_contains($starter, 'Subdomain -> $workersDevSubdomain'), 'Relay startup must repeat the exact workers.dev answer at first publish.');
$assert(str_contains($controller, "ValidateSet('status','start','stop','enable','disable','autostart-on','autostart-off')"), 'Relay controller must expose the intended local lifecycle actions only.');
$assert(str_contains($controller, 'Stop-LegacyRelayForSite'), 'Relay controller must retire matching legacy Elementize relay processes.');
$assert(str_contains($controller, '$hostMatches.Count -eq 1'), 'Relay controller may fall back to a unique host match when WordPress reports a different local scheme.');
$assert(str_contains($controller, '$env:USERPROFILE') && str_contains($controller, "'AppData\\Local'"), 'Relay controller must recover LocalAppData when PHP-CGI omits LOCALAPPDATA.');
$assert(str_contains($controller, '[string]$RuntimeRoot') && str_contains($controller, '[string]$StartupCmd'), 'Relay controller must accept explicit installer-resolved runtime paths.');
$assert(str_contains($controller, "'disable' { Stop-Relay; Remove-Item"), 'Full relay shutdown must stop the relay and disable Windows autostart.');
$assert(str_contains($controller, "'enable' { Write-Autostart; Start-Relay }"), 'Full relay startup must enable Windows autostart and start the relay.');

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
