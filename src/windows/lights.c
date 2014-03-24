#include <pebble.h>
#include "lights.h"
#include "../libs/pebble-assist.h"
#include "../common.h"
#include "options.h"

#define MENU_NUM_SECTIONS 2

#define MENU_SECTION_ALL 0
#define MENU_SECTION_LIST 1

#define MENU_SECTION_ROWS_ALL 1

#define MAX_LIGHTS 30

static Light lights[MAX_LIGHTS];

static uint16_t menu_get_num_sections_callback(struct MenuLayer *menu_layer, void *callback_context);
static uint16_t menu_get_num_rows_callback(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context);
static int16_t menu_get_header_height_callback(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context);
static int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context);
static void menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context);
static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context);
static void menu_select_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context);
static void menu_select_long_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context);

static Window *window;
static MenuLayer *menu_layer;

static int num_lights = 0;
static int selected_index = 0;
static bool no_server = false;
static bool no_lights = false;
static bool out_failed = false;
static bool conn_timeout = false;
static bool conn_error = false;
static bool server_error = false;

void lights_init(void) {
	window = window_create();

	menu_layer = menu_layer_create_fullscreen(window);
	menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks) {
		.get_num_sections = menu_get_num_sections_callback,
		.get_num_rows = menu_get_num_rows_callback,
		.get_header_height = menu_get_header_height_callback,
		.get_cell_height = menu_get_cell_height_callback,
		.draw_header = menu_draw_header_callback,
		.draw_row = menu_draw_row_callback,
		.select_click = menu_select_callback,
		.select_long_click = menu_select_long_callback,
	});
	menu_layer_set_click_config_onto_window(menu_layer, window);
	menu_layer_add_to_window(menu_layer, window);

	window_stack_push(window, true);
}

void lights_destroy(void) {
	options_destroy();
	menu_layer_destroy_safe(menu_layer);
	window_destroy_safe(window);
}

void lights_in_received_handler(DictionaryIterator *iter) {
	Tuple *index_tuple = dict_find(iter, KEY_INDEX);
	Tuple *label_tuple = dict_find(iter, KEY_LABEL);
	Tuple *color_tuple = dict_find(iter, KEY_COLOR);
	Tuple *state_tuple = dict_find(iter, KEY_STATE);
	Tuple *error_tuple = dict_find(iter, KEY_ERROR);

	if (error_tuple) {
		if (strcmp(error_tuple->value->cstring, "no_server_set") != 0) {
			no_server = true;
		} else if (strcmp(error_tuple->value->cstring, "timeout") != 0) {
			conn_timeout = true;
		} else if (strcmp(error_tuple->value->cstring, "error") != 0) {
			conn_error = true;
		} else if (strcmp(error_tuple->value->cstring, "server_error") != 0) {
			server_error = true;
		}
		menu_layer_reload_data_and_mark_dirty(menu_layer);
	}
	else if (index_tuple && label_tuple && color_tuple && state_tuple) {
		no_server = false;
		out_failed = false;
		conn_timeout = false;
		conn_error = false;
		server_error = false;
		Light light;
		light.index = index_tuple->value->int16;
		strncpy(light.label, label_tuple->value->cstring, sizeof(light.label) - 1);
		strncpy(light.color, color_tuple->value->cstring, sizeof(light.color) - 1);
		strncpy(light.state, state_tuple->value->cstring, sizeof(light.state) - 1);
		lights[light.index] = light;
	}
	else if (index_tuple) {
		num_lights = index_tuple->value->int16;
		no_lights = num_lights == 0;
		menu_layer_reload_data_and_mark_dirty(menu_layer);
	}
}

void lights_out_sent_handler(DictionaryIterator *sent) {
}

void lights_out_failed_handler(DictionaryIterator *failed, AppMessageResult reason) {
	out_failed = true;
	menu_layer_reload_data_and_mark_dirty(menu_layer);
}

bool lights_is_on_top() {
	return window == window_stack_get_top_window();
}

Light* light() {
	return &lights[selected_index];
}

void toggle_light() {
	strncpy(light()->state, "...", sizeof(light()->state) - 1);
	menu_layer_reload_data_and_mark_dirty(menu_layer);
	Tuplet index_tuple = TupletInteger(KEY_INDEX, light()->index);
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL)
		return;
	dict_write_tuplet(iter, &index_tuple);
	dict_write_end(iter);
	app_message_outbox_send();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - //

static uint16_t menu_get_num_sections_callback(struct MenuLayer *menu_layer, void *callback_context) {
	return MENU_NUM_SECTIONS;
}

static uint16_t menu_get_num_rows_callback(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
	switch (section_index) {
		case MENU_SECTION_ALL:
			return MENU_SECTION_ROWS_ALL;
		case MENU_SECTION_LIST:
			return num_lights ? num_lights : 1;
		default:
			return 0;
	}
}

static int16_t menu_get_header_height_callback(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
	switch (section_index) {
		case MENU_SECTION_ALL:
			return MENU_CELL_BASIC_HEADER_HEIGHT;
		case MENU_SECTION_LIST:
			return 1; // this should be 0 but MenuLayer breaks if we set it to 0. <<< TODO
		default:
			return 0;
	}
}

static int16_t menu_get_cell_height_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
	switch (cell_index->section) {
		case MENU_SECTION_ALL:
			return 30;
		case MENU_SECTION_LIST:
			return 30;
		default:
			return 0;
	}
}

static void menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context) {
	switch (section_index) {
		case MENU_SECTION_ALL:
			menu_cell_basic_header_draw(ctx, cell_layer, "PebbLIFX");
			break;
	}
}

static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
	graphics_context_set_text_color(ctx, GColorBlack);
	switch (cell_index->section) {
		case MENU_SECTION_ALL:
			if (out_failed) {
				graphics_draw_text(ctx, "Phone unreachable!", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (no_server) {
				graphics_draw_text(ctx, "No server set", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (no_lights) {
				graphics_draw_text(ctx, "No lights found", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (conn_timeout) {
				graphics_draw_text(ctx, "Connection timed out!", fonts_get_system_font(FONT_KEY_GOTHIC_18), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 44 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (conn_error) {
				graphics_draw_text(ctx, "HTTP Error!", fonts_get_system_font(FONT_KEY_GOTHIC_18), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 44 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (server_error) {
				graphics_draw_text(ctx, "Server error!", fonts_get_system_font(FONT_KEY_GOTHIC_18), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 44 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else if (num_lights == 0) {
				graphics_draw_text(ctx, "Loading lights...", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			} else {
				graphics_draw_text(ctx, "All Lights", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { PEBBLE_WIDTH - 8, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
			}
			break;
		case MENU_SECTION_LIST:
			if (num_lights > 0) {
				graphics_draw_text(ctx, lights[cell_index->row].label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), (GRect) { .origin = { 4, 2 }, .size = { 100, 22 } }, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
				graphics_draw_text(ctx, lights[cell_index->row].state, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), (GRect) { .origin = { 110, -3 }, .size = { 30, 26 } }, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
			}
			break;
	}
}

static void menu_select_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
	switch (cell_index->section) {
		case MENU_SECTION_ALL:
			break;
		case MENU_SECTION_LIST:
			if (num_lights > 0) {
				selected_index = cell_index->row;
				options_init();
			}
			break;
	}
}

static void menu_select_long_callback(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
	toggle_light();
}
