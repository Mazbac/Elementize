<?php
/**
 * Plugin Name: Elementize
 * Description: A controlled REST API for conversational WordPress, Elementor, and Pixfort editing.
 * Version: 0.2.1
 * Requires at least: 6.5
 * Requires PHP: 8.0
 * Requires Plugins: elementor
 * Author: Mazbac
 * License: GPL-2.0-or-later
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

final class Elementize_Plugin_0201 {
	private const VERSION = '0.2.1';
	private const REST_NAMESPACE = 'elementize/v1';
	private const MAX_TEXT_LENGTH = 20000;
	private const MAX_UPDATES = 100;

	public static function init(): void {
		add_action( 'rest_api_init', [ self::class, 'register_routes' ] );
		add_action( 'admin_menu', [ self::class, 'register_admin_menu' ] );
	}

	public static function register_admin_menu(): void {
		add_menu_page(
			'Elementize',
			'Elementize',
			'manage_options',
			'elementize',
			[ self::class, 'render_admin_page' ],
			'dashicons-admin-tools',
			58
		);
	}

	public static function render_admin_page(): void {
		if ( ! current_user_can( 'manage_options' ) ) {
			return;
		}

		$elementor_loaded = class_exists( '\\Elementor\\Plugin' );
		$pixfort = self::get_pixfort_plugin_info();
		$theme = wp_get_theme();
		$pixfort_library_file = self::get_pixfort_library_loader_path();
		$pixfort_library_available = file_exists( $pixfort_library_file );
		?>
		<div class="wrap">
			<h1>Elementize</h1>
			<p>Controlled WordPress, Elementor, and Pixfort access for conversational tools.</p>

			<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;max-width:1000px;margin-top:20px;">
				<?php self::render_status_card( 'Elementize', true, 'Version ' . self::VERSION ); ?>
				<?php self::render_status_card( 'Elementor', $elementor_loaded, $elementor_loaded && defined( 'ELEMENTOR_VERSION' ) ? 'Version ' . ELEMENTOR_VERSION : 'Not detected' ); ?>
				<?php self::render_status_card( 'Pixfort Core', $pixfort['active'], $pixfort['active'] ? trim( $pixfort['name'] . ( $pixfort['version'] ? ' ' . $pixfort['version'] : '' ) ) : 'Not detected' ); ?>
				<?php self::render_status_card( 'Pixfort Library', $pixfort_library_available, $pixfort_library_available ? 'Catalogue source detected' : 'Catalogue source not detected' ); ?>
				<?php self::render_status_card( 'REST API', true, 'Endpoints registered' ); ?>
			</div>

			<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;">
				<h2 style="margin-top:0;">Environment</h2>
				<table class="widefat striped" style="border:0;box-shadow:none;">
					<tbody>
						<tr><td><strong>WordPress</strong></td><td><?php echo esc_html( get_bloginfo( 'version' ) ); ?></td></tr>
						<tr><td><strong>Theme</strong></td><td><?php echo esc_html( trim( $theme->get( 'Name' ) . ' ' . $theme->get( 'Version' ) ) ); ?></td></tr>
						<tr><td><strong>API status endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::REST_NAMESPACE . '/status' ) ); ?></code></td></tr>
						<tr><td><strong>Pixfort catalogue endpoint</strong></td><td><code><?php echo esc_html( rest_url( self::REST_NAMESPACE . '/pixfort/templates' ) ); ?></code></td></tr>
					</tbody>
				</table>
			</div>

			<div class="card" style="max-width:1000px;margin-top:20px;padding:20px;">
				<h2 style="margin-top:0;">Current test state</h2>
				<p><strong>V0.1 Elementor copy editing is proven.</strong> Version 0.2.1 adds the first read-only Pixfort catalogue endpoint.</p>
				<p>No Pixfort template insertion or destructive page operations are exposed in this build.</p>
			</div>
		</div>
		<?php
	}

	private static function render_status_card( string $label, bool $ok, string $detail ): void {
		$badge_background = $ok ? '#edfaef' : '#fcf0f1';
		$badge_color = $ok ? '#116329' : '#8a2424';
		$badge_text = $ok ? 'Detected' : 'Attention';
		?>
		<div class="card" style="margin:0;padding:20px;">
			<div style="display:flex;justify-content:space-between;align-items:center;gap:12px;">
				<h2 style="margin:0;"><?php echo esc_html( $label ); ?></h2>
				<span style="display:inline-block;padding:4px 8px;border-radius:999px;background:<?php echo esc_attr( $badge_background ); ?>;color:<?php echo esc_attr( $badge_color ); ?>;font-weight:600;"><?php echo esc_html( $badge_text ); ?></span>
			</div>
			<p style="margin-bottom:0;"><?php echo esc_html( $detail ); ?></p>
		</div>
		<?php
	}

	private static function get_pixfort_plugin_info(): array {
		if ( ! function_exists( 'get_plugins' ) ) {
			require_once ABSPATH . 'wp-admin/includes/plugin.php';
		}

		$active_plugins = (array) get_option( 'active_plugins', [] );
		if ( is_multisite() ) {
			$active_plugins = array_merge(
				$active_plugins,
				array_keys( (array) get_site_option( 'active_sitewide_plugins', [] ) )
			);
		}

		$plugins = get_plugins();
		foreach ( array_unique( $active_plugins ) as $plugin_file ) {
			$plugin_data = $plugins[ $plugin_file ] ?? [];
			$name = isset( $plugin_data['Name'] ) ? (string) $plugin_data['Name'] : '';
			if ( false === stripos( $plugin_file, 'pixfort' ) && false === stripos( $name, 'pixfort' ) ) {
				continue;
			}

			return [
				'active'  => true,
				'name'    => $name ?: 'Pixfort plugin',
				'version' => isset( $plugin_data['Version'] ) ? (string) $plugin_data['Version'] : '',
			];
		}

		return [ 'active' => false, 'name' => '', 'version' => '' ];
	}

	public static function register_routes(): void {
		register_rest_route(
			self::REST_NAMESPACE,
			'/status',
			[
				'methods'             => WP_REST_Server::READABLE,
				'callback'            => [ self::class, 'get_status' ],
				'permission_callback' => [ self::class, 'can_access' ],
			]
		);

		register_rest_route(
			self::REST_NAMESPACE,
			'/pages',
			[
				'methods'             => WP_REST_Server::READABLE,
				'callback'            => [ self::class, 'list_pages' ],
				'permission_callback' => [ self::class, 'can_access' ],
				'args'                => [
					'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
					'per_page' => [ 'type' => 'integer', 'default' => 50, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
				],
			]
		);

		register_rest_route(
			self::REST_NAMESPACE,
			'/pixfort/templates',
			[
				'methods'             => WP_REST_Server::READABLE,
				'callback'            => [ self::class, 'list_pixfort_templates' ],
				'permission_callback' => [ self::class, 'can_manage_pixfort' ],
				'args'                => [
					'type' => [ 'type' => 'string', 'default' => 'section', 'sanitize_callback' => 'sanitize_key' ],
					'q' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_text_field' ],
					'category' => [ 'type' => 'string', 'default' => '', 'sanitize_callback' => 'sanitize_key' ],
					'page' => [ 'type' => 'integer', 'default' => 1, 'minimum' => 1, 'sanitize_callback' => 'absint' ],
					'per_page' => [ 'type' => 'integer', 'default' => 50, 'minimum' => 1, 'maximum' => 100, 'sanitize_callback' => 'absint' ],
				],
			]
		);

		register_rest_route(
			self::REST_NAMESPACE,
			'/pages/(?P<id>\\d+)/text',
			[
				[
					'methods'             => WP_REST_Server::READABLE,
					'callback'            => [ self::class, 'get_page_text' ],
					'permission_callback' => [ self::class, 'can_edit_requested_page' ],
					'args'                => [ 'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ] ],
				],
				[
					'methods'             => WP_REST_Server::EDITABLE,
					'callback'            => [ self::class, 'update_page_text' ],
					'permission_callback' => [ self::class, 'can_edit_requested_page' ],
					'args'                => [
						'id' => [ 'type' => 'integer', 'required' => true, 'sanitize_callback' => 'absint' ],
						'content_hash' => [ 'type' => 'string', 'required' => true ],
						'updates' => [ 'type' => 'array', 'required' => true ],
					],
				],
			]
		);
	}

	public static function can_access() {
		if ( ! is_user_logged_in() ) {
			return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
		}
		if ( ! current_user_can( 'edit_pages' ) ) {
			return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit pages.', [ 'status' => 403 ] );
		}
		return true;
	}

	public static function can_manage_pixfort() {
		if ( ! is_user_logged_in() ) {
			return new WP_Error( 'elementize_auth_required', 'Authentication is required.', [ 'status' => 401 ] );
		}
		if ( ! current_user_can( 'manage_options' ) ) {
			return new WP_Error( 'elementize_pixfort_forbidden', 'Pixfort library access requires an administrator.', [ 'status' => 403 ] );
		}
		return true;
	}

	public static function can_edit_requested_page( WP_REST_Request $request ) {
		$base_permission = self::can_access();
		if ( is_wp_error( $base_permission ) ) {
			return $base_permission;
		}

		$post_id = absint( $request['id'] );
		$post = get_post( $post_id );
		if ( ! $post || 'page' !== $post->post_type ) {
			return new WP_Error( 'elementize_page_not_found', 'Page not found.', [ 'status' => 404 ] );
		}
		if ( ! current_user_can( 'edit_post', $post_id ) ) {
			return new WP_Error( 'elementize_forbidden', 'The authenticated WordPress user cannot edit this page.', [ 'status' => 403 ] );
		}
		return true;
	}

	public static function get_status(): WP_REST_Response {
		$elementor_loaded = class_exists( '\\Elementor\\Plugin' );
		$theme = wp_get_theme();
		$pixfort = self::get_pixfort_plugin_info();
		return new WP_REST_Response(
			[
				'elementize_version'       => self::VERSION,
				'wordpress_version'        => get_bloginfo( 'version' ),
				'elementor_loaded'         => $elementor_loaded,
				'elementor_version'        => defined( 'ELEMENTOR_VERSION' ) ? ELEMENTOR_VERSION : null,
				'pixfort_loaded'           => $pixfort['active'],
				'pixfort_name'             => $pixfort['active'] ? $pixfort['name'] : null,
				'pixfort_version'          => $pixfort['active'] ? $pixfort['version'] : null,
				'pixfort_library_detected' => file_exists( self::get_pixfort_library_loader_path() ),
				'theme'                    => $theme->get( 'Name' ),
				'theme_version'            => $theme->get( 'Version' ),
			],
			200
		);
	}

	public static function list_pages( WP_REST_Request $request ) {
		if ( ! class_exists( '\\Elementor\\Plugin' ) ) {
			return self::elementor_missing_error();
		}

		$page = max( 1, absint( $request->get_param( 'page' ) ) );
		$per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );
		$statuses = array_keys( get_post_stati( [ 'internal' => false ] ) );
		$query = new WP_Query(
			[
				'post_type' => 'page',
				'post_status' => $statuses,
				'perm' => 'editable',
				'posts_per_page' => $per_page,
				'paged' => $page,
				'orderby' => 'modified',
				'order' => 'DESC',
			]
		);

		$pages = [];
		foreach ( $query->posts as $post ) {
			if ( ! current_user_can( 'edit_post', $post->ID ) ) {
				continue;
			}
			$document = self::get_elementor_document( $post->ID );
			$is_elementor = $document && $document->is_built_with_elementor();
			$pages[] = [
				'id' => $post->ID,
				'title' => get_the_title( $post ),
				'status' => $post->post_status,
				'modified_gmt' => $post->post_modified_gmt,
				'url' => get_permalink( $post ),
				'is_elementor' => (bool) $is_elementor,
				'elementor_edit_url' => $is_elementor ? $document->get_edit_url() : null,
			];
		}

		return new WP_REST_Response(
			[
				'page' => $page,
				'per_page' => $per_page,
				'total' => (int) $query->found_posts,
				'total_pages' => (int) $query->max_num_pages,
				'pages' => $pages,
			],
			200
		);
	}

	public static function list_pixfort_templates( WP_REST_Request $request ) {
		$library = self::load_pixfort_library();
		if ( is_wp_error( $library ) ) {
			return $library;
		}

		$type = strtolower( (string) $request->get_param( 'type' ) );
		if ( '' === $type ) {
			$type = 'section';
		}
		if ( ! in_array( $type, [ 'section', 'page', 'all' ], true ) ) {
			return new WP_Error( 'elementize_invalid_pixfort_type', 'Pixfort type must be section, page, or all.', [ 'status' => 400 ] );
		}

		$q = strtolower( trim( (string) $request->get_param( 'q' ) ) );
		$category = strtolower( trim( (string) $request->get_param( 'category' ) ) );
		$page = max( 1, absint( $request->get_param( 'page' ) ) );
		$per_page = min( 100, max( 1, absint( $request->get_param( 'per_page' ) ) ) );

		$templates = [];
		if ( 'section' === $type || 'all' === $type ) {
			self::append_pixfort_templates( $templates, $library['sections'] ?? [], 'section' );
		}
		if ( 'page' === $type || 'all' === $type ) {
			self::append_pixfort_templates( $templates, $library['pages'] ?? [], 'page' );
		}

		$filtered = [];
		foreach ( array_values( $templates ) as $template ) {
			$categories = array_map( 'strtolower', $template['categories'] );
			if ( '' !== $category && 'all' !== $category && ! in_array( $category, $categories, true ) ) {
				continue;
			}
			if ( '' !== $q ) {
				$haystack = strtolower( implode( ' ', array_filter( [ $template['id'], $template['title'], $template['subtype'], implode( ' ', $template['categories'] ) ] ) ) );
				if ( false === strpos( $haystack, $q ) ) {
					continue;
				}
			}
			$filtered[] = $template;
		}

		usort( $filtered, static fn( array $a, array $b ): int => strcasecmp( $a['title'], $b['title'] ) );
		$total = count( $filtered );
		$total_pages = max( 1, (int) ceil( $total / $per_page ) );
		$offset = ( $page - 1 ) * $per_page;
		$items = array_slice( $filtered, $offset, $per_page );

		if ( 'section' === $type ) {
			$category_data = self::normalize_pixfort_categories( $library['sectionsCategories'] ?? [] );
		} elseif ( 'page' === $type ) {
			$category_data = self::normalize_pixfort_categories( $library['pagesCategories'] ?? [] );
		} else {
			$category_data = [
				'section' => self::normalize_pixfort_categories( $library['sectionsCategories'] ?? [] ),
				'page' => self::normalize_pixfort_categories( $library['pagesCategories'] ?? [] ),
			];
		}

		return new WP_REST_Response(
			[
				'type' => $type,
				'q' => $q,
				'category' => $category,
				'page' => $page,
				'per_page' => $per_page,
				'total' => $total,
				'total_pages' => $total_pages,
				'available_categories' => $category_data,
				'templates' => $items,
			],
			200
		);
	}

	private static function append_pixfort_templates( array &$templates, $source, string $kind ): void {
		if ( ! is_array( $source ) ) {
			return;
		}

		foreach ( $source as $raw ) {
			if ( ! is_array( $raw ) || empty( $raw['id'] ) ) {
				continue;
			}

			$id = (string) $raw['id'];
			$key = $kind . '|' . $id;
			$categories = [];
			if ( isset( $raw['categories'] ) && is_array( $raw['categories'] ) ) {
				foreach ( $raw['categories'] as $raw_category ) {
					if ( is_string( $raw_category ) && '' !== trim( $raw_category ) ) {
						$categories[] = strtolower( trim( $raw_category ) );
					}
				}
			}

			if ( isset( $templates[ $key ] ) ) {
				$templates[ $key ]['categories'] = array_values( array_unique( array_merge( $templates[ $key ]['categories'], $categories ) ) );
				continue;
			}

			$templates[ $key ] = [
				'id' => $id,
				'kind' => $kind,
				'title' => isset( $raw['title'] ) ? (string) $raw['title'] : $id,
				'thumbnail' => isset( $raw['thumbnail'] ) ? esc_url_raw( (string) $raw['thumbnail'] ) : null,
				'preview_url' => isset( $raw['url'] ) ? esc_url_raw( (string) $raw['url'] ) : null,
				'pixfort_type' => isset( $raw['type'] ) ? (string) $raw['type'] : null,
				'subtype' => isset( $raw['subtype'] ) ? (string) $raw['subtype'] : null,
				'categories' => array_values( array_unique( $categories ) ),
				'container_based' => ! empty( $raw['container_based'] ),
			];
		}
	}

	private static function normalize_pixfort_categories( $source ): array {
		if ( ! is_array( $source ) ) {
			return [];
		}

		$categories = [];
		foreach ( $source as $raw ) {
			if ( ! is_array( $raw ) || empty( $raw['id'] ) ) {
				continue;
			}
			$categories[] = [
				'id' => (string) $raw['id'],
				'title' => isset( $raw['title'] ) ? (string) $raw['title'] : (string) $raw['id'],
				'number' => isset( $raw['number'] ) ? absint( $raw['number'] ) : null,
			];
		}
		return $categories;
	}

	private static function load_pixfort_library() {
		static $cached_library = null;
		if ( is_array( $cached_library ) ) {
			return $cached_library;
		}

		if ( ! function_exists( 'pixfort_elementor_library_data' ) ) {
			$loader = self::get_pixfort_library_loader_path();
			if ( file_exists( $loader ) ) {
				require_once $loader;
			}
		}

		if ( ! function_exists( 'pixfort_elementor_library_data' ) ) {
			return new WP_Error( 'elementize_pixfort_library_unavailable', 'The active theme does not expose the Pixfort Elementor template library.', [ 'status' => 503 ] );
		}

		try {
			$library = pixfort_elementor_library_data();
		} catch ( Throwable $exception ) {
			return new WP_Error( 'elementize_pixfort_library_failed', 'Pixfort could not load its Elementor template catalogue: ' . $exception->getMessage(), [ 'status' => 500 ] );
		}

		if ( ! is_array( $library ) || ! isset( $library['sections'], $library['pages'] ) ) {
			return new WP_Error( 'elementize_pixfort_library_invalid', 'Pixfort returned an invalid template catalogue.', [ 'status' => 500 ] );
		}

		$cached_library = $library;
		return $cached_library;
	}

	private static function get_pixfort_library_loader_path(): string {
		return trailingslashit( get_template_directory() ) . 'inc/demo-content/elementor/loader.php';
	}

	public static function get_page_text( WP_REST_Request $request ) {
		$post_id = absint( $request['id'] );
		$document = self::require_elementor_document( $post_id );
		if ( is_wp_error( $document ) ) {
			return $document;
		}

		$elements = $document->get_elements_data();
		if ( ! is_array( $elements ) ) {
			return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
		}

		$items = [];
		self::collect_text_items_from_elements( $elements, $items );
		$post = get_post( $post_id );
		return new WP_REST_Response(
			[
				'page' => [
					'id' => $post_id,
					'title' => get_the_title( $post_id ),
					'status' => $post->post_status,
					'modified_gmt' => $post->post_modified_gmt,
					'edit_url' => $document->get_edit_url(),
					'permalink' => get_permalink( $post_id ),
				],
				'content_hash' => self::hash_elements( $elements ),
				'text_items' => $items,
			],
			200
		);
	}

	public static function update_page_text( WP_REST_Request $request ) {
		$post_id = absint( $request['id'] );
		$document = self::require_elementor_document( $post_id );
		if ( is_wp_error( $document ) ) {
			return $document;
		}

		$elements = $document->get_elements_data();
		if ( ! is_array( $elements ) ) {
			return new WP_Error( 'elementize_invalid_elementor_data', 'Elementor returned invalid page data.', [ 'status' => 500 ] );
		}

		$expected_hash = (string) $request->get_param( 'content_hash' );
		$current_hash = self::hash_elements( $elements );
		if ( ! hash_equals( $current_hash, $expected_hash ) ) {
			return new WP_Error( 'elementize_content_changed', 'The Elementor page changed after it was read. Read the page again before applying edits.', [ 'status' => 409, 'content_hash' => $current_hash ] );
		}

		$updates = $request->get_param( 'updates' );
		if ( ! is_array( $updates ) || empty( $updates ) ) {
			return new WP_Error( 'elementize_updates_required', 'At least one text update is required.', [ 'status' => 400 ] );
		}
		if ( count( $updates ) > self::MAX_UPDATES ) {
			return new WP_Error( 'elementize_too_many_updates', 'Send at most ' . self::MAX_UPDATES . ' text updates per request.', [ 'status' => 400 ] );
		}

		$changed = [];
		foreach ( $updates as $index => $update ) {
			$validation = self::validate_update( $update, $index );
			if ( is_wp_error( $validation ) ) {
				return $validation;
			}

			$old_value = null;
			$new_value = wp_kses_post( (string) $update['value'] );
			$result = self::apply_text_update(
				$elements,
				(string) $update['element_id'],
				$update['setting_path'],
				$new_value,
				array_key_exists( 'expected_value', $update ) ? (string) $update['expected_value'] : null,
				$old_value
			);
			if ( is_wp_error( $result ) ) {
				return $result;
			}
			if ( false === $result ) {
				return new WP_Error( 'elementize_element_not_found', 'The requested Elementor element no longer exists.', [ 'status' => 409 ] );
			}

			$changed[] = [
				'element_id' => (string) $update['element_id'],
				'setting_path' => array_values( $update['setting_path'] ),
				'old_value' => $old_value,
				'new_value' => $new_value,
			];
		}

		$revision = self::create_prechange_revision( $post_id );
		if ( is_wp_error( $revision ) ) {
			return $revision;
		}

		try {
			$saved = $document->save( [ 'elements' => $elements ] );
		} catch ( Throwable $exception ) {
			return new WP_Error( 'elementize_save_failed', 'Elementor could not save the page: ' . $exception->getMessage(), [ 'status' => 500 ] );
		}
		if ( ! $saved ) {
			return new WP_Error( 'elementize_save_failed', 'Elementor refused to save the page.', [ 'status' => 500 ] );
		}

		$reloaded_document = self::get_elementor_document( $post_id );
		$reloaded_elements = $reloaded_document ? $reloaded_document->get_elements_data() : $elements;
		return new WP_REST_Response(
			[
				'saved' => true,
				'page_id' => $post_id,
				'updated_count' => count( $changed ),
				'revision_id' => $revision,
				'content_hash' => self::hash_elements( is_array( $reloaded_elements ) ? $reloaded_elements : $elements ),
				'changes' => $changed,
				'edit_url' => $document->get_edit_url(),
				'permalink' => get_permalink( $post_id ),
			],
			200
		);
	}

	private static function create_prechange_revision( int $post_id ) {
		$post = get_post( $post_id );
		$revisions_enabled = $post && post_type_supports( $post->post_type, 'revisions' ) && wp_revisions_enabled( $post );
		if ( ! $revisions_enabled ) {
			return null;
		}

		add_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
		try {
			$saved_revision = wp_save_post_revision( $post_id );
		} finally {
			remove_filter( 'wp_save_post_revision_check_for_changes', '__return_false', PHP_INT_MAX );
		}

		if ( is_wp_error( $saved_revision ) || ! is_int( $saved_revision ) || $saved_revision <= 0 ) {
			return new WP_Error( 'elementize_revision_failed', 'Elementize could not create a pre-change revision, so the page was not modified.', [ 'status' => 500 ] );
		}
		return $saved_revision;
	}

	private static function validate_update( $update, int $index ) {
		if ( ! is_array( $update ) ) {
			return self::invalid_update_error( $index, 'Each update must be an object.' );
		}
		if ( empty( $update['element_id'] ) || ! is_string( $update['element_id'] ) ) {
			return self::invalid_update_error( $index, 'element_id is required.' );
		}
		if ( empty( $update['setting_path'] ) || ! is_array( $update['setting_path'] ) ) {
			return self::invalid_update_error( $index, 'setting_path must be a non-empty array.' );
		}
		if ( ! array_key_exists( 'value', $update ) || ! is_string( $update['value'] ) ) {
			return self::invalid_update_error( $index, 'value must be a string.' );
		}
		if ( strlen( $update['value'] ) > self::MAX_TEXT_LENGTH ) {
			return self::invalid_update_error( $index, 'value is too long.' );
		}
		if ( array_key_exists( 'expected_value', $update ) && ! is_string( $update['expected_value'] ) ) {
			return self::invalid_update_error( $index, 'expected_value must be a string when provided.' );
		}
		return true;
	}

	private static function invalid_update_error( int $index, string $message ): WP_Error {
		return new WP_Error( 'elementize_invalid_update', 'Invalid update at index ' . $index . ': ' . $message, [ 'status' => 400 ] );
	}

	private static function apply_text_update( array &$elements, string $element_id, array $setting_path, string $new_value, ?string $expected_value, ?string &$old_value ) {
		foreach ( $elements as &$element ) {
			if ( ! is_array( $element ) ) {
				continue;
			}

			if ( isset( $element['id'] ) && (string) $element['id'] === $element_id ) {
				if ( ! isset( $element['settings'] ) || ! is_array( $element['settings'] ) ) {
					return new WP_Error( 'elementize_setting_not_found', 'The target Elementor element has no editable settings.', [ 'status' => 400 ] );
				}
				$cursor =& $element['settings'];
				foreach ( $setting_path as $segment ) {
					if ( ! is_string( $segment ) && ! is_int( $segment ) ) {
						return new WP_Error( 'elementize_invalid_setting_path', 'Each setting_path segment must be a string or integer.', [ 'status' => 400 ] );
					}
					if ( ! is_array( $cursor ) || ! array_key_exists( $segment, $cursor ) ) {
						return new WP_Error( 'elementize_setting_not_found', 'The requested text setting no longer exists.', [ 'status' => 409 ] );
					}
					$cursor =& $cursor[ $segment ];
				}
				if ( ! is_string( $cursor ) || ! self::is_copy_candidate( $setting_path, $cursor ) ) {
					return new WP_Error( 'elementize_not_copy_field', 'Elementize only permits text updates to recognized copy fields in this build.', [ 'status' => 400 ] );
				}
				if ( null !== $expected_value && $cursor !== $expected_value ) {
					return new WP_Error( 'elementize_value_changed', 'The target text changed after it was read.', [ 'status' => 409 ] );
				}
				$old_value = $cursor;
				$cursor = $new_value;
				return true;
			}

			if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
				$result = self::apply_text_update( $element['elements'], $element_id, $setting_path, $new_value, $expected_value, $old_value );
				if ( true === $result || is_wp_error( $result ) ) {
					return $result;
				}
			}
		}
		unset( $element );
		return false;
	}

	private static function collect_text_items_from_elements( array $elements, array &$items ): void {
		foreach ( $elements as $element ) {
			if ( ! is_array( $element ) ) {
				continue;
			}
			$element_id = isset( $element['id'] ) ? (string) $element['id'] : '';
			$element_type = isset( $element['elType'] ) ? (string) $element['elType'] : null;
			$widget_type = isset( $element['widgetType'] ) ? (string) $element['widgetType'] : null;
			if ( $element_id && ! empty( $element['settings'] ) && is_array( $element['settings'] ) ) {
				self::collect_text_items_from_settings( $element['settings'], [], $element_id, $element_type, $widget_type, $items );
			}
			if ( ! empty( $element['elements'] ) && is_array( $element['elements'] ) ) {
				self::collect_text_items_from_elements( $element['elements'], $items );
			}
		}
	}

	private static function collect_text_items_from_settings( $value, array $path, string $element_id, ?string $element_type, ?string $widget_type, array &$items ): void {
		if ( is_array( $value ) ) {
			foreach ( $value as $key => $nested_value ) {
				self::collect_text_items_from_settings( $nested_value, array_merge( $path, [ $key ] ), $element_id, $element_type, $widget_type, $items );
			}
			return;
		}
		if ( ! is_string( $value ) || ! self::is_copy_candidate( $path, $value ) ) {
			return;
		}
		$items[] = [
			'element_id' => $element_id,
			'element_type' => $element_type,
			'widget_type' => $widget_type,
			'setting_path' => array_values( $path ),
			'format' => wp_strip_all_tags( $value ) === $value ? 'text' : 'html',
			'value' => $value,
		];
	}

	private static function is_copy_candidate( array $path, string $value ): bool {
		if ( '' === trim( $value ) || strlen( $value ) > self::MAX_TEXT_LENGTH ) {
			return false;
		}
		if ( str_starts_with( ltrim( $value ), '[elementor-tag' ) ) {
			return false;
		}

		$key = '';
		for ( $i = count( $path ) - 1; $i >= 0; $i-- ) {
			if ( is_string( $path[ $i ] ) ) {
				$key = strtolower( $path[ $i ] );
				break;
			}
		}
		if ( '' === $key ) {
			return false;
		}

		$blocked = '/(^|_)(url|link|href|src|id|class|selector|shortcode|icon|image|media|color|background|gradient|font|typography|size|width|height|margin|padding|border|radius|opacity|align|position|offset|z_index|animation|transition|transform|query|taxonomy|post_id)(_|$)/i';
		if ( preg_match( $blocked, $key ) ) {
			return false;
		}
		$allowed = '/(^|_)(title|text|content|editor|description|heading|headline|subtitle|sub_title|label|caption|button_text|btn_text|placeholder|prefix|suffix|message)(_|$)/i';
		return (bool) preg_match( $allowed, $key );
	}

	private static function require_elementor_document( int $post_id ) {
		if ( ! class_exists( '\\Elementor\\Plugin' ) ) {
			return self::elementor_missing_error();
		}
		$document = self::get_elementor_document( $post_id );
		if ( ! $document ) {
			return new WP_Error( 'elementize_elementor_document_not_found', 'Elementor could not load this page as a document.', [ 'status' => 404 ] );
		}
		if ( ! $document->is_built_with_elementor() ) {
			return new WP_Error( 'elementize_not_elementor_page', 'This page is not currently built with Elementor.', [ 'status' => 400 ] );
		}
		return $document;
	}

	private static function get_elementor_document( int $post_id ) {
		try {
			return \Elementor\Plugin::$instance->documents->get( $post_id );
		} catch ( Throwable $exception ) {
			return null;
		}
	}

	private static function hash_elements( array $elements ): string {
		$json = wp_json_encode( $elements, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE );
		return hash( 'sha256', false === $json ? '' : $json );
	}

	private static function elementor_missing_error(): WP_Error {
		return new WP_Error( 'elementize_elementor_required', 'Elementor must be installed and active.', [ 'status' => 503 ] );
	}
}

Elementize_Plugin_0201::init();
