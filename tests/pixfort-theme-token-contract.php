<?php

define( 'ABSPATH', __DIR__ . '/../' );

if ( ! class_exists( 'WP_Error' ) ) {
    class WP_Error {
        private string $code;
        private string $message;
        private $data;
        public function __construct( string $code = '', string $message = '', $data = null ) {
            $this->code = $code;
            $this->message = $message;
            $this->data = $data;
        }
        public function get_error_code(): string { return $this->code; }
        public function get_error_message(): string { return $this->message; }
        public function get_error_data() { return $this->data; }
    }
}

if ( ! function_exists( 'is_wp_error' ) ) {
    function is_wp_error( $value ): bool { return $value instanceof WP_Error; }
}
if ( ! function_exists( 'sanitize_key' ) ) {
    function sanitize_key( $value ): string { return strtolower( preg_replace( '/[^a-z0-9_\-]/i', '', (string) $value ) ); }
}
if ( ! function_exists( 'sanitize_text_field' ) ) {
    function sanitize_text_field( $value ): string { return trim( strip_tags( (string) $value ) ); }
}

require_once __DIR__ . '/../includes/elementize-pixfort-theme-tokens.inc';

function fail_pixfort_theme_token_contract( string $message ): void {
    fwrite( STDERR, $message . PHP_EOL );
    exit( 1 );
}

function set_private_static_pixfort( string $property, $value ): void {
    $reflection = new ReflectionProperty( Elementize_Pixfort_Theme_Tokens::class, $property );
    $reflection->setValue( null, $value );
}

$fixture_meta = [
    'pix-highlighted-text|title_color' => [
        'type' => 'select',
        'lookup_source' => 'contract_fixture',
        'options' => [
            'dark-opacity-4' => 'Dark opacity 4',
            'heading-default' => 'Heading default',
            'body-default' => 'Body default',
            'primary' => 'Primary',
            'secondary' => 'Secondary',
            'custom' => 'Custom',
        ],
    ],
];
$fixture_roles = [
    'primary' => [
        'value' => '#56188f',
        'source' => 'pixfort_theme_option',
        'option_key' => 'opt-primary-color',
        'authoritative' => true,
    ],
    'secondary' => [
        'value' => '#ccafe6',
        'source' => 'pixfort_theme_option',
        'option_key' => 'opt-secondary-color',
        'authoritative' => true,
    ],
];

set_private_static_pixfort( 'meta_cache', $fixture_meta );
set_private_static_pixfort( 'theme_roles', $fixture_roles );

$meta = Elementize_Pixfort_Theme_Tokens::control_meta( 'pix-highlighted-text', 'title_color', 'dark-opacity-4' );
if ( ! is_array( $meta ) ) fail_pixfort_theme_token_contract( 'Fixture semantic Pixfort control was not recognized.' );

$normalization_values = [];
foreach ( (array) ( $meta['normalization_options'] ?? [] ) as $option ) {
    if ( is_array( $option ) ) $normalization_values[] = (string) ( $option['value'] ?? '' );
}
if ( ! in_array( 'heading-default', $normalization_values, true ) ) {
    fail_pixfort_theme_token_contract( 'Installed heading-default selector was not exposed as a writable normalization option.' );
}
if ( in_array( 'custom', $normalization_values, true ) ) {
    fail_pixfort_theme_token_contract( 'Pixfort custom selector must not be exposed as semantic normalization.' );
}

$heading = Elementize_Pixfort_Theme_Tokens::validate_transition( 'pix-highlighted-text', 'title_color', 'dark-opacity-4', 'heading-default' );
if ( is_wp_error( $heading ) || 'heading-default' !== $heading ) {
    fail_pixfort_theme_token_contract( 'Exact installed heading-default selector transition was rejected.' );
}

$primary = Elementize_Pixfort_Theme_Tokens::validate_transition( 'pix-highlighted-text', 'title_color', 'dark-opacity-4', 'primary' );
if ( is_wp_error( $primary ) || 'primary' !== $primary ) {
    fail_pixfort_theme_token_contract( 'Authoritatively grounded Primary selector transition was rejected.' );
}

$custom = Elementize_Pixfort_Theme_Tokens::validate_transition( 'pix-highlighted-text', 'title_color', 'dark-opacity-4', 'custom' );
if ( ! is_wp_error( $custom ) || 'elementize_pixfort_theme_token_target_forbidden' !== $custom->get_error_code() ) {
    fail_pixfort_theme_token_contract( 'Pixfort custom selector must fail closed.' );
}

$invented = Elementize_Pixfort_Theme_Tokens::validate_transition( 'pix-highlighted-text', 'title_color', 'dark-opacity-4', 'invented-token' );
if ( ! is_wp_error( $invented ) || 'elementize_pixfort_theme_token_not_allowed' !== $invented->get_error_code() ) {
    fail_pixfort_theme_token_contract( 'Uninstalled semantic selector must fail closed.' );
}

set_private_static_pixfort( 'theme_roles', [ 'secondary' => $fixture_roles['secondary'] ] );
$unresolved_primary = Elementize_Pixfort_Theme_Tokens::validate_transition( 'pix-highlighted-text', 'title_color', 'dark-opacity-4', 'primary' );
if ( ! is_wp_error( $unresolved_primary ) || 'elementize_pixfort_theme_token_unresolved' !== $unresolved_primary->get_error_code() ) {
    fail_pixfort_theme_token_contract( 'Primary must still require authoritative Pixfort theme-role resolution.' );
}

echo "Pixfort theme-token selector contract OK\n";
