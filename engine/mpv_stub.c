#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <stdlib.h>
#include <string.h>

struct mpv_handle {
    int dummy;
};

struct mpv_render_context {
    int dummy;
};

mpv_handle* mpv_create(void) {
    mpv_handle* h = (mpv_handle*)calloc(1, sizeof(mpv_handle));
    return h;
}

int mpv_initialize(mpv_handle* ctx) { return 0; }

void mpv_terminate_destroy(mpv_handle* ctx) {
    if (ctx) free(ctx);
}

int mpv_set_option_string(mpv_handle* ctx, const char* name, const char* data) { return 0; }
int mpv_set_property_string(mpv_handle* ctx, const char* name, const char* data) { return 0; }
int mpv_get_property(mpv_handle* ctx, const char* name, mpv_format format, void* data) { return -1; }
int mpv_observe_property(mpv_handle* mpv, uint64_t reply_userdata, const char* name, mpv_format format) { return 0; }
int mpv_command(mpv_handle* ctx, const char** args) { return 0; }
int mpv_command_async(mpv_handle* ctx, uint64_t reply_userdata, const char** args) { return 0; }
int mpv_command_node(mpv_handle* ctx, mpv_node* args, mpv_node* result) { return 0; }
void mpv_free_node_contents(mpv_node* node) {}
void mpv_free(void* data) { if (data) free(data); }
int mpv_request_log_messages(mpv_handle* ctx, const char* min_level) { return 0; }

mpv_event* mpv_wait_event(mpv_handle* ctx, double timeout) {
    static mpv_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event_id = MPV_EVENT_NONE;
    return &ev;
}

const char* mpv_event_name(mpv_event_id event) { return "none"; }
const char* mpv_error_string(int error) { return "ok"; }

int mpv_render_context_create(mpv_render_context** res, mpv_handle* mpv, mpv_render_param* params) {
    if (res) {
        *res = (mpv_render_context*)calloc(1, sizeof(mpv_render_context));
    }
    return 0;
}

int mpv_render_context_render(mpv_render_context* ctx, mpv_render_param* params) { return 0; }
void mpv_render_context_report_swap(mpv_render_context* ctx) {}
void mpv_render_context_free(mpv_render_context* ctx) {
    if (ctx) free(ctx);
}

int mpv_stream_cb_add_ro(mpv_handle *ctx, const char *protocol, void *user_data, void *open_fn) {
    return 0;
}

int WebPGetInfo(const uint8_t* data, size_t data_size, int* width, int* height) {
    return 0;
}

uint8_t* WebPDecodeRGBA(const uint8_t* data, size_t data_size, int* width, int* height) {
    return NULL;
}

void WebPFree(void* ptr) {
    if (ptr) free(ptr);
}
