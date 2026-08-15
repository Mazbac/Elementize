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
$assert((bool) preg_match('/^components:\R\s{2}schemas:\s*\{\}\R\s{2}securitySchemes:/m', $schema), 'GPT Builder compatibility requires components.schemas to be an explicit object before securitySchemes.');
$assert((bool) preg_match('/^security:\R\s{2}- basicAuth:\s*\[\]/m', $schema), 'GPT Action schema must keep the explicit Basic Auth security declaration.');

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
$assert(str_contains($onboarding, 'elementize-public-origin.txt'), 'Persistent workers.dev origin file must stay supported.');
$assert(str_contains($onboarding, 'install-stable-relay.ps1'), 'Setup must point at the stable relay installer.');
$assert(str_contains($onboarding, 'control-stable-relay.ps1'), 'Admin lifecycle controls must use the stable relay controller.');
$assert(str_contains($onboarding, 'One-time relay migration required'), 'Legacy Quick Tunnel installs must be presented as a migration, not as an endless starting state.');
$assert(str_contains($onboarding, 'Transport: <strong>ngrok stable domain</strong>'), 'Admin must identify the stable ngrok transport.');
$assert(!str_contains($onboarding, 'install-free-cloudflare-relay.ps1'), 'Removed Quick Tunnel installer must not return to onboarding.');
$assert(str_contains($onboarding, 'Checking automatically every 10 seconds') && str_contains($onboarding, 'window.location.reload()'), 'Starting relay state must refresh itself automatically until it settles.');
$assert(str_contains($onboarding, 'elementize-relay-runtime.json') && str_contains($onboarding, 'relay_runtime_pointer'), 'Admin relay controls must consume the installer-written runtime pointer.');

$worker = $read($root . '/tools/cloudflare-relay/worker.js');
$assert(str_contains($worker, "startsWith('/wp-json/elementize/v1/')"), 'Cloudflare Worker must remain restricted to the Elementize REST namespace.');
$assert(str_contains($worker, "ngrok-skip-browser-warning"), 'Worker must bypass the free ngrok browser interstitial for API traffic.');
$assert(str_contains($worker, 'ERR_NGROK_') && str_contains($worker, 'relay_unreachable'), 'Worker must convert offline ngrok endpoints into a controlled 503.');

$installer = $read($root . '/tools/windows/install-stable-relay.ps1');
$starter = $read($root . '/tools/windows/start-ngrok-relay.ps1');
$controller = $read($root . '/tools/windows/control-stable-relay.ps1');
$assert(str_contains($installer, '--id Ngrok.Ngrok --exact'), 'Stable relay installer must install ngrok through WinGet when missing.');
$assert(str_contains($installer, "Microsoft\\WinGet\\Packages") && str_contains($installer, "Ngrok.Ngrok_*"), 'Installer must resolve the WinGet package binary in the same PowerShell session.');
$assert(str_contains($installer, 'Get-AuthenticodeSignature') && str_contains($installer, "SignerCertificate.Subject -notmatch 'O=\"?ngrok, Inc\\.\"?'"), 'Installer must verify the ngrok Authenticode signature before execution.');
$assert(!str_contains($installer, 'bin.equinox.io') && !str_contains($installer, 'Expand-Archive'), 'Installer must not auto-download a second ngrok executable outside WinGet.');
$assert(str_contains($installer, "[Version]'3.12.1'") && str_contains($installer, '& $ngrok update'), 'Installer must upgrade old ngrok agents through ngrok built-in updater before using Traffic Policy.');
$assert(str_contains($installer, "'version: 2'") && !str_contains($installer, "'version: 3'"), 'ngrok runtime config must stay on broadly compatible config v2.');
$assert(str_contains($installer, "provider = 'ngrok'"), 'Relay settings must explicitly identify the ngrok transport.');
$assert(str_contains($installer, "req.url.path.startsWith(''/wp-json/elementize/v1/'')"), 'ngrok Traffic Policy must deny paths outside the Elementize REST namespace.');
$assert(str_contains($installer, 'type: add-headers') && str_contains($installer, 'host:'), 'ngrok Traffic Policy must restore the local WordPress Host header.');
$assert(str_contains($installer, 'ngrokOrigin = $ngrokOrigin') && str_contains($installer, 'ngrokWebPort = $ngrokWebPort'), 'Stable ngrok origin and local status port must be persisted.');
$assert(!str_contains($installer, "@('--url'"), 'Free-plan ngrok startup must use the account-assigned development domain rather than request a custom URL.');
$assert(str_contains($installer, '$siteHash = Get-ShortSha256 $LocalOrigin.ToLowerInvariant() 8'), 'Worker identity must include a deterministic site-origin hash.');
$assert(str_contains($installer, '$WorkerName = "elementize-relay-$siteKey"'), 'Default Worker name must stay unique per site.');
$assert(str_contains($installer, '$projectDir = Join-Path $runtimeRoot \'worker\''), 'Worker deploy source must remain isolated from Wrangler tooling.');
$assert(str_contains($installer, '$wranglerDir = Join-Path $runtimeRoot \'wrangler\''), 'Wrangler must remain isolated from Worker source.');
$assert(str_contains($installer, 'Wrangler: reuse'), 'A healthy Wrangler installation must be reused.');
$assert(str_contains($installer, 'existingStableOrigin'), 'Migration must preserve the existing workers.dev CustomGPT URL.');
$assert(str_contains($installer, 'elementize-relay-runtime.json'), 'Installer must persist a web-context-safe runtime pointer.');

$assert(str_contains($starter, "ngrok.exe"), 'Relay monitor must supervise ngrok, not cloudflared.');
$assert(str_contains($starter, "--traffic-policy-file"), 'Relay monitor must apply the Elementize-only ngrok Traffic Policy.');
$assert(str_contains($starter, '/api/tunnels'), 'Relay monitor must verify the assigned ngrok development endpoint through the local agent API.');
$assert(!str_contains($starter, 'cloudflared') && !str_contains($starter, 'trycloudflare') && !str_contains($starter, 'wrangler deploy'), 'Relay restart path must not recreate Quick Tunnels or redeploy the Worker.');
$assert(str_contains($controller, "migration_required"), 'Controller must identify legacy workers.dev runtimes that need migration.');
$assert(str_contains($controller, 'start-ngrok-relay.ps1') && str_contains($controller, "ngrok.exe"), 'Controller must manage the ngrok runtime.');
$assert(str_contains($controller, "'disable' { Stop-Relay; Remove-Autostart }"), 'Full shutdown must stop the relay and disable Windows autostart.');
$assert(str_contains($controller, "'enable' { Write-Autostart; Start-Relay }"), 'Full startup must enable Windows autostart and start the relay.');
$assert(str_contains($controller, '$env:USERPROFILE') && str_contains($controller, "'AppData\Local'"), 'Relay controller must work in WordPress PHP-CGI where LOCALAPPDATA can be absent.');

foreach (['install-free-cloudflare-relay.ps1','start-free-cloudflare-relay.ps1','control-free-cloudflare-relay.ps1'] as $legacyRelayScript) {
    $assert(!is_file($root . '/tools/windows/' . $legacyRelayScript), "Legacy Quick Tunnel script must stay removed: $legacyRelayScript");
}

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
