<?php
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/elementize-core.inc';
require_once __DIR__ . '/elementize-content.inc';
require_once __DIR__ . '/elementize-media-library.inc';
require_once __DIR__ . '/elementize-chat-media.inc';
require_once __DIR__ . '/elementize-onboarding.inc';

Elementize_Core::init();
Elementize_Content::init();
Elementize_Media_Library::init();
Elementize_Chat_Media::init();
Elementize_Onboarding::init();
